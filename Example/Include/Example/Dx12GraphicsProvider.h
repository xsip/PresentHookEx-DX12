#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <type_traits>

namespace DX12 {

    struct GraphicsContext {
        HRESULT status = E_PENDING;
        IDXGISwapChain3* swapChain = nullptr;
        ID3D12Device* device = nullptr;
        ID3D12CommandQueue* commandQueue = nullptr;

        bool IsComplete() const noexcept {
            return SUCCEEDED(status) && swapChain != nullptr && device != nullptr && commandQueue != nullptr;
        }
    };

    using AcquireGraphicsContextFn = HRESULT(*)(GraphicsContext& context);
    using ReleaseGraphicsContextFn = void(*)(GraphicsContext& context);

    struct GraphicsProviderCallbacks {
        AcquireGraphicsContextFn acquire = nullptr;
        ReleaseGraphicsContextFn release = nullptr;
    };

    inline GraphicsProviderCallbacks g_graphicsProvider{};

    HRESULT AcquireGraphicsContext(GraphicsContext& context);
    void ReleaseGraphicsContext(GraphicsContext& context);

    inline void RegisterGraphicsProvider(GraphicsProviderCallbacks callbacks) noexcept {
        g_graphicsProvider = callbacks;
    }

    static_assert(std::is_standard_layout_v<GraphicsContext>);
    static_assert(std::is_trivially_copyable_v<GraphicsContext>);

}
