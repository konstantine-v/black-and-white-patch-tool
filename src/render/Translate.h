#pragma once

// Direct3D 7 -> Direct3D 9 translation of states, transforms and vertex formats. Pure functions over numbers so they
// can be tested and used from either side of the renderer (ddraw.h and d3d9.h cannot share a translation unit).

#include <array>
#include <cstdint>

namespace bw::render {

/// Direct3D 9 values used by the translation (from d3d9types.h).
namespace d3d9 {
inline constexpr std::uint32_t kRsZEnable = 7;
inline constexpr std::uint32_t kRsLighting = 137;
inline constexpr std::uint32_t kRsDepthBias = 195;
inline constexpr std::uint32_t kSampAddressU = 1;
inline constexpr std::uint32_t kSampAddressV = 2;
inline constexpr std::uint32_t kSampBorderColor = 4;
inline constexpr std::uint32_t kSampMagFilter = 5;
inline constexpr std::uint32_t kSampMinFilter = 6;
inline constexpr std::uint32_t kSampMipFilter = 7;
inline constexpr std::uint32_t kSampMipMapLodBias = 8;
inline constexpr std::uint32_t kSampMaxMipLevel = 9;
inline constexpr std::uint32_t kSampMaxAnisotropy = 10;
inline constexpr std::uint32_t kTexfNone = 0;
inline constexpr std::uint32_t kTexfPoint = 1;
inline constexpr std::uint32_t kTexfLinear = 2;
inline constexpr std::uint32_t kTexfAnisotropic = 3;
inline constexpr std::uint32_t kTsWorld = 256;
}  // namespace d3d9

/// One Direct3D 9 state write produced by a Direct3D 7 state.
struct StateWrite {
    enum class Kind : std::uint8_t { None, RenderState, SamplerState, TextureStageState };
    Kind kind = Kind::None;
    std::uint32_t state = 0;
    std::uint32_t value = 0;

    friend constexpr bool operator==(const StateWrite&, const StateWrite&) = default;
};

/// Up to two writes (D3DTSS_ADDRESS sets U and V); unused entries have Kind::None.
using StateWrites = std::array<StateWrite, 2>;

/// A D3DRENDERSTATE_* value as Direct3D 9 render states. Retired states (TEXTUREPERSPECTIVE, ANTIALIAS, LINEPATTERN,
/// ZVISIBLE, ...) produce nothing; ZBIAS becomes DEPTHBIAS.
[[nodiscard]] StateWrites TranslateRenderState(std::uint32_t state, std::uint32_t value) noexcept;

/// A D3DTSS_* value as Direct3D 9 texture-stage or sampler states (addressing and filtering moved to samplers, with
/// the Direct3D 7 filter enums mapped to D3DTEXF_*).
[[nodiscard]] StateWrites TranslateTextureStageState(std::uint32_t type, std::uint32_t value) noexcept;

/// D3DTRANSFORMSTATE_* as D3DTRANSFORMSTATETYPE, or 0 for an unknown value.
[[nodiscard]] std::uint32_t TranslateTransform(std::uint32_t state) noexcept;

/// True for render states that exist in Direct3D 7 but have no effect on a Direct3D 7 device or in Direct3D 9.
[[nodiscard]] bool IsRetiredRenderState(std::uint32_t state) noexcept;

/// Size in bytes of a vertex in flexible vertex format `fvf` (identical bit layout in Direct3D 7 and 9; Direct3D 7's
/// D3DFVF_RESERVED1 occupies the slot of Direct3D 9's PSIZE, 4 bytes).
[[nodiscard]] std::uint32_t VertexStride(std::uint32_t fvf) noexcept;

/// True when lighting must be off for this format: Direct3D 7 does not light vertices without a normal (pre-lit
/// D3DLVERTEX), Direct3D 9 lights them to the material colour.
[[nodiscard]] bool IsUnlitFormat(std::uint32_t fvf) noexcept;

/// Primitives drawn from `count` vertices or indices of primitive type `type` (D3DPRIMITIVETYPE, same values in both).
[[nodiscard]] std::uint32_t PrimitiveCount(std::uint32_t type, std::uint32_t count) noexcept;

}  // namespace bw::render
