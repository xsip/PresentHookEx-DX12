#include <Example/Include.h>
#include <Example/Dx12GraphicsProvider.h>
#include <Windows.h>
#include <LiquidHookEx/Include.h>
#include <LiquidHookEx/Macros.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>

namespace DX12 {
    using LiquidHookEx::proc;
    namespace {
        struct RemoteGraphicsContext {
            GraphicsContext graphics{};
            HWND dummyWindow = nullptr;
            IDXGIFactory4* factory = nullptr;
            IDXGISwapChain1* swapChain1 = nullptr;
            D3D12_COMMAND_QUEUE_DESC queueDesc{};
            DXGI_SWAP_CHAIN_DESC1 swapDesc{};
            using CreateWindowExFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD,
                int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
            using DestroyWindowFn = BOOL(WINAPI*)(HWND);
            using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL,
                REFIID, void**);
            using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
            CreateWindowExFn createWindowEx = nullptr;
            DestroyWindowFn destroyWindow = nullptr;
            D3D12CreateDeviceFn d3d12CreateDevice = nullptr;
            CreateDXGIFactory2Fn createFactory = nullptr;
            WCHAR windowClassName[16]{};
            WCHAR windowTitle[64]{};
            IID iidDevice{};
            IID iidCommandQueue{};
            IID iidFactory{};
            IID iidSwapChain3{};
        };

        static_assert(std::is_trivially_copyable_v<RemoteGraphicsContext>);
        static uintptr_t g_remoteGraphicsContext = 0;
        static bool g_remoteAcquireInProgress = false;
        static HANDLE g_pendingAcquireThread = nullptr;
        static void* g_pendingAcquireShellcode = nullptr;
        static HANDLE g_pendingReleaseThread = nullptr;
        static void* g_pendingReleaseShellcode = nullptr;
    }

    LH_START(".pDX12Obtain")
        DWORD WINAPI DX12GraphicsObtainerThread(LPVOID parameter) {
        if (!parameter)
            return 1;
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_PENDING;
        if (!reinterpret_cast<RemoteGraphicsContext*>(parameter)->createWindowEx ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->destroyWindow ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->d3d12CreateDevice ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->createFactory) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_INVALIDARG;
            return 1;
        }
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->dummyWindow =
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->createWindowEx(
                0, reinterpret_cast<RemoteGraphicsContext*>(parameter)->windowClassName,
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->windowTitle,
                WS_OVERLAPPED, 0, 0, 160, 90, nullptr, nullptr, nullptr, nullptr);
        if (!reinterpret_cast<RemoteGraphicsContext*>(parameter)->dummyWindow) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_FAIL;
            return 1;
        }

        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status =
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->createFactory(
                0, reinterpret_cast<RemoteGraphicsContext*>(parameter)->iidFactory,
                reinterpret_cast<void**>(&reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory));
        if (FAILED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status) ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory) {
            if (SUCCEEDED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status))
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_FAIL;
            return 1;
        }
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status =
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->d3d12CreateDevice(
                nullptr, D3D_FEATURE_LEVEL_11_0,
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->iidDevice,
                reinterpret_cast<void**>(&reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.device));
        if (FAILED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status) ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.device) {
            if (SUCCEEDED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status))
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_FAIL;
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory->Release();
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory = nullptr;
            return 1;
        }

        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status =
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.device->CreateCommandQueue(
                &reinterpret_cast<RemoteGraphicsContext*>(parameter)->queueDesc,
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->iidCommandQueue,
                reinterpret_cast<void**>(&reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.commandQueue));
        if (FAILED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status) ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.commandQueue) {
            if (SUCCEEDED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status))
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_FAIL;
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory->Release();
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory = nullptr;
            return 1;
        }

        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status =
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory->CreateSwapChainForHwnd(
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.commandQueue,
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->dummyWindow,
                &reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapDesc,
                nullptr, nullptr,
                &reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapChain1);
        if (SUCCEEDED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status) &&
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapChain1) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status =
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapChain1->QueryInterface(
                    reinterpret_cast<RemoteGraphicsContext*>(parameter)->iidSwapChain3,
                    reinterpret_cast<void**>(&reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.swapChain));
        }
        if (reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapChain1) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapChain1->Release();
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->swapChain1 = nullptr;
        }
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory->Release();
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->factory = nullptr;
        if (FAILED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status) ||
            !reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.swapChain) {
            if (SUCCEEDED(reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status))
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = E_FAIL;
            return 1;
        }
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = S_OK;
        return 0;
    }
    DWORD WINAPI DX12GraphicsObtainerThreadEnd() { return 0; }
    LH_END()

        LH_START(".pDX12Release")
        DWORD WINAPI DX12GraphicsReleaserThread(LPVOID parameter) {
        if (!parameter)
            return 1;
        if (reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.swapChain) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.swapChain->Release();
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.swapChain = nullptr;
        }
        if (reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.commandQueue) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.commandQueue->Release();
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.commandQueue = nullptr;
        }
        if (reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.device) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.device->Release();
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.device = nullptr;
        }
        if (reinterpret_cast<RemoteGraphicsContext*>(parameter)->dummyWindow &&
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->destroyWindow) {
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->destroyWindow(
                reinterpret_cast<RemoteGraphicsContext*>(parameter)->dummyWindow);
            reinterpret_cast<RemoteGraphicsContext*>(parameter)->dummyWindow = nullptr;
        }
        reinterpret_cast<RemoteGraphicsContext*>(parameter)->graphics.status = S_OK;
        return 0;
    }
    DWORD WINAPI DX12GraphicsReleaserThreadEnd() { return 0; }
    LH_END()

        HRESULT AcquireGraphicsContext(GraphicsContext& context) {
        context = {};
        if (!proc || !proc->IsTarget64())
            return context.status = DXGI_ERROR_UNSUPPORTED;
        if (g_remoteGraphicsContext || g_remoteAcquireInProgress)
            return context.status = HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);


        const HWND targetWindow = proc->GetHwnd();
        if (!targetWindow)
            return context.status = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        DWORD visibilityWaitMs = 0;
        while (IsWindow(targetWindow) && !IsWindowVisible(targetWindow) && visibilityWaitMs < 15000) {
            Sleep(10);
            visibilityWaitMs += 10;
        }
        if (!IsWindow(targetWindow) || !IsWindowVisible(targetWindow))
            return context.status = HRESULT_FROM_WIN32(ERROR_TIMEOUT);

        g_remoteAcquireInProgress = true;
        auto targetD3d12 = proc->GetRemoteModule("d3d12.dll", false);
        auto targetDxgi = proc->GetRemoteModule("dxgi.dll", false);
        auto targetUser32 = proc->GetRemoteModule("user32.dll", false);
        if (!targetD3d12 || !targetDxgi || !targetUser32) {
            g_remoteAcquireInProgress = false;
            return context.status = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        }

        RemoteGraphicsContext remote{};
        remote.createWindowEx = reinterpret_cast<RemoteGraphicsContext::CreateWindowExFn>(
            targetUser32->GetProcAddress("CreateWindowExW"));
        remote.destroyWindow = reinterpret_cast<RemoteGraphicsContext::DestroyWindowFn>(
            targetUser32->GetProcAddress("DestroyWindow"));
        remote.d3d12CreateDevice = reinterpret_cast<RemoteGraphicsContext::D3D12CreateDeviceFn>(
            targetD3d12->GetProcAddress("D3D12CreateDevice"));
        remote.createFactory = reinterpret_cast<RemoteGraphicsContext::CreateDXGIFactory2Fn>(
            targetDxgi->GetProcAddress("CreateDXGIFactory2"));
        wcscpy_s(remote.windowClassName, L"STATIC");
        wcscpy_s(remote.windowTitle, L"DX12 SwapChainObtainer");
        remote.iidDevice = __uuidof(ID3D12Device);
        remote.iidCommandQueue = __uuidof(ID3D12CommandQueue);
        remote.iidFactory = __uuidof(IDXGIFactory4);
        remote.iidSwapChain3 = __uuidof(IDXGISwapChain3);
        remote.queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        remote.queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        remote.queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        remote.queueDesc.NodeMask = 0;
        remote.swapDesc.Width = 160;
        remote.swapDesc.Height = 90;
        remote.swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        remote.swapDesc.SampleDesc.Count = 1;
        remote.swapDesc.SampleDesc.Quality = 0;
        remote.swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        remote.swapDesc.BufferCount = 2;
        remote.swapDesc.Scaling = DXGI_SCALING_STRETCH;
        remote.swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        remote.swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        remote.swapDesc.Flags = 0;
        if (!remote.createWindowEx || !remote.destroyWindow ||
            !remote.d3d12CreateDevice || !remote.createFactory) {
            g_remoteAcquireInProgress = false;
            return context.status = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        }

        const uintptr_t remoteAddress = reinterpret_cast<uintptr_t>(proc->Alloc(sizeof(remote)));
        if (!remoteAddress || !proc->Write<RemoteGraphicsContext>(remoteAddress, remote)) {
            if (remoteAddress)
                proc->FreeRemote(reinterpret_cast<void*>(remoteAddress));
            g_remoteAcquireInProgress = false;
            return context.status = E_OUTOFMEMORY;
        }
        void* shellcode = proc->AllocAndWriteShellcode(DX12GraphicsObtainerThread,
            DX12GraphicsObtainerThreadEnd);
        if (!shellcode) {
            proc->FreeRemote(reinterpret_cast<void*>(remoteAddress));
            g_remoteAcquireInProgress = false;
            return context.status = E_OUTOFMEMORY;
        }
        HANDLE thread = proc->CreateRemoteThreadEx(
            reinterpret_cast<LPTHREAD_START_ROUTINE>(shellcode),
            reinterpret_cast<void*>(remoteAddress));
        if (!thread) {
            proc->FreeRemote(shellcode);
            proc->FreeRemote(reinterpret_cast<void*>(remoteAddress));
            g_remoteAcquireInProgress = false;
            return context.status = HRESULT_FROM_WIN32(GetLastError());
        }
        const DWORD wait = WaitForSingleObject(thread, 15000);
        DWORD exitCode = 1;
        if (wait == WAIT_OBJECT_0)
            GetExitCodeThread(thread, &exitCode);
        if (wait != WAIT_OBJECT_0) {


            g_pendingAcquireThread = thread;
            g_pendingAcquireShellcode = shellcode;
            g_remoteGraphicsContext = remoteAddress;
            g_remoteAcquireInProgress = false;
            return context.status = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        CloseHandle(thread);
        proc->FreeRemote(shellcode);

        RemoteGraphicsContext completed{};
        if (!proc->Read<RemoteGraphicsContext>(remoteAddress, &completed, sizeof(completed))) {
            g_remoteGraphicsContext = remoteAddress;
            g_remoteAcquireInProgress = false;
            return context.status = E_FAIL;
        }
        g_remoteGraphicsContext = remoteAddress;
        context = completed.graphics;
        g_remoteAcquireInProgress = false;
        if (exitCode != 0 || !context.IsComplete()) {
            const HRESULT result = FAILED(context.status) ? context.status : E_FAIL;
            ReleaseGraphicsContext(context);
            context.status = result;
            return result;
        }
        return S_OK;
    }

    void ReleaseGraphicsContext(GraphicsContext& context) {
        if (g_pendingAcquireThread) {
            const DWORD acquireWait = WaitForSingleObject(g_pendingAcquireThread, 15000);
            if (acquireWait != WAIT_OBJECT_0)
                return;
            CloseHandle(g_pendingAcquireThread);
            g_pendingAcquireThread = nullptr;
            if (g_pendingAcquireShellcode) {
                proc->FreeRemote(g_pendingAcquireShellcode);
                g_pendingAcquireShellcode = nullptr;
            }
        }
        if (g_pendingReleaseThread) {
            const DWORD releaseWait = WaitForSingleObject(g_pendingReleaseThread, 15000);
            if (releaseWait != WAIT_OBJECT_0)
                return;
            CloseHandle(g_pendingReleaseThread);
            g_pendingReleaseThread = nullptr;
            if (g_pendingReleaseShellcode) {
                proc->FreeRemote(g_pendingReleaseShellcode);
                g_pendingReleaseShellcode = nullptr;
            }
            if (g_remoteGraphicsContext)
                proc->FreeRemote(reinterpret_cast<void*>(g_remoteGraphicsContext));
            g_remoteGraphicsContext = 0;
            g_remoteAcquireInProgress = false;
            context = {};
            context.status = S_OK;
            return;
        }
        if (!g_remoteGraphicsContext) {
            context = {};
            context.status = S_FALSE;
            return;
        }
        void* shellcode = proc->AllocAndWriteShellcode(DX12GraphicsReleaserThread,
            DX12GraphicsReleaserThreadEnd);
        if (!shellcode)
            return;
        HANDLE thread = proc->CreateRemoteThreadEx(
            reinterpret_cast<LPTHREAD_START_ROUTINE>(shellcode),
            reinterpret_cast<void*>(g_remoteGraphicsContext));
        if (!thread) {
            proc->FreeRemote(shellcode);
            return;
        }
        const DWORD wait = WaitForSingleObject(thread, 10000);
        if (wait != WAIT_OBJECT_0) {
            g_pendingReleaseThread = thread;
            g_pendingReleaseShellcode = shellcode;
            return;
        }
        CloseHandle(thread);
        proc->FreeRemote(shellcode);
        proc->FreeRemote(reinterpret_cast<void*>(g_remoteGraphicsContext));
        g_remoteGraphicsContext = 0;
        g_remoteAcquireInProgress = false;
        context = {};
        context.status = S_OK;
    }
}