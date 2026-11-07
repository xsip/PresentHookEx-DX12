#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <HeadlessRenderer/Dx12OverlayProtocol.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

class RenderObjectManager;
struct ImGui_ImplDX12_InitInfo;

// Standalone D3D12 producer. It renders ImGui into an off-screen BGRA texture;
// sharing and consumer synchronization are added in a later porting step.
class HeadlessRendererDX12 {
public:
    HeadlessRendererDX12() = default;
    ~HeadlessRendererDX12();

    HeadlessRendererDX12(const HeadlessRendererDX12&) = delete;
    HeadlessRendererDX12& operator=(const HeadlessRendererDX12&) = delete;

    bool Start(RenderObjectManager* renderer, HWND inputWindow, UINT width, UINT height,
        const char* handshakeName = nullptr, const LUID* adapterLuid = nullptr);
    void Stop();
    bool IsReady() const noexcept;
    bool IsRunning() const noexcept;

    // Borrowed local-process resource, valid until Stop().
    ID3D12Resource* GetRenderTexture() const noexcept { return m_renderTexture.Get(); }

private:
    void RenderThread();
    bool CreateDeviceResources();
    bool CreateRenderTexture(UINT width, UINT height);
    bool CreateSharedOverlay();
    bool RenderFrame();
    bool WaitForGpu();
    bool WaitForFenceValue(UINT64 value, DWORD timeoutMs);
    HANDLE DuplicateHandleToTarget(HANDLE sourceHandle);
    void CloseTargetHandle(UINT64 remoteHandle);
    void PublishState(DWORD state);
    void Cleanup();
    void FeedInput();
    void ScrollHookThread();
    static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM message, LPARAM data);
    static void AllocateSrv(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    static void FreeSrv(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

    RenderObjectManager* m_renderObjectManager = nullptr;
    HWND m_inputWindow = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    DWORD m_targetPid = 0;
    LUID m_requiredAdapterLuid{};
    bool m_useRequiredAdapterLuid = false;
    std::string m_handshakeName;
    std::uint64_t m_generation = 1;
    std::uint64_t m_frameIndex = 0;
    bool m_sharedEnabled = false;
    std::atomic_bool m_running = false;
    std::atomic_bool m_ready = false;
    std::atomic_bool m_scrollRunning = false;
    std::thread m_thread;
    std::thread m_scrollThread;
    inline static HHOOK s_mouseHook = nullptr;
    inline static std::atomic<LONG> s_wheelAccum = 0;

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTexture;
    HANDLE m_sharedResourceHandle = nullptr;
    HANDLE m_sharedFenceHandle = nullptr;
    HANDLE m_targetProcess = nullptr;
    HANDLE m_remoteResourceHandle = nullptr;
    HANDLE m_remoteFenceHandle = nullptr;
    HANDLE m_handshakeMapping = nullptr;
    DX12Overlay::Handshake* m_handshake = nullptr;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_nextFenceValue = 1;
    UINT64 m_lastFenceValue = 0;
    UINT m_rtvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    UINT m_nextSrvDescriptor = 0;
    std::vector<UINT> m_freeSrvDescriptors;
    std::mutex m_srvMutex;
    bool m_imguiContextCreated = false;
    bool m_imguiBackendInitialized = false;
};

