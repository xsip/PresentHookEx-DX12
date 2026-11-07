#pragma once

#include <cstdint>
#include <type_traits>

namespace DX12Overlay {

    inline constexpr std::uint32_t HANDSHAKE_MAGIC = 0x324F4C44; // "DLO2"
    inline constexpr std::uint16_t HANDSHAKE_VERSION = 1;
    inline constexpr std::uint32_t API_DIRECT3D12 = 12;

    enum class HandshakeState : std::uint32_t {
        Empty = 0,
        Ready = 1,
        Stopping = 2,
        Closed = 3,
    };

    // Values in resourceHandle and fenceHandle are HANDLE values duplicated
    // into targetPid's handle table. They are numeric wire values here; a
    // consumer must use them only from that target process.
    struct Handshake {
        std::uint32_t magic = HANDSHAKE_MAGIC;
        std::uint16_t version = HANDSHAKE_VERSION;
        std::uint16_t headerSize = 0;
        std::uint32_t api = API_DIRECT3D12;
        std::uint32_t state = static_cast<std::uint32_t>(HandshakeState::Empty);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t format = 0;
        std::uint32_t producerPid = 0;
        std::uint32_t targetPid = 0;
        std::uint32_t adapterLuidLow = 0;
        std::uint32_t adapterLuidHigh = 0;
        std::uint64_t generation = 0;
        std::uint64_t resourceHandle = 0;
        std::uint64_t fenceHandle = 0;
        std::uint64_t producerFenceValue = 0;
        std::uint64_t consumerFenceValue = 0;
    };

    // For frame N (zero-based), producer signals 2*N+1 after rendering and
    // transitioning the overlay to PIXEL_SHADER_RESOURCE. Consumer waits for
    // 2*N+1, composites, then signals 2*N+2. Producer waits for 2*N+2 before
    // reusing the shared texture. Initial fence value zero makes frame zero
    // immediately available to the producer.
    inline constexpr std::uint64_t ProducerReadyValue(std::uint64_t frame) noexcept {
        return frame * 2 + 1;
    }
    inline constexpr std::uint64_t ConsumerDoneValue(std::uint64_t frame) noexcept {
        return frame * 2 + 2;
    }

    static_assert(std::is_standard_layout_v<Handshake>);
    static_assert(std::is_trivially_copyable_v<Handshake>);

}
