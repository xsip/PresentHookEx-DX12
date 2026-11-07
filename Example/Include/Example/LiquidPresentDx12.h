#pragma once

#include <Example/Dx12GraphicsProvider.h>
#include <HeadlessRenderer/Dx12OverlayProtocol.h>
#include <LiquidHookEx/Include.h>
#include <d3dcompiler.h>

namespace LiquidHookEx {

    struct Dx12ExecuteHookData : public LiquidHookEx::VTable::BaseHookData {
        ID3D12CommandQueue* volatile applicationQueue = nullptr;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        volatile LONG captureBusy = 0;
    };

    struct Dx12PresentHookData : public LiquidHookEx::VTable::BaseHookData {

        DX12::GraphicsContext graphicsContext{};
        Dx12ExecuteHookData* executeHookData = nullptr;
        DWORD targetPid = 0;

        using OpenFileMappingAFn = HANDLE(WINAPI*)(DWORD, BOOL, LPCSTR);
        using MapViewOfFileFn = LPVOID(WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
        using UnmapViewOfFileFn = BOOL(WINAPI*)(LPCVOID);
        using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
        using CreateEventWFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
        using WaitForSingleObjectFn = DWORD(WINAPI*)(HANDLE, DWORD);
        using SleepFn = VOID(WINAPI*)(DWORD);
        using SerializeRootSignatureFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*,
            D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

        OpenFileMappingAFn pOpenFileMappingA = nullptr;
        MapViewOfFileFn pMapViewOfFile = nullptr;
        UnmapViewOfFileFn pUnmapViewOfFile = nullptr;
        CloseHandleFn pCloseHandle = nullptr;
        CreateEventWFn pCreateEventW = nullptr;
        WaitForSingleObjectFn pWaitForSingleObject = nullptr;
        SleepFn pSleep = nullptr;
        SerializeRootSignatureFn pSerializeRootSignature = nullptr;

        IID iidDevice{};
        IID iidCommandQueue{};
        IID iidSwapChain3{};
        IID iidResource{};
        IID iidFence{};
        IID iidDescriptorHeap{};
        IID iidCommandAllocator{};
        IID iidGraphicsCommandList{};
        IID iidRootSignature{};
        IID iidPipelineState{};

        BYTE vertexShader[4096]{};
        SIZE_T vertexShaderSize = 0;
        BYTE pixelShader[4096]{};
        SIZE_T pixelShaderSize = 0;
        char handshakeName[128]{};

        ID3D12Resource* overlayResource = nullptr;
        ID3D12Fence* sharedFence = nullptr;
        ID3D12DescriptorHeap* srvHeap = nullptr;
        ID3D12DescriptorHeap* rtvHeap = nullptr;
        ID3D12RootSignature* rootSignature = nullptr;
        ID3D12PipelineState* pipelineState = nullptr;
        ID3D12CommandAllocator* commandAllocator = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        ID3D12Fence* compositorFence = nullptr;
        HANDLE compositorFenceEvent = nullptr;
        HANDLE handshakeMapping = nullptr;
        DX12Overlay::Handshake* handshake = nullptr;
        ID3D12Device* queueDevice = nullptr;
        ID3D12Device* presentDevice = nullptr;
        ID3D12CommandQueue* compositorQueue = nullptr;
        IDXGISwapChain3* activeSwapChain = nullptr;
        IDXGISwapChain3* candidateSwapChain = nullptr;
        ID3D12Resource* currentBackBuffer = nullptr;
        ID3DBlob* rootSignatureBlob = nullptr;
        ID3DBlob* rootSignatureErrors = nullptr;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        D3D12_RESOURCE_DESC currentBackBufferDesc{};
        D3D12_DESCRIPTOR_RANGE rootDescriptorRange{};
        D3D12_ROOT_PARAMETER rootParameter{};
        D3D12_STATIC_SAMPLER_DESC rootSampler{};
        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
        D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
        D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
        D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc{};
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle{};
        D3D12_RESOURCE_BARRIER resourceBarrier{};
        D3D12_VIEWPORT viewport{};
        D3D12_RECT scissorRect{};
        ID3D12DescriptorHeap* activeDescriptorHeaps[1]{};
        ID3D12CommandList* activeCommandLists[1]{};
        HANDLE importedResourceHandle = nullptr;
        HANDLE importedFenceHandle = nullptr;
        LUID adapterLuid{};
        UINT32 overlayWidth = 0;
        UINT32 overlayHeight = 0;
        LONG handshakeState = 0;
        UINT frameBufferIndex = 0;
        UINT waitAttempts = 0;
        UINT64 readyFenceValue = 0;
        UINT64 consumedFenceValue = 0;
        UINT64 completedFenceValue = 0;
        HRESULT operationResult = S_OK;
        UINT rtvDescriptorSize = 0;
        UINT64 frameIndex = 0;
        UINT64 compositorFenceValue = 0;
        DXGI_FORMAT pipelineFormat = DXGI_FORMAT_UNKNOWN;
        volatile LONG initState = 0;
        volatile LONG hookBusy = 0;
        HRESULT lastHookError = S_OK;
        HRESULT lastPresentResult = S_OK;
        LONG hookStage = 0;
        LONG lastHookFailureStage = 0;
        volatile LONG adapterLuidReady = 0;
        UINT32 handshakeAdapterLuidLow = 0;
        UINT32 handshakeAdapterLuidHigh = 0;
        UINT64 handshakeGeneration = 0;
        UINT64 sharedFenceCompletedValue = 0;
        HRESULT targetDeviceRemovedReason = S_OK;
    };

    class PresentDx12 {
    public:
        inline static LiquidHookEx::VTable m_Hook = LiquidHookEx::VTable("Dx12PresentHook");
        inline static LiquidHookEx::VTable m_ExecuteHook = LiquidHookEx::VTable("Dx12ExecuteCommandListsHook");

    public:

        static bool Hook(const DX12::GraphicsContext& context);
        static bool Unhook();
        static bool IsHooked() { return m_Hook.IsHooked(); }
    };

}
