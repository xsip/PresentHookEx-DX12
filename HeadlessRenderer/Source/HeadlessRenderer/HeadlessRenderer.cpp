#include <HeadlessRenderer/HeadlessRenderer.h>
#include <dxgi1_6.h>

#include <HeadlessRenderer/LiquidMenu.h>
#include <HeadlessRenderer/RenderObjectManager.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <algorithm>
#include <cstdio>
#include <cstring>


void SetupLiquidStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 8);

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.12f, 0.15f, 1);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.15f, 1);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.12f, 0.15f, 1);

    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.18f, 0.23f, 1);

    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.18f, 0.22f, 1);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.23f, 0.40f, 0.70f, 1);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.90f, 0.35f, 0.15f, 1);

    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.16f, 1);
    colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.45f, 0.80f, 1);
    // colors[ImGuiCol_TabActive] = ImVec4(0.90f, 0.40f, 0.20f, 1);

    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.65f, 1.0f, 1);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.55f, 1.0f, 1);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.45f, 0.20f, 1);
}
namespace {
    constexpr UINT SRV_DESCRIPTOR_CAPACITY = 128;
}

HeadlessRendererDX12::~HeadlessRendererDX12() {
    Stop();
}

bool HeadlessRendererDX12::Start(RenderObjectManager* renderer, HWND inputWindow, UINT width, UINT height,
    const char* handshakeName, const LUID* adapterLuid) {
    if (m_running.load() || width == 0 || height == 0)
        return false;

    if (m_thread.joinable())
        m_thread.join();

    m_renderObjectManager = renderer;
    m_inputWindow = inputWindow;
    m_width = width;
    m_height = height;
    ++m_generation;
    if (m_generation == 0)
        m_generation = 1;
    m_useRequiredAdapterLuid = adapterLuid != nullptr;
    m_requiredAdapterLuid = adapterLuid ? *adapterLuid : LUID{};
    m_targetPid = 0;
    if (inputWindow)
        GetWindowThreadProcessId(inputWindow, &m_targetPid);
    m_handshakeName.clear();
    if (handshakeName && handshakeName[0]) {
        m_handshakeName = handshakeName;
    }
    else if (m_targetPid != 0) {
        char generatedName[96]{};
        sprintf_s(generatedName, "Local\\LiquidOverlayHandshake_DX12_%lu",
            static_cast<unsigned long>(m_targetPid));
        m_handshakeName = generatedName;
    }
    m_ready.store(false);
    m_running.store(true);

    try {
        m_thread = std::thread(&HeadlessRendererDX12::RenderThread, this);
        m_scrollRunning.store(true);
        m_scrollThread = std::thread(&HeadlessRendererDX12::ScrollHookThread, this);
    }
    catch (...) {
        m_running.store(false);
        m_scrollRunning.store(false);
        if (m_thread.joinable())
            m_thread.join();
        if (m_scrollThread.joinable())
            m_scrollThread.join();
        return false;
    }
    return true;
}

void HeadlessRendererDX12::Stop() {
    m_running.store(false);
    m_scrollRunning.store(false);
    if (m_thread.joinable())
        m_thread.join();
    if (m_scrollThread.joinable())
        m_scrollThread.join();
    m_ready.store(false);
}

bool HeadlessRendererDX12::IsReady() const noexcept {
    return m_ready.load();
}

bool HeadlessRendererDX12::IsRunning() const noexcept {
    return m_running.load();
}

void HeadlessRendererDX12::RenderThread() {
    if (!CreateDeviceResources() || !CreateRenderTexture(m_width, m_height) ||
        (m_targetPid != 0 && !CreateSharedOverlay())) {
        Cleanup();
        m_running.store(false);
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_imguiContextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(m_width), static_cast<float>(m_height));
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    SetupLiquidStyle();
    ImGui::GetStyle().Alpha = 1.0f;

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = m_device.Get();
    initInfo.CommandQueue = m_commandQueue.Get();
    initInfo.NumFramesInFlight = 1;
    initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = m_srvHeap.Get();
    initInfo.SrvDescriptorAllocFn = &HeadlessRendererDX12::AllocateSrv;
    initInfo.SrvDescriptorFreeFn = &HeadlessRendererDX12::FreeSrv;
    if (!ImGui_ImplDX12_Init(&initInfo)) {
        std::printf("[HeadlessRendererDX12] ImGui_ImplDX12_Init failed\n");
        Cleanup();
        m_running.store(false);
        return;
    }
    m_imguiBackendInitialized = true;

    m_ready.store(true);
    std::printf("[HeadlessRendererDX12] Ready (%ux%u)\n", m_width, m_height);

    while (m_running.load()) {
        if (!RenderFrame()) {
            std::printf("[HeadlessRendererDX12] Frame submission failed\n");
            m_running.store(false);
            break;
        }
        Sleep(1);
    }

    m_ready.store(false);
    WaitForGpu();
    Cleanup();
    std::printf("[HeadlessRendererDX12] Stopped\n");
}

bool HeadlessRendererDX12::CreateDeviceResources() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        debugController->EnableDebugLayer();
#endif

    HRESULT hr = E_FAIL;
    if (m_useRequiredAdapterLuid) {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            std::printf("[HeadlessRendererDX12] CreateDXGIFactory1 failed: 0x%08X\n", hr);
            return false;
        }

        for (UINT adapterIndex = 0; ; ++adapterIndex) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            hr = factory->EnumAdapters1(adapterIndex, &adapter);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr)) {
                std::printf("[HeadlessRendererDX12] EnumAdapters1 failed: 0x%08X\n", hr);
                return false;
            }

            DXGI_ADAPTER_DESC1 adapterDesc{};
            hr = adapter->GetDesc1(&adapterDesc);
            if (FAILED(hr)) {
                std::printf("[HeadlessRendererDX12] IDXGIAdapter1::GetDesc1 failed: 0x%08X\n", hr);
                return false;
            }
            if (adapterDesc.AdapterLuid.LowPart == m_requiredAdapterLuid.LowPart &&
                adapterDesc.AdapterLuid.HighPart == m_requiredAdapterLuid.HighPart) {
                hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&m_device));
                break;
            }
        }
        if (!m_device) {
            std::printf("[HeadlessRendererDX12] No D3D12 adapter matched target LUID %08X:%08X (hr=0x%08X)\n",
                static_cast<unsigned int>(m_requiredAdapterLuid.HighPart),
                static_cast<unsigned int>(m_requiredAdapterLuid.LowPart),
                static_cast<unsigned int>(hr));
            return false;
        }
    }
    else {
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device));
    }
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] D3D12CreateDevice failed: 0x%08X\n", hr);
        return false;
    }
    const LUID selectedAdapterLuid = m_device->GetAdapterLuid();
    std::printf("[HeadlessRendererDX12] Selected adapter LUID: %08X:%08X\n",
        static_cast<unsigned int>(selectedAdapterLuid.HighPart),
        static_cast<unsigned int>(selectedAdapterLuid.LowPart));

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] CreateCommandQueue failed: 0x%08X\n", hr);
        return false;
    }

    hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_commandAllocator));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] CreateCommandAllocator failed: 0x%08X\n", hr);
        return false;
    }

    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] CreateCommandList failed: 0x%08X\n", hr);
        return false;
    }
    m_commandList->Close();

    const D3D12_FENCE_FLAGS fenceFlags = m_targetPid != 0
        ? D3D12_FENCE_FLAG_SHARED : D3D12_FENCE_FLAG_NONE;
    hr = m_device->CreateFence(0, fenceFlags, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] CreateFence failed: 0x%08X\n", hr);
        return false;
    }

    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        std::printf("[HeadlessRendererDX12] CreateEvent failed: %lu\n", GetLastError());
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    hr = m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] Create RTV heap failed: 0x%08X\n", hr);
        return false;
    }
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = SRV_DESCRIPTOR_CAPACITY;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = m_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] Create SRV heap failed: 0x%08X\n", hr);
        return false;
    }
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool HeadlessRendererDX12::CreateRenderTexture(UINT width, UINT height) {
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    const FLOAT clearColor[4] = { 0, 0, 0, 0 };
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resourceDesc.Format;
    memcpy(clearValue.Color, clearColor, sizeof(clearColor));

    const D3D12_HEAP_FLAGS heapFlags = m_targetPid != 0
        ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
    HRESULT hr = m_device->CreateCommittedResource(&heapProperties,
        heapFlags, &resourceDesc, D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue, IID_PPV_ARGS(&m_renderTexture));
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] Create overlay texture failed: 0x%08X\n", hr);
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = resourceDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    m_device->CreateRenderTargetView(m_renderTexture.Get(), &rtvDesc,
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool HeadlessRendererDX12::CreateSharedOverlay() {
    m_targetProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, m_targetPid);
    if (!m_targetProcess) {
        std::printf("[HeadlessRendererDX12] OpenProcess(%lu) for handle sharing failed: %lu\n",
            m_targetPid, GetLastError());
        return false;
    }

    HRESULT hr = m_device->CreateSharedHandle(m_renderTexture.Get(), nullptr,
        GENERIC_ALL, nullptr, &m_sharedResourceHandle);
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] CreateSharedHandle(resource) failed: 0x%08X\n", hr);
        return false;
    }
    hr = m_device->CreateSharedHandle(m_fence.Get(), nullptr,
        GENERIC_ALL, nullptr, &m_sharedFenceHandle);
    if (FAILED(hr)) {
        std::printf("[HeadlessRendererDX12] CreateSharedHandle(fence) failed: 0x%08X\n", hr);
        return false;
    }

    m_remoteResourceHandle = DuplicateHandleToTarget(m_sharedResourceHandle);
    m_remoteFenceHandle = DuplicateHandleToTarget(m_sharedFenceHandle);
    if (!m_remoteResourceHandle || !m_remoteFenceHandle) {
        std::printf("[HeadlessRendererDX12] Duplicating shared handles into target failed: %lu\n",
            GetLastError());
        return false;
    }

    m_handshakeMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(sizeof(DX12Overlay::Handshake)), m_handshakeName.c_str());
    if (!m_handshakeMapping) {
        std::printf("[HeadlessRendererDX12] CreateFileMapping failed: %lu\n", GetLastError());
        return false;
    }
    // The target process may retain a read-only view of this mapping so its
    // Present hook can detect renderer generations. Reuse that mapping during
    // recovery and publish the new handles/generation into the same view.
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        std::printf("[HeadlessRendererDX12] Reusing handshake mapping for recovery: %s\n",
            m_handshakeName.c_str());

    m_handshake = static_cast<DX12Overlay::Handshake*>(MapViewOfFile(m_handshakeMapping,
        FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DX12Overlay::Handshake)));
    if (!m_handshake) {
        std::printf("[HeadlessRendererDX12] MapViewOfFile failed: %lu\n", GetLastError());
        return false;
    }

    std::memset(m_handshake, 0, sizeof(*m_handshake));
    m_handshake->magic = DX12Overlay::HANDSHAKE_MAGIC;
    m_handshake->version = DX12Overlay::HANDSHAKE_VERSION;
    m_handshake->headerSize = static_cast<std::uint16_t>(sizeof(*m_handshake));
    m_handshake->api = DX12Overlay::API_DIRECT3D12;
    m_handshake->width = m_width;
    m_handshake->height = m_height;
    m_handshake->format = static_cast<std::uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM);
    m_handshake->producerPid = GetCurrentProcessId();
    m_handshake->targetPid = m_targetPid;
    const LUID adapterLuid = m_device->GetAdapterLuid();
    m_handshake->adapterLuidLow = adapterLuid.LowPart;
    m_handshake->adapterLuidHigh = static_cast<std::uint32_t>(adapterLuid.HighPart);
    m_handshake->generation = m_generation;
    m_handshake->resourceHandle = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(m_remoteResourceHandle));
    m_handshake->fenceHandle = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(m_remoteFenceHandle));
    m_sharedEnabled = true;
    PublishState(static_cast<DWORD>(DX12Overlay::HandshakeState::Ready));

    std::printf("[HeadlessRendererDX12] Published v%u shared overlay for pid %lu as %s\n",
        DX12Overlay::HANDSHAKE_VERSION, m_targetPid, m_handshakeName.c_str());
    return true;
}

bool HeadlessRendererDX12::RenderFrame() {
    if (!WaitForGpu())
        return false;
    if (m_sharedEnabled && m_frameIndex != 0 &&
        !WaitForFenceValue(DX12Overlay::ConsumerDoneValue(m_frameIndex - 1), 100))
        return true;

    HRESULT hr = m_commandAllocator->Reset();
    if (FAILED(hr))
        return false;
    hr = m_commandList->Reset(m_commandAllocator.Get(), nullptr);
    if (FAILED(hr))
        return false;

    ImGuiIO& io = ImGui::GetIO();
    FeedInput();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    if (m_renderObjectManager) {
        const bool mouseReleased = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0;
        RenderLiquidMenu();
        m_renderObjectManager->Render(io.DisplaySize, io.MousePos, mouseReleased);
    }
    ImGui::Render();

    if (m_sharedEnabled && m_frameIndex != 0) {
        D3D12_RESOURCE_BARRIER toRenderTarget{};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = m_renderTexture.Get();
        toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_commandList->ResourceBarrier(1, &toRenderTarget);
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const FLOAT clearColor[4] = { 0, 0, 0, 0 };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    m_commandList->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    if (m_sharedEnabled) {
        D3D12_RESOURCE_BARRIER toShaderResource{};
        toShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toShaderResource.Transition.pResource = m_renderTexture.Get();
        toShaderResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_commandList->ResourceBarrier(1, &toShaderResource);
    }

    hr = m_commandList->Close();
    if (FAILED(hr))
        return false;
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    m_lastFenceValue = m_sharedEnabled
        ? DX12Overlay::ProducerReadyValue(m_frameIndex)
        : m_nextFenceValue++;
    hr = m_commandQueue->Signal(m_fence.Get(), m_lastFenceValue);
    if (SUCCEEDED(hr) && m_handshake) {
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&m_handshake->producerFenceValue),
            static_cast<LONG64>(m_lastFenceValue));
    }
    if (SUCCEEDED(hr))
        ++m_frameIndex;
    return SUCCEEDED(hr);
}

bool HeadlessRendererDX12::WaitForGpu() {
    if (!m_fence || m_lastFenceValue == 0)
        return true;
    for (;;) {
        const UINT64 completedValue = m_fence->GetCompletedValue();
        if (completedValue == UINT64_MAX) {
            std::printf("[HeadlessRendererDX12] Producer fence reports device removed: completed=%llu last=%llu frame=%llu reason=0x%08X\n",
                static_cast<unsigned long long>(completedValue),
                static_cast<unsigned long long>(m_lastFenceValue),
                static_cast<unsigned long long>(m_frameIndex),
                static_cast<unsigned int>(m_device ? m_device->GetDeviceRemovedReason() : E_POINTER));
            return false;
        }
        if (completedValue >= m_lastFenceValue)
            return true;
        // Stop() must be able to interrupt a fence wait if the target/device
        // stalls while the renderer is being recovered.
        if (!m_running.load()) {
            std::printf("[HeadlessRendererDX12] Producer fence wait interrupted during shutdown: completed=%llu last=%llu frame=%llu\n",
                static_cast<unsigned long long>(completedValue),
                static_cast<unsigned long long>(m_lastFenceValue),
                static_cast<unsigned long long>(m_frameIndex));
            return false;
        }
        const HRESULT hr = m_fence->SetEventOnCompletion(m_lastFenceValue, m_fenceEvent);
        if (FAILED(hr)) {
            std::printf("[HeadlessRendererDX12] SetEventOnCompletion failed while waiting for producer fence: 0x%08X\n",
                static_cast<unsigned int>(hr));
            return false;
        }
        const DWORD waitResult = WaitForSingleObject(m_fenceEvent, 100);
        if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_TIMEOUT) {
            std::printf("[HeadlessRendererDX12] Producer fence event wait failed: %lu\n", GetLastError());
            return false;
        }
    }
}

bool HeadlessRendererDX12::WaitForFenceValue(UINT64 value, DWORD timeoutMs) {
    if (!m_fence || m_fence->GetCompletedValue() >= value)
        return true;

    HRESULT hr = m_fence->SetEventOnCompletion(value, m_fenceEvent);
    if (FAILED(hr))
        return false;
    const DWORD result = WaitForSingleObject(m_fenceEvent, timeoutMs);
    return result == WAIT_OBJECT_0;
}

void HeadlessRendererDX12::Cleanup() {
    if (m_handshake) {
        PublishState(static_cast<DWORD>(DX12Overlay::HandshakeState::Stopping));
        if (m_sharedEnabled && m_frameIndex != 0 &&
            !WaitForFenceValue(DX12Overlay::ConsumerDoneValue(m_frameIndex - 1), 1000)) {
            std::printf("[HeadlessRendererDX12] Consumer did not acknowledge final frame before shutdown\n");
        }
        PublishState(static_cast<DWORD>(DX12Overlay::HandshakeState::Closed));
    }

    if (m_imguiBackendInitialized) {
        ImGui_ImplDX12_Shutdown();
        m_imguiBackendInitialized = false;
    }
    if (m_imguiContextCreated) {
        ImGui::DestroyContext();
        m_imguiContextCreated = false;
    }

    CloseTargetHandle(static_cast<UINT64>(reinterpret_cast<std::uintptr_t>(m_remoteResourceHandle)));
    CloseTargetHandle(static_cast<UINT64>(reinterpret_cast<std::uintptr_t>(m_remoteFenceHandle)));
    m_remoteResourceHandle = nullptr;
    m_remoteFenceHandle = nullptr;
    if (m_sharedResourceHandle) {
        CloseHandle(m_sharedResourceHandle);
        m_sharedResourceHandle = nullptr;
    }
    if (m_sharedFenceHandle) {
        CloseHandle(m_sharedFenceHandle);
        m_sharedFenceHandle = nullptr;
    }
    if (m_handshake) {
        UnmapViewOfFile(m_handshake);
        m_handshake = nullptr;
    }
    if (m_handshakeMapping) {
        CloseHandle(m_handshakeMapping);
        m_handshakeMapping = nullptr;
    }

    m_renderTexture.Reset();
    m_srvHeap.Reset();
    m_rtvHeap.Reset();
    m_fence.Reset();
    m_commandList.Reset();
    m_commandAllocator.Reset();
    m_commandQueue.Reset();
    m_device.Reset();
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    if (m_targetProcess) {
        CloseHandle(m_targetProcess);
        m_targetProcess = nullptr;
    }
    m_freeSrvDescriptors.clear();
    m_nextSrvDescriptor = 0;
    m_lastFenceValue = 0;
    m_nextFenceValue = 1;
    m_frameIndex = 0;
    m_sharedEnabled = false;
}

HANDLE HeadlessRendererDX12::DuplicateHandleToTarget(HANDLE sourceHandle) {
    HANDLE targetHandle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), sourceHandle, m_targetProcess,
        &targetHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return nullptr;
    return targetHandle;
}

void HeadlessRendererDX12::CloseTargetHandle(UINT64 remoteHandle) {
    if (!m_targetProcess || remoteHandle == 0)
        return;
    HANDLE localDuplicate = nullptr;
    const HANDLE targetHandle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(remoteHandle));
    if (DuplicateHandle(m_targetProcess, targetHandle, GetCurrentProcess(), &localDuplicate,
        0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE) && localDuplicate) {
        CloseHandle(localDuplicate);
    }
}

void HeadlessRendererDX12::PublishState(DWORD state) {
    if (m_handshake)
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&m_handshake->state),
            static_cast<LONG>(state));
}

void HeadlessRendererDX12::FeedInput() {
    ImGuiIO& io = ImGui::GetIO();
    POINT cursor{};
    if (GetCursorPos(&cursor) && m_inputWindow && ScreenToClient(m_inputWindow, &cursor)) {
        cursor.x = (std::max)(0L, (std::min)(cursor.x, static_cast<LONG>(m_width - 1)));
        cursor.y = (std::max)(0L, (std::min)(cursor.y, static_cast<LONG>(m_height - 1)));
    }
    io.MousePos = ImVec2(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    const LONG wheelDelta = s_wheelAccum.exchange(0);
    if (wheelDelta != 0)
        io.MouseWheel += static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA);

    static ULONGLONG lastTick = GetTickCount64();
    const ULONGLONG now = GetTickCount64();
    io.DeltaTime = static_cast<float>(now - lastTick) / 1000.0f;
    if (io.DeltaTime <= 0.0f)
        io.DeltaTime = 1.0f / 1000.0f;
    lastTick = now;
}

void HeadlessRendererDX12::ScrollHookThread() {
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, &HeadlessRendererDX12::LowLevelMouseProc,
        nullptr, 0);
    if (!s_mouseHook) {
        std::printf("[HeadlessRendererDX12] WH_MOUSE_LL install failed: %lu\n", GetLastError());
        return;
    }

    MSG message{};
    while (m_scrollRunning.load()) {
        MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_ALERTABLE);
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    UnhookWindowsHookEx(s_mouseHook);
    s_mouseHook = nullptr;
}

LRESULT CALLBACK HeadlessRendererDX12::LowLevelMouseProc(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && message == WM_MOUSEWHEEL) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        const SHORT delta = static_cast<SHORT>(HIWORD(mouse->mouseData));
        s_wheelAccum.fetch_add(static_cast<LONG>(delta));
    }
    return CallNextHookEx(s_mouseHook, code, message, data);
}

void HeadlessRendererDX12::AllocateSrv(ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    auto* self = static_cast<HeadlessRendererDX12*>(info->UserData);
    std::lock_guard<std::mutex> lock(self->m_srvMutex);
    UINT index;
    if (!self->m_freeSrvDescriptors.empty()) {
        index = self->m_freeSrvDescriptors.back();
        self->m_freeSrvDescriptors.pop_back();
    }
    else {
        index = self->m_nextSrvDescriptor++;
    }

    const auto cpuStart = self->m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    const auto gpuStart = self->m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    cpu->ptr = cpuStart.ptr + static_cast<SIZE_T>(index) * self->m_srvDescriptorSize;
    gpu->ptr = gpuStart.ptr + static_cast<UINT64>(index) * self->m_srvDescriptorSize;
}

void HeadlessRendererDX12::FreeSrv(ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    auto* self = static_cast<HeadlessRendererDX12*>(info->UserData);
    std::lock_guard<std::mutex> lock(self->m_srvMutex);
    const auto start = self->m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    if (cpu.ptr < start.ptr)
        return;
    const SIZE_T offset = cpu.ptr - start.ptr;
    if (offset % self->m_srvDescriptorSize != 0)
        return;
    const UINT index = static_cast<UINT>(offset / self->m_srvDescriptorSize);
    if (index < self->m_nextSrvDescriptor)
        self->m_freeSrvDescriptors.push_back(index);
}

