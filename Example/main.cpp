#include <Example/Include.h>
#include <Example/LiquidPresentDx12.h>
#include <HeadlessRenderer/HeadlessRenderer.h>
#include <cstring>

std::string szExe = R"(PROCESS.exe)";


int main() {
	{
        SetConsoleTitle("PresentHookEx - External DX12 Fullscreen hook");
        LiquidHookEx::INIT(szExe);
        DX12::RegisterGraphicsProvider({
            &DX12::AcquireGraphicsContext,
            &DX12::ReleaseGraphicsContext,
            });
        const HWND targetHwnd = LiquidHookEx::proc->GetHwnd();
        if (!targetHwnd) {
            printf("Couldn't find a target window for the DX12 shared overlay.\n");
            return -1;
        }

        if (!DX12::g_graphicsProvider.acquire || !DX12::g_graphicsProvider.release) {
            printf("DX12 remote acquisition is not installed. Implement the provider registration in SwapChainObtainer.\n");
            return -1;
        }

        DX12::GraphicsContext graphicsContext{};
        const HRESULT acquireResult = DX12::g_graphicsProvider.acquire(graphicsContext);
        printf("SwapChain: 0x%p\n", graphicsContext.device);
        printf("IsComplete: %i\n", graphicsContext.IsComplete());
        printf("Status: %i\n", graphicsContext.status);
        if (FAILED(acquireResult) || !graphicsContext.IsComplete()) {
            printf("DX12 remote acquisition failed: 0x%08X\n", acquireResult);
            DX12::g_graphicsProvider.release(graphicsContext);
            return -1;
        }

        UINT overlayWidth = 1920;
        UINT overlayHeight = 1080;
        RECT clientRect{};
        if (GetClientRect(targetHwnd, &clientRect) && clientRect.right > 0 && clientRect.bottom > 0) {
            overlayWidth = static_cast<UINT>(clientRect.right);
            overlayHeight = static_cast<UINT>(clientRect.bottom);
        }

        RenderObjectManager renderObjectManager{ {} };
        HeadlessRendererDX12 renderer;
        if (!LiquidHookEx::PresentDx12::Hook(graphicsContext)) {
            printf("Couldn't install DX12 Present hook.\n");
            DX12::g_graphicsProvider.release(graphicsContext);
            return -1;
        }

        LUID targetAdapterLuid{};
        bool targetAdapterReady = false;
        const uintptr_t hookDataAddress = reinterpret_cast<uintptr_t>(
            LiquidHookEx::PresentDx12::m_Hook.GetDataRemote());
        printf("Waiting for the hooked target Present to report its adapter...\n");
        for (UINT attempt = 0; attempt < 15000; ++attempt) {
            if (LiquidHookEx::proc->ReadDirect<LONG>(hookDataAddress +
                offsetof(LiquidHookEx::Dx12PresentHookData, adapterLuidReady)) != 0) {
                targetAdapterLuid = LiquidHookEx::proc->ReadDirect<LUID>(hookDataAddress +
                    offsetof(LiquidHookEx::Dx12PresentHookData, adapterLuid));
                targetAdapterReady = true;
                break;
            }
            if (attempt != 0 && attempt % 3000 == 0)
                printf("Still waiting for the target adapter (%u seconds).\n", attempt / 1000);
            Sleep(1);
        }
        if (!targetAdapterReady) {
            printf("Timed out waiting for the target DX12 adapter LUID.\n");
            LiquidHookEx::PresentDx12::Unhook();
            DX12::g_graphicsProvider.release(graphicsContext);
            return -1;
        }
        printf("Target DX12 adapter LUID: %08X:%08X\n",
            static_cast<unsigned int>(targetAdapterLuid.HighPart),
            static_cast<unsigned int>(targetAdapterLuid.LowPart));

        if (!renderer.Start(&renderObjectManager, targetHwnd, overlayWidth, overlayHeight,
            nullptr, &targetAdapterLuid)) {
            printf("Couldn't start the DX12 renderer on the target adapter.\n");
            LiquidHookEx::PresentDx12::Unhook();
            DX12::g_graphicsProvider.release(graphicsContext);
            return -1;
        }
        while (renderer.IsRunning() && !renderer.IsReady())
            Sleep(1);
        if (!renderer.IsReady()) {
            printf("DX12 renderer initialization failed.\n");
            renderer.Stop();
            LiquidHookEx::PresentDx12::Unhook();
            DX12::g_graphicsProvider.release(graphicsContext);
            return -1;
        }

        printf("DX12 overlay hook is running. Press Delete to stop.\n");
        UINT64 observedFrameIndex = LiquidHookEx::proc->ReadDirect<UINT64>(hookDataAddress +
            offsetof(LiquidHookEx::Dx12PresentHookData, frameIndex));
        ULONGLONG lastFrameProgressTick = GetTickCount64();
        bool stallRecoveryAttempted = false;
        while (renderer.IsRunning() && !GetAsyncKeyState(VK_DELETE)) {
            const LUID currentTargetAdapterLuid = LiquidHookEx::proc->ReadDirect<LUID>(hookDataAddress +
                offsetof(LiquidHookEx::Dx12PresentHookData, adapterLuid));
            if (LiquidHookEx::proc->ReadDirect<LONG>(hookDataAddress +
                offsetof(LiquidHookEx::Dx12PresentHookData, adapterLuidReady)) != 0 &&
                (currentTargetAdapterLuid.LowPart != targetAdapterLuid.LowPart ||
                    currentTargetAdapterLuid.HighPart != targetAdapterLuid.HighPart)) {
                printf("Target DX12 adapter changed to %08X:%08X; restarting shared renderer.\n",
                    static_cast<unsigned int>(currentTargetAdapterLuid.HighPart),
                    static_cast<unsigned int>(currentTargetAdapterLuid.LowPart));
                renderer.Stop();

                for (UINT attempt = 0; attempt < 5000; ++attempt) {
                    if (LiquidHookEx::proc->ReadDirect<LONG>(hookDataAddress +
                        offsetof(LiquidHookEx::Dx12PresentHookData, hookBusy)) == 0)
                        break;
                    Sleep(1);
                }
                if (LiquidHookEx::proc->ReadDirect<LONG>(hookDataAddress +
                    offsetof(LiquidHookEx::Dx12PresentHookData, hookBusy)) != 0) {
                    printf("Timed out waiting for the DX12 Present callback before adapter restart.\n");
                    break;
                }

                targetAdapterLuid = LiquidHookEx::proc->ReadDirect<LUID>(hookDataAddress +
                    offsetof(LiquidHookEx::Dx12PresentHookData, adapterLuid));
                if (!renderer.Start(&renderObjectManager, targetHwnd, overlayWidth, overlayHeight,
                    nullptr, &targetAdapterLuid)) {
                    printf("Couldn't restart the DX12 renderer on the changed target adapter.\n");
                    break;
                }
                while (renderer.IsRunning() && !renderer.IsReady())
                    Sleep(1);
                if (!renderer.IsReady()) {
                    printf("DX12 renderer failed to restart on the changed target adapter.\n");
                    break;
                }
                printf("[HeadlessRendererDX12] Restarted on target adapter %08X:%08X\n",
                    static_cast<unsigned int>(targetAdapterLuid.HighPart),
                    static_cast<unsigned int>(targetAdapterLuid.LowPart));
                observedFrameIndex = LiquidHookEx::proc->ReadDirect<UINT64>(hookDataAddress +
                    offsetof(LiquidHookEx::Dx12PresentHookData, frameIndex));
                lastFrameProgressTick = GetTickCount64();
                stallRecoveryAttempted = false;
            }

            const UINT64 currentFrameIndex = LiquidHookEx::proc->ReadDirect<UINT64>(hookDataAddress +
                offsetof(LiquidHookEx::Dx12PresentHookData, frameIndex));
            if (currentFrameIndex != observedFrameIndex) {
                observedFrameIndex = currentFrameIndex;
                lastFrameProgressTick = GetTickCount64();
                stallRecoveryAttempted = false;
            }
            else if (!stallRecoveryAttempted && GetTickCount64() - lastFrameProgressTick >= 2000) {
                stallRecoveryAttempted = true;
                printf("[HeadlessRendererDX12] No overlay frame progress for 2 seconds; restarting the shared renderer.\n");
                printf("[HeadlessRendererDX12] Stopping the stalled renderer...\n");
                renderer.Stop();
                printf("[HeadlessRendererDX12] Stalled renderer stopped.\n");

                for (UINT attempt = 0; attempt < 5000; ++attempt) {
                    if (LiquidHookEx::proc->ReadDirect<LONG>(hookDataAddress +
                        offsetof(LiquidHookEx::Dx12PresentHookData, hookBusy)) == 0)
                        break;
                    Sleep(1);
                }
                if (LiquidHookEx::proc->ReadDirect<LONG>(hookDataAddress +
                    offsetof(LiquidHookEx::Dx12PresentHookData, hookBusy)) != 0) {
                    printf("Timed out waiting for the DX12 Present callback before renderer recovery.\n");
                    break;
                }

                printf("[HeadlessRendererDX12] Starting a replacement renderer...\n");
                if (!renderer.Start(&renderObjectManager, targetHwnd, overlayWidth, overlayHeight,
                    nullptr, &targetAdapterLuid)) {
                    printf("Couldn't restart the DX12 renderer after the frame stall.\n");
                    break;
                }
                while (renderer.IsRunning() && !renderer.IsReady())
                    Sleep(1);
                if (!renderer.IsReady()) {
                    printf("DX12 renderer failed to recover after the frame stall.\n");
                    break;
                }
                printf("[HeadlessRendererDX12] Shared renderer restarted after the frame stall.\n");
                lastFrameProgressTick = GetTickCount64();
            }
            if (GetAsyncKeyState(VK_LSHIFT)) {
                auto data = LiquidHookEx::PresentDx12::m_Hook.ReadData<LiquidHookEx::Dx12PresentHookData>();
                printf("Status: %i hookError=0x%08X failureStage=%ld presentResult=0x%08X targetDeviceReason=0x%08X sharedFence=%llu frame=%llu targetLuid=%08X:%08X overlayLuid=%08X:%08X\n",
                    data.initState, static_cast<unsigned int>(data.lastHookError),
                    data.lastHookFailureStage,
                    static_cast<unsigned int>(data.lastPresentResult),
                    static_cast<unsigned int>(data.targetDeviceRemovedReason),
                    static_cast<unsigned long long>(data.sharedFenceCompletedValue),
                    static_cast<unsigned long long>(data.frameIndex),
                    static_cast<unsigned int>(data.adapterLuid.HighPart),
                    static_cast<unsigned int>(data.adapterLuid.LowPart),
                    data.handshakeAdapterLuidHigh, data.handshakeAdapterLuidLow);
            }
            Sleep(100);

        }

        auto finalHookData = LiquidHookEx::PresentDx12::m_Hook.ReadData<LiquidHookEx::Dx12PresentHookData>();
        printf("DX12 final hook status=%ld error=0x%08X stage=%ld present=0x%08X targetDeviceReason=0x%08X sharedFence=%llu frame=%llu targetLuid=%08X:%08X overlayLuid=%08X:%08X\n",
            finalHookData.initState, static_cast<unsigned int>(finalHookData.lastHookError),
            finalHookData.lastHookFailureStage, static_cast<unsigned int>(finalHookData.lastPresentResult),
            static_cast<unsigned int>(finalHookData.targetDeviceRemovedReason),
            static_cast<unsigned long long>(finalHookData.sharedFenceCompletedValue),
            static_cast<unsigned long long>(finalHookData.frameIndex),
            static_cast<unsigned int>(finalHookData.adapterLuid.HighPart),
            static_cast<unsigned int>(finalHookData.adapterLuid.LowPart),
            finalHookData.handshakeAdapterLuidHigh, finalHookData.handshakeAdapterLuidLow);

        if (!LiquidHookEx::PresentDx12::Unhook()) {
            printf("Couldn't unhook DX12 Present. Keep the target alive and restart it.\n");
            return -1;
        }
        renderer.Stop();
        DX12::g_graphicsProvider.release(graphicsContext);
        return 0;
	}
}