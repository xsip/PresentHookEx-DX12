#include <Example/LiquidPresentDx12.h>
#include <LiquidHookEx/Macros.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

namespace LiquidHookEx {

    static void* g_dx12OriginalPresent = nullptr;
    static Dx12PresentHookData* g_dx12PresentData = nullptr;
    static void* g_dx12OriginalExecuteCommandLists = nullptr;
    static Dx12ExecuteHookData* g_dx12ExecuteHookData = nullptr;

    namespace DX12PresentShellcode {
        LH_START("lhPresent12")

            __declspec(safebuffers)
            HRESULT __stdcall HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {

            using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
            PresentFn originalPresent = reinterpret_cast<PresentFn>(g_dx12OriginalPresent);
            Dx12PresentHookData* data = g_dx12PresentData;
            if (!originalPresent)
                return DXGI_ERROR_INVALID_CALL;
            if (!data || InterlockedCompareExchange(&data->hookBusy, 1, 0) != 0)
                return originalPresent(swapChain, syncInterval, flags);
            data->hookStage = 0;
            if (data->initState < 0)
                goto dx12_forward;

            if (data->initState != 1) {
                data->initState = 2;
                data->hookStage = 100;
                if (FAILED(data->graphicsContext.status) ||
                    !swapChain ||
                    !data->executeHookData ||
                    !data->pOpenFileMappingA || !data->pMapViewOfFile ||
                    !data->pUnmapViewOfFile || !data->pCloseHandle ||
                    !data->pCreateEventW || !data->pWaitForSingleObject ||
                    !data->pSleep || !data->pSerializeRootSignature) {
                    data->operationResult = E_INVALIDARG;
                    goto dx12_cleanup;
                }
                data->hookStage = 101;
                data->operationResult = swapChain->GetDevice(data->iidDevice,
                    reinterpret_cast<void**>(&data->presentDevice));
                if (FAILED(data->operationResult) || !data->presentDevice) {
                    data->operationResult = FAILED(data->operationResult) ? data->operationResult : E_FAIL;
                    goto dx12_cleanup;
                }
                data->adapterLuid = data->presentDevice->GetAdapterLuid();
                InterlockedExchange(&data->adapterLuidReady, 1);
                data->hookStage = 102;
                data->operationResult = swapChain->QueryInterface(data->iidSwapChain3,
                    reinterpret_cast<void**>(&data->activeSwapChain));
                if (FAILED(data->operationResult) || !data->activeSwapChain) {
                    data->operationResult = FAILED(data->operationResult) ? data->operationResult : E_FAIL;
                    goto dx12_cleanup;
                }
                data->hookStage = 103;
                data->compositorQueue = reinterpret_cast<ID3D12CommandQueue*>(
                    InterlockedCompareExchangePointer(
                        reinterpret_cast<PVOID volatile*>(&data->executeHookData->applicationQueue),
                        nullptr, nullptr));
                if (!data->compositorQueue) {
                    data->operationResult = E_PENDING;
                    goto dx12_cleanup;
                }
                data->queueDesc = data->compositorQueue->GetDesc();
                if (data->queueDesc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
                    data->compositorQueue = nullptr;
                    data->operationResult = E_INVALIDARG;
                    goto dx12_cleanup;
                }
                data->compositorQueue->AddRef();
                data->hookStage = 110;
                data->handshakeMapping = data->pOpenFileMappingA(
                    FILE_MAP_READ, FALSE, data->handshakeName);
                if (!data->handshakeMapping) {
                    data->operationResult = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
                    goto dx12_cleanup;
                }
                data->hookStage = 111;
                data->handshake = static_cast<DX12Overlay::Handshake*>(
                    data->pMapViewOfFile(data->handshakeMapping,
                        FILE_MAP_READ, 0, 0, sizeof(DX12Overlay::Handshake)));
                if (!data->handshake) {
                    data->operationResult = HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
                    goto dx12_cleanup;
                }

                data->hookStage = 112;
                data->handshakeState = *reinterpret_cast<volatile LONG*>(&data->handshake->state);
                data->overlayWidth = data->handshake->width;
                data->overlayHeight = data->handshake->height;
                data->importedResourceHandle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(data->handshake->resourceHandle));
                data->importedFenceHandle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(data->handshake->fenceHandle));
                data->handshakeAdapterLuidLow = data->handshake->adapterLuidLow;
                data->handshakeAdapterLuidHigh = data->handshake->adapterLuidHigh;
                if (data->handshakeGeneration != data->handshake->generation) {
                    data->handshakeGeneration = data->handshake->generation;
                    data->frameIndex = 0;
                }
                if (data->handshake->magic != DX12Overlay::HANDSHAKE_MAGIC) {
                    data->hookStage = 1121; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->version != DX12Overlay::HANDSHAKE_VERSION) {
                    data->hookStage = 1122; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->headerSize != sizeof(DX12Overlay::Handshake)) {
                    data->hookStage = 1123; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->api != DX12Overlay::API_DIRECT3D12) {
                    data->hookStage = 1124; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshakeState != static_cast<LONG>(DX12Overlay::HandshakeState::Ready)) {
                    data->hookStage = 1125; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->targetPid != data->targetPid) {
                    data->hookStage = 1126; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (!data->overlayWidth || !data->overlayHeight) {
                    data->hookStage = 1127; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->format != static_cast<UINT32>(DXGI_FORMAT_B8G8R8A8_UNORM)) {
                    data->hookStage = 1128; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->adapterLuidLow != data->adapterLuid.LowPart) {
                    data->hookStage = 1129; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (data->handshake->adapterLuidHigh != static_cast<UINT32>(data->adapterLuid.HighPart)) {
                    data->hookStage = 1130; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                if (!data->importedResourceHandle || !data->importedFenceHandle) {
                    data->hookStage = 1131; data->operationResult = DXGI_ERROR_INVALID_CALL; goto dx12_cleanup;
                }
                data->hookStage = 120;
                data->operationResult = data->presentDevice->OpenSharedHandle(
                    data->importedResourceHandle, data->iidResource,
                    reinterpret_cast<void**>(&data->overlayResource));
                if (FAILED(data->operationResult) || !data->overlayResource)
                    goto dx12_cleanup;
                data->hookStage = 121;
                data->operationResult = data->presentDevice->OpenSharedHandle(
                    data->importedFenceHandle, data->iidFence,
                    reinterpret_cast<void**>(&data->sharedFence));
                if (FAILED(data->operationResult) || !data->sharedFence)
                    goto dx12_cleanup;
                data->hookStage = 122;
                data->currentBackBufferDesc = data->overlayResource->GetDesc();
                if (data->currentBackBufferDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
                    data->currentBackBufferDesc.Width != data->overlayWidth ||
                    data->currentBackBufferDesc.Height != data->overlayHeight) {
                    data->operationResult = DXGI_ERROR_INVALID_CALL;
                    goto dx12_cleanup;
                }
                data->descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                data->descriptorHeapDesc.NumDescriptors = 1;
                data->descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                data->hookStage = 130;
                data->operationResult = data->presentDevice->CreateDescriptorHeap(
                    &data->descriptorHeapDesc, data->iidDescriptorHeap,
                    reinterpret_cast<void**>(&data->srvHeap));
                if (FAILED(data->operationResult) || !data->srvHeap)
                    goto dx12_cleanup;
                data->shaderResourceViewDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                data->shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                data->shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                data->shaderResourceViewDesc.Texture2D.MipLevels = 1;
                data->presentDevice->CreateShaderResourceView(data->overlayResource,
                    &data->shaderResourceViewDesc, data->srvHeap->GetCPUDescriptorHandleForHeapStart());
                data->descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                data->descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                data->hookStage = 131;
                data->operationResult = data->presentDevice->CreateDescriptorHeap(
                    &data->descriptorHeapDesc, data->iidDescriptorHeap,
                    reinterpret_cast<void**>(&data->rtvHeap));
                if (FAILED(data->operationResult) || !data->rtvHeap)
                    goto dx12_cleanup;
                data->rtvDescriptorSize = data->presentDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                data->hookStage = 132;
                data->operationResult = data->presentDevice->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT, data->iidCommandAllocator,
                    reinterpret_cast<void**>(&data->commandAllocator));
                if (FAILED(data->operationResult) || !data->commandAllocator)
                    goto dx12_cleanup;
                data->hookStage = 133;
                data->operationResult = data->presentDevice->CreateCommandList(
                    0, D3D12_COMMAND_LIST_TYPE_DIRECT, data->commandAllocator, nullptr,
                    data->iidGraphicsCommandList, reinterpret_cast<void**>(&data->commandList));
                if (FAILED(data->operationResult) || !data->commandList)
                    goto dx12_cleanup;
                data->hookStage = 134;
                data->operationResult = data->commandList->Close();
                if (FAILED(data->operationResult)) goto dx12_cleanup;
                data->hookStage = 135;
                data->operationResult = data->presentDevice->CreateFence(
                    0, D3D12_FENCE_FLAG_NONE, data->iidFence,
                    reinterpret_cast<void**>(&data->compositorFence));
                if (FAILED(data->operationResult) || !data->compositorFence)
                    goto dx12_cleanup;
                data->hookStage = 136;
                data->compositorFenceEvent = data->pCreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!data->compositorFenceEvent) goto dx12_cleanup;
                data->hookStage = 137;
                data->operationResult = data->activeSwapChain->GetDesc1(&data->swapChainDesc);
                if (FAILED(data->operationResult)) goto dx12_cleanup;
                data->rootDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                data->rootDescriptorRange.NumDescriptors = 1;
                data->rootDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                data->rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                data->rootParameter.DescriptorTable.NumDescriptorRanges = 1;
                data->rootParameter.DescriptorTable.pDescriptorRanges = &data->rootDescriptorRange;
                data->rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                data->rootSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                data->rootSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                data->rootSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                data->rootSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                data->rootSampler.MaxAnisotropy = 1;
                data->rootSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                data->rootSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;


                *reinterpret_cast<UINT*>(&data->rootSampler.MaxLOD) = 0x7F7FFFFF;
                data->rootSignatureDesc.NumParameters = 1;
                data->rootSignatureDesc.pParameters = &data->rootParameter;
                data->rootSignatureDesc.NumStaticSamplers = 1;
                data->rootSignatureDesc.pStaticSamplers = &data->rootSampler;
                data->hookStage = 138;
                data->operationResult = data->pSerializeRootSignature(
                    &data->rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                    &data->rootSignatureBlob, &data->rootSignatureErrors);
                if (data->rootSignatureErrors) { data->rootSignatureErrors->Release(); data->rootSignatureErrors = nullptr; }
                if (FAILED(data->operationResult) || !data->rootSignatureBlob) goto dx12_cleanup;
                data->hookStage = 139;
                data->operationResult = data->presentDevice->CreateRootSignature(
                    0, data->rootSignatureBlob->GetBufferPointer(), data->rootSignatureBlob->GetBufferSize(),
                    data->iidRootSignature, reinterpret_cast<void**>(&data->rootSignature));
                data->rootSignatureBlob->Release();
                data->rootSignatureBlob = nullptr;
                if (FAILED(data->operationResult) || !data->rootSignature) goto dx12_cleanup;
                data->pipelineDesc.pRootSignature = data->rootSignature;
                data->pipelineDesc.VS.pShaderBytecode = data->vertexShader;
                data->pipelineDesc.VS.BytecodeLength = data->vertexShaderSize;
                data->pipelineDesc.PS.pShaderBytecode = data->pixelShader;
                data->pipelineDesc.PS.BytecodeLength = data->pixelShaderSize;
                data->pipelineDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                data->pipelineDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                data->pipelineDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                data->pipelineDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                data->pipelineDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                data->pipelineDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                data->pipelineDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                data->pipelineDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
                data->pipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                data->pipelineDesc.SampleMask = UINT_MAX;
                data->pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                data->pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                data->pipelineDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
                *reinterpret_cast<UINT*>(&data->pipelineDesc.RasterizerState.DepthBiasClamp) = 0;
                *reinterpret_cast<UINT*>(&data->pipelineDesc.RasterizerState.SlopeScaledDepthBias) = 0;
                data->pipelineDesc.RasterizerState.DepthClipEnable = TRUE;
                data->pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                data->pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                data->pipelineDesc.NumRenderTargets = 1;
                data->pipelineDesc.RTVFormats[0] = data->swapChainDesc.Format;
                data->pipelineDesc.SampleDesc.Count = 1;
                data->hookStage = 140;
                data->operationResult = data->presentDevice->CreateGraphicsPipelineState(
                    &data->pipelineDesc, data->iidPipelineState,
                    reinterpret_cast<void**>(&data->pipelineState));
                if (FAILED(data->operationResult) || !data->pipelineState) goto dx12_cleanup;
                data->pipelineFormat = data->swapChainDesc.Format;
                data->initState = 1;
                data->lastHookError = S_OK;
                data->lastHookFailureStage = 0;
            }

            // Keep the read-only handshake mapping alive while initialized so
            // a renderer restart (new generation/handles) is noticed without
            // requiring the target process to be restarted.
            if (data->handshake &&
                (data->handshake->state != static_cast<UINT32>(DX12Overlay::HandshakeState::Ready) ||
                    data->handshake->generation != data->handshakeGeneration)) {
                data->hookStage = 180;
                data->operationResult = S_OK;
                goto dx12_cleanup;
            }

            // Present can be called for a replacement swap chain after a
            // window move, display change, or device-resource recreation.
            // Drop the old chain-bound resources and rebuild on the next
            // Present instead of compositing into a stale back buffer.
            data->hookStage = 190;
            data->operationResult = swapChain->QueryInterface(data->iidSwapChain3,
                reinterpret_cast<void**>(&data->candidateSwapChain));
            if (FAILED(data->operationResult) || !data->candidateSwapChain)
                goto dx12_cleanup;
            if (data->candidateSwapChain != data->activeSwapChain) {
                data->hookStage = 191;
                data->operationResult = S_OK;
                goto dx12_cleanup;
            }
            data->candidateSwapChain->Release();
            data->candidateSwapChain = nullptr;

            data->hookStage = 200;
            data->operationResult = data->compositorQueue->GetDevice(data->iidDevice,
                reinterpret_cast<void**>(&data->queueDevice));
            if (FAILED(data->operationResult) || !data->queueDevice ||
                data->queueDevice != data->presentDevice) {
                if (data->queueDevice) { data->queueDevice->Release(); data->queueDevice = nullptr; }
                data->operationResult = DXGI_ERROR_INVALID_CALL;
                goto dx12_cleanup;
            }
            data->queueDevice->Release();
            data->queueDevice = nullptr;

            data->readyFenceValue = data->frameIndex * 2 + 1;
            data->hookStage = 210;
            data->consumedFenceValue = data->frameIndex * 2 + 2;
            data->waitAttempts = 0;
            data->sharedFenceCompletedValue = data->sharedFence->GetCompletedValue();
            while (data->sharedFenceCompletedValue < data->readyFenceValue &&
                data->waitAttempts++ < 16) {
                data->pSleep(1);
                data->sharedFenceCompletedValue = data->sharedFence->GetCompletedValue();
            }
            if (data->sharedFenceCompletedValue == UINT64_MAX) {
                data->targetDeviceRemovedReason = data->presentDevice->GetDeviceRemovedReason();
                data->operationResult = DXGI_ERROR_DEVICE_REMOVED; goto dx12_cleanup;
            }
            if (data->sharedFenceCompletedValue < data->readyFenceValue)
                goto dx12_forward;
            data->frameBufferIndex = data->activeSwapChain->GetCurrentBackBufferIndex();
            data->hookStage = 211;
            data->operationResult = data->activeSwapChain->GetBuffer(
                data->frameBufferIndex, data->iidResource,
                reinterpret_cast<void**>(&data->currentBackBuffer));
            if (FAILED(data->operationResult) || !data->currentBackBuffer) goto dx12_cleanup;
            data->currentBackBufferDesc = data->currentBackBuffer->GetDesc();
            if (data->pipelineFormat != data->currentBackBufferDesc.Format) {
                data->pipelineState->Release();
                data->pipelineState = nullptr;
                data->pipelineDesc.RTVFormats[0] = data->currentBackBufferDesc.Format;
                data->hookStage = 212;
                data->operationResult = data->presentDevice->CreateGraphicsPipelineState(
                    &data->pipelineDesc, data->iidPipelineState,
                    reinterpret_cast<void**>(&data->pipelineState));
                if (FAILED(data->operationResult) || !data->pipelineState) goto dx12_cleanup;
                data->pipelineFormat = data->currentBackBufferDesc.Format;
            }
            if (data->compositorFenceValue) {
                data->hookStage = 220;
                data->completedFenceValue = data->compositorFence->GetCompletedValue();
                if (data->completedFenceValue == UINT64_MAX) { data->operationResult = DXGI_ERROR_DEVICE_REMOVED; goto dx12_cleanup; }
                if (data->completedFenceValue < data->compositorFenceValue) {
                    data->operationResult = data->compositorFence->SetEventOnCompletion(
                        data->compositorFenceValue, data->compositorFenceEvent);
                    if (FAILED(data->operationResult) || data->pWaitForSingleObject(
                        data->compositorFenceEvent, INFINITE) != WAIT_OBJECT_0) goto dx12_cleanup;
                }
            }
            data->hookStage = 221;
            data->operationResult = data->compositorQueue->Wait(
                data->sharedFence, data->readyFenceValue);
            if (FAILED(data->operationResult)) goto dx12_cleanup;
            data->hookStage = 222;
            data->operationResult = data->commandAllocator->Reset();
            if (FAILED(data->operationResult)) goto dx12_cleanup;
            data->hookStage = 223;
            data->operationResult = data->commandList->Reset(
                data->commandAllocator, data->pipelineState);
            if (FAILED(data->operationResult)) goto dx12_cleanup;
            data->renderTargetViewDesc.Format = data->currentBackBufferDesc.Format;
            data->renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            data->renderTargetHandle = data->rtvHeap->GetCPUDescriptorHandleForHeapStart();
            data->presentDevice->CreateRenderTargetView(data->currentBackBuffer,
                &data->renderTargetViewDesc, data->renderTargetHandle);
            data->resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            data->resourceBarrier.Transition.pResource = data->currentBackBuffer;
            data->resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            data->resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            data->resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            data->commandList->ResourceBarrier(1, &data->resourceBarrier);
            data->commandList->OMSetRenderTargets(1, &data->renderTargetHandle, FALSE, nullptr);
            data->viewport.Width = static_cast<float>(data->currentBackBufferDesc.Width);
            data->viewport.Height = static_cast<float>(data->currentBackBufferDesc.Height);
            *reinterpret_cast<UINT*>(&data->viewport.MaxDepth) = 0x3F800000;
            data->commandList->RSSetViewports(1, &data->viewport);
            data->scissorRect.right = static_cast<LONG>(data->currentBackBufferDesc.Width);
            data->scissorRect.bottom = static_cast<LONG>(data->currentBackBufferDesc.Height);
            data->commandList->RSSetScissorRects(1, &data->scissorRect);
            data->activeDescriptorHeaps[0] = data->srvHeap;
            data->commandList->SetDescriptorHeaps(1, data->activeDescriptorHeaps);
            data->commandList->SetGraphicsRootSignature(data->rootSignature);
            data->commandList->SetPipelineState(data->pipelineState);
            data->commandList->SetGraphicsRootDescriptorTable(0,
                data->srvHeap->GetGPUDescriptorHandleForHeapStart());
            data->commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            data->commandList->DrawInstanced(3, 1, 0, 0);
            data->resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            data->resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            data->commandList->ResourceBarrier(1, &data->resourceBarrier);
            data->hookStage = 224;
            data->operationResult = data->commandList->Close();
            if (FAILED(data->operationResult)) goto dx12_cleanup;
            data->activeCommandLists[0] = data->commandList;
            data->compositorQueue->ExecuteCommandLists(1, data->activeCommandLists);
            data->hookStage = 225;
            data->operationResult = data->compositorQueue->Signal(
                data->sharedFence, data->consumedFenceValue);
            if (FAILED(data->operationResult)) goto dx12_cleanup;
            ++data->compositorFenceValue;
            data->hookStage = 226;
            data->operationResult = data->compositorQueue->Signal(
                data->compositorFence, data->compositorFenceValue);
            if (FAILED(data->operationResult)) goto dx12_cleanup;
            data->hookStage = 227;
            data->completedFenceValue = data->compositorFence->GetCompletedValue();
            if (data->completedFenceValue < data->compositorFenceValue) {
                data->operationResult = data->compositorFence->SetEventOnCompletion(
                    data->compositorFenceValue, data->compositorFenceEvent);
                if (FAILED(data->operationResult) || data->pWaitForSingleObject(
                    data->compositorFenceEvent, INFINITE) != WAIT_OBJECT_0) goto dx12_cleanup;
            }
            data->currentBackBuffer->Release();
            data->currentBackBuffer = nullptr;
            ++data->frameIndex;
            goto dx12_forward;

        dx12_cleanup:
            data->lastHookError = data->operationResult;
            data->lastHookFailureStage = data->hookStage;
            if (data->handshake) { data->pUnmapViewOfFile(data->handshake); data->handshake = nullptr; }
            if (data->handshakeMapping) { data->pCloseHandle(data->handshakeMapping); data->handshakeMapping = nullptr; }
            if (data->rootSignatureErrors) { data->rootSignatureErrors->Release(); data->rootSignatureErrors = nullptr; }
            if (data->rootSignatureBlob) { data->rootSignatureBlob->Release(); data->rootSignatureBlob = nullptr; }
            if (data->currentBackBuffer) { data->currentBackBuffer->Release(); data->currentBackBuffer = nullptr; }
            if (data->candidateSwapChain) { data->candidateSwapChain->Release(); data->candidateSwapChain = nullptr; }
            if (data->activeSwapChain) { data->activeSwapChain->Release(); data->activeSwapChain = nullptr; }
            if (data->queueDevice) { data->queueDevice->Release(); data->queueDevice = nullptr; }
            if (data->compositorQueue) { data->compositorQueue->Release(); data->compositorQueue = nullptr; }
            if (data->presentDevice) { data->presentDevice->Release(); data->presentDevice = nullptr; }
            if (data->commandList) { data->commandList->Release(); data->commandList = nullptr; }
            if (data->commandAllocator) { data->commandAllocator->Release(); data->commandAllocator = nullptr; }
            if (data->pipelineState) { data->pipelineState->Release(); data->pipelineState = nullptr; }
            if (data->rootSignature) { data->rootSignature->Release(); data->rootSignature = nullptr; }
            if (data->rtvHeap) { data->rtvHeap->Release(); data->rtvHeap = nullptr; }
            if (data->srvHeap) { data->srvHeap->Release(); data->srvHeap = nullptr; }
            if (data->compositorFence) { data->compositorFence->Release(); data->compositorFence = nullptr; }
            if (data->sharedFence) { data->sharedFence->Release(); data->sharedFence = nullptr; }
            if (data->overlayResource) { data->overlayResource->Release(); data->overlayResource = nullptr; }
            if (data->compositorFenceEvent) { data->pCloseHandle(data->compositorFenceEvent); data->compositorFenceEvent = nullptr; }
            data->rtvDescriptorSize = 0;
            data->compositorFenceValue = 0;
            data->pipelineFormat = DXGI_FORMAT_UNKNOWN;
            data->initState = 0;

        dx12_forward:
            data->operationResult = originalPresent(swapChain, syncInterval, flags);
            data->lastPresentResult = data->operationResult;
            if (data->operationResult == DXGI_ERROR_DEVICE_REMOVED ||
                data->operationResult == DXGI_ERROR_DEVICE_RESET) {


                if (data->handshake) { data->pUnmapViewOfFile(data->handshake); data->handshake = nullptr; }
                if (data->handshakeMapping) { data->pCloseHandle(data->handshakeMapping); data->handshakeMapping = nullptr; }
                if (data->rootSignatureErrors) { data->rootSignatureErrors->Release(); data->rootSignatureErrors = nullptr; }
                if (data->rootSignatureBlob) { data->rootSignatureBlob->Release(); data->rootSignatureBlob = nullptr; }
                if (data->currentBackBuffer) { data->currentBackBuffer->Release(); data->currentBackBuffer = nullptr; }
                if (data->candidateSwapChain) { data->candidateSwapChain->Release(); data->candidateSwapChain = nullptr; }
                if (data->activeSwapChain) { data->activeSwapChain->Release(); data->activeSwapChain = nullptr; }
                if (data->queueDevice) { data->queueDevice->Release(); data->queueDevice = nullptr; }
                if (data->compositorQueue) { data->compositorQueue->Release(); data->compositorQueue = nullptr; }
                if (data->presentDevice) { data->presentDevice->Release(); data->presentDevice = nullptr; }
                if (data->commandList) { data->commandList->Release(); data->commandList = nullptr; }
                if (data->commandAllocator) { data->commandAllocator->Release(); data->commandAllocator = nullptr; }
                if (data->pipelineState) { data->pipelineState->Release(); data->pipelineState = nullptr; }
                if (data->rootSignature) { data->rootSignature->Release(); data->rootSignature = nullptr; }
                if (data->rtvHeap) { data->rtvHeap->Release(); data->rtvHeap = nullptr; }
                if (data->srvHeap) { data->srvHeap->Release(); data->srvHeap = nullptr; }
                if (data->compositorFence) { data->compositorFence->Release(); data->compositorFence = nullptr; }
                if (data->sharedFence) { data->sharedFence->Release(); data->sharedFence = nullptr; }
                if (data->overlayResource) { data->overlayResource->Release(); data->overlayResource = nullptr; }
                if (data->compositorFenceEvent) { data->pCloseHandle(data->compositorFenceEvent); data->compositorFenceEvent = nullptr; }
                data->rtvDescriptorSize = 0;
                data->compositorFenceValue = 0;
                data->pipelineFormat = DXGI_FORMAT_UNKNOWN;
                data->initState = 0;
            }
            InterlockedExchange(&data->hookBusy, 0);
            return data->operationResult;
        }

        __declspec(safebuffers)
            void HookPresentEnd() {}

        LH_END()

            LH_START("lhDx12ExecuteCommandLists")

            __declspec(safebuffers)
            void STDMETHODCALLTYPE HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT count,
                ID3D12CommandList* const* commandLists) {
            using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                ID3D12CommandList* const*);
            ExecuteCommandListsFn originalExecuteCommandLists =
                reinterpret_cast<ExecuteCommandListsFn>(g_dx12OriginalExecuteCommandLists);
            Dx12ExecuteHookData* data = g_dx12ExecuteHookData;
            if (!originalExecuteCommandLists)
                return;
            if (data && queue && InterlockedCompareExchange(&data->captureBusy, 1, 0) == 0) {
                data->queueDesc = queue->GetDesc();
                if (data->queueDesc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
                    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&data->applicationQueue), queue);
                InterlockedExchange(&data->captureBusy, 0);
            }
            originalExecuteCommandLists(queue, count, commandLists);
        }

        __declspec(safebuffers)
            void HookExecuteCommandListsEnd() {}

        LH_END()

            LH_START("lhPresent12Cleanup")

            static void ReleaseResources(Dx12PresentHookData* data);

        __declspec(safebuffers)
            DWORD WINAPI CleanupRemote(void* context) {
            ReleaseResources(static_cast<Dx12PresentHookData*>(context));
            return 0;
        }

        __declspec(safebuffers)
            static void ReleaseResources(Dx12PresentHookData* data) {
            if (!data)
                return;
            if (data->commandList) { data->commandList->Release(); data->commandList = nullptr; }
            if (data->commandAllocator) { data->commandAllocator->Release(); data->commandAllocator = nullptr; }
            if (data->candidateSwapChain) { data->candidateSwapChain->Release(); data->candidateSwapChain = nullptr; }
            if (data->activeSwapChain) { data->activeSwapChain->Release(); data->activeSwapChain = nullptr; }
            if (data->queueDevice) { data->queueDevice->Release(); data->queueDevice = nullptr; }
            if (data->compositorQueue) { data->compositorQueue->Release(); data->compositorQueue = nullptr; }
            if (data->presentDevice) { data->presentDevice->Release(); data->presentDevice = nullptr; }
            if (data->pipelineState) { data->pipelineState->Release(); data->pipelineState = nullptr; }
            if (data->rootSignature) { data->rootSignature->Release(); data->rootSignature = nullptr; }
            if (data->rtvHeap) { data->rtvHeap->Release(); data->rtvHeap = nullptr; }
            if (data->srvHeap) { data->srvHeap->Release(); data->srvHeap = nullptr; }
            if (data->compositorFence) { data->compositorFence->Release(); data->compositorFence = nullptr; }
            if (data->sharedFence) { data->sharedFence->Release(); data->sharedFence = nullptr; }
            if (data->overlayResource) { data->overlayResource->Release(); data->overlayResource = nullptr; }
            if (data->compositorFenceEvent) {
                data->pCloseHandle(data->compositorFenceEvent);
                data->compositorFenceEvent = nullptr;
            }
            data->rtvDescriptorSize = 0;
            data->compositorFenceValue = 0;
            data->pipelineFormat = DXGI_FORMAT_UNKNOWN;
        }

        __declspec(safebuffers)
            void CleanupRemoteEnd() {}

        LH_END()
    }

    bool PresentDx12::Hook(const DX12::GraphicsContext& context) {
        if (!context.IsComplete()) {
            std::printf("[DX12 Present] Acquisition context is incomplete or failed (0x%08X).\n",
                context.status);
            return false;
        }

        auto kernel32 = LiquidHookEx::proc->GetRemoteModule("kernel32.dll", false);
        auto d3d12 = LiquidHookEx::proc->GetRemoteModule("d3d12.dll", false);
        if (!kernel32 || !d3d12) {
            std::printf("[DX12 Present] Target kernel32.dll or d3d12.dll is unavailable.\n");
            return false;
        }

        Dx12PresentHookData hookData{};
        hookData.graphicsContext = context;
        hookData.targetPid = LiquidHookEx::proc->GetProcId();
        hookData.iidDevice = __uuidof(ID3D12Device);
        hookData.iidCommandQueue = __uuidof(ID3D12CommandQueue);
        hookData.iidSwapChain3 = __uuidof(IDXGISwapChain3);
        hookData.iidResource = __uuidof(ID3D12Resource);
        hookData.iidFence = __uuidof(ID3D12Fence);
        hookData.iidDescriptorHeap = __uuidof(ID3D12DescriptorHeap);
        hookData.iidCommandAllocator = __uuidof(ID3D12CommandAllocator);
        hookData.iidGraphicsCommandList = __uuidof(ID3D12GraphicsCommandList);
        hookData.iidRootSignature = __uuidof(ID3D12RootSignature);
        hookData.iidPipelineState = __uuidof(ID3D12PipelineState);

        hookData.pOpenFileMappingA = reinterpret_cast<Dx12PresentHookData::OpenFileMappingAFn>(
            kernel32->GetProcAddress("OpenFileMappingA"));
        hookData.pMapViewOfFile = reinterpret_cast<Dx12PresentHookData::MapViewOfFileFn>(
            kernel32->GetProcAddress("MapViewOfFile"));
        hookData.pUnmapViewOfFile = reinterpret_cast<Dx12PresentHookData::UnmapViewOfFileFn>(
            kernel32->GetProcAddress("UnmapViewOfFile"));
        hookData.pCloseHandle = reinterpret_cast<Dx12PresentHookData::CloseHandleFn>(
            kernel32->GetProcAddress("CloseHandle"));
        hookData.pCreateEventW = reinterpret_cast<Dx12PresentHookData::CreateEventWFn>(
            kernel32->GetProcAddress("CreateEventW"));
        hookData.pWaitForSingleObject = reinterpret_cast<Dx12PresentHookData::WaitForSingleObjectFn>(
            kernel32->GetProcAddress("WaitForSingleObject"));
        hookData.pSleep = reinterpret_cast<Dx12PresentHookData::SleepFn>(
            kernel32->GetProcAddress("Sleep"));
        hookData.pSerializeRootSignature = reinterpret_cast<Dx12PresentHookData::SerializeRootSignatureFn>(
            d3d12->GetProcAddress("D3D12SerializeRootSignature"));
        if (!hookData.pOpenFileMappingA || !hookData.pMapViewOfFile ||
            !hookData.pUnmapViewOfFile || !hookData.pCloseHandle ||
            !hookData.pCreateEventW || !hookData.pWaitForSingleObject || !hookData.pSleep ||
            !hookData.pSerializeRootSignature) {
            std::printf("[DX12 Present] Missing target API export needed by the copied hook.\n");
            return false;
        }

        char handshakeName[128]{};
        sprintf_s(handshakeName, "Local\\LiquidOverlayHandshake_DX12_%lu",
            static_cast<unsigned long>(hookData.targetPid));
        strcpy_s(hookData.handshakeName, handshakeName);

        static constexpr char shaderSource[] = R"(
Texture2D overlayTexture : register(t0);
SamplerState overlaySampler : register(s0);
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VertexOutput VSMain(uint vertexId : SV_VertexID) {
    float2 positions[3] = { float2(-1.0, 1.0), float2(3.0, 1.0), float2(-1.0, -3.0) };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = float2((positions[vertexId].x + 1.0) * 0.5, (1.0 - positions[vertexId].y) * 0.5);
    return output;
}
float4 PSMain(VertexOutput input) : SV_Target0 { return overlayTexture.Sample(overlaySampler, input.uv); }
)";
        ID3DBlob* vertexShader = nullptr;
        ID3DBlob* pixelShader = nullptr;
        ID3DBlob* errors = nullptr;
        const UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
        HRESULT hr = D3DCompile(shaderSource, sizeof(shaderSource) - 1, nullptr, nullptr, nullptr,
            "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errors);
        if (FAILED(hr) || !vertexShader) {
            if (errors) errors->Release();
            if (vertexShader) vertexShader->Release();
            std::printf("[DX12 Present] Vertex shader compilation failed: 0x%08X\n", hr);
            return false;
        }
        if (errors) { errors->Release(); errors = nullptr; }
        hr = D3DCompile(shaderSource, sizeof(shaderSource) - 1, nullptr, nullptr, nullptr,
            "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errors);
        if (FAILED(hr) || !pixelShader) {
            if (errors) errors->Release();
            vertexShader->Release();
            if (pixelShader) pixelShader->Release();
            std::printf("[DX12 Present] Pixel shader compilation failed: 0x%08X\n", hr);
            return false;
        }
        if (errors) errors->Release();
        if (vertexShader->GetBufferSize() > sizeof(hookData.vertexShader) ||
            pixelShader->GetBufferSize() > sizeof(hookData.pixelShader)) {
            vertexShader->Release();
            pixelShader->Release();
            std::printf("[DX12 Present] Compiled shader bytecode exceeds hook data capacity.\n");
            return false;
        }
        memcpy(hookData.vertexShader, vertexShader->GetBufferPointer(), vertexShader->GetBufferSize());
        hookData.vertexShaderSize = vertexShader->GetBufferSize();
        memcpy(hookData.pixelShader, pixelShader->GetBufferPointer(), pixelShader->GetBufferSize());
        hookData.pixelShaderSize = pixelShader->GetBufferSize();
        vertexShader->Release();
        pixelShader->Release();

        const uintptr_t swapChainAddress = reinterpret_cast<uintptr_t>(context.swapChain);
        const uintptr_t presentAddress = LiquidHookEx::proc->GetVTableFunction<8>(swapChainAddress);
        if (!presentAddress) {
            std::printf("[DX12 Present] Couldn't read Present slot from target swap chain.\n");
            return false;
        }
        const uintptr_t executeAddress = LiquidHookEx::proc->GetVTableFunction<10>(
            reinterpret_cast<uintptr_t>(context.commandQueue));
        if (!executeAddress) {
            std::printf("[DX12 Present] Couldn't read ID3D12CommandQueue::ExecuteCommandLists.\n");
            return false;
        }

        Dx12ExecuteHookData executeHookData{};
        bool executeHooked = m_ExecuteHook.HookUsingAddr<Dx12ExecuteHookData>(
            executeAddress, "D3D12Core.dll", executeHookData,
            reinterpret_cast<void*>(DX12PresentShellcode::HookExecuteCommandLists),
            reinterpret_cast<void*>(DX12PresentShellcode::HookExecuteCommandListsEnd),
            {
                LiquidHookEx::VTable::RipSlot::Data(&g_dx12ExecuteHookData),
                LiquidHookEx::VTable::RipSlot::Orig(&g_dx12OriginalExecuteCommandLists),
            });
        if (!executeHooked) {
            executeHooked = m_ExecuteHook.HookUsingAddr<Dx12ExecuteHookData>(
                executeAddress, "D3D12.dll", executeHookData,
                reinterpret_cast<void*>(DX12PresentShellcode::HookExecuteCommandLists),
                reinterpret_cast<void*>(DX12PresentShellcode::HookExecuteCommandListsEnd),
                {
                    LiquidHookEx::VTable::RipSlot::Data(&g_dx12ExecuteHookData),
                    LiquidHookEx::VTable::RipSlot::Orig(&g_dx12OriginalExecuteCommandLists),
                });
        }
        if (!executeHooked) {
            std::printf("[DX12 Present] Couldn't hook ExecuteCommandLists to capture the game's direct queue.\n");
            return false;
        }
        hookData.executeHookData = static_cast<Dx12ExecuteHookData*>(m_ExecuteHook.GetDataRemote());

        if (!m_Hook.HookUsingAddr<Dx12PresentHookData>(
            presentAddress,
            "DXGI.DLL",
            hookData,
            reinterpret_cast<void*>(DX12PresentShellcode::HookPresent),
            reinterpret_cast<void*>(DX12PresentShellcode::HookPresentEnd),
            {
                LiquidHookEx::VTable::RipSlot::Data(&g_dx12PresentData),
                LiquidHookEx::VTable::RipSlot::Orig(&g_dx12OriginalPresent),
            })) {
            m_ExecuteHook.Unhook();
            return false;
        }
        return true;
    }

    bool PresentDx12::Unhook() {
        if (!m_Hook.IsHooked())
            return false;

        m_Hook.WriteField<LONG>(offsetof(Dx12PresentHookData, initState), -1);
        if (m_ExecuteHook.IsHooked() && !m_ExecuteHook.Unhook())
            return false;
        if (!m_Hook.Unhook())
            return false;

        Dx12PresentHookData cleanupData{};
        bool callbackIdle = false;
        for (UINT attempt = 0; attempt < 5000; ++attempt) {
            cleanupData = m_Hook.ReadData<Dx12PresentHookData>();
            if (InterlockedCompareExchange(&cleanupData.hookBusy, 0, 0) == 0) {
                callbackIdle = true;
                break;
            }
            Sleep(1);
        }
        if (!callbackIdle) {
            std::printf("[DX12 Present] Timed out waiting for the target callback to become idle.\n");
            return false;
        }
        Sleep(10);
        cleanupData = m_Hook.ReadData<Dx12PresentHookData>();

        const DWORD cleanupResult = LiquidHookEx::proc->ExecuteRemoteWrapper(
            reinterpret_cast<void*>(DX12PresentShellcode::CleanupRemote),
            reinterpret_cast<void*>(DX12PresentShellcode::CleanupRemoteEnd),
            cleanupData, 5000);
        if (cleanupResult != 0) {
            std::printf("[DX12 Present] Target-side resource cleanup failed.\n");
            return false;
        }
        return true;
    }

}
