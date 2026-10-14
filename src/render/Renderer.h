#pragma once

// The Direct3D 9 renderer ([Display] Renderer=1): DirectDrawCreateEx is answered with this project's DirectDraw and
// Direct3D 7 objects (render/DirectDraw.h, Surface.h, Device.h), which draw through Direct3D 9Ex (render/Gpu.h).
// See docs/RENDERER.md.

#include "core/Config.h"
#include "core/Windows.h"
#include "render/Gpu.h"

#include <unknwn.h>

#include <string_view>

namespace bw::render {

void Install(HMODULE game, const Config& config) noexcept;

/// True when the Direct3D 9 renderer replaces DirectDraw.
[[nodiscard]] bool Active() noexcept;

[[nodiscard]] const gpu::Options& Options() noexcept;

/// Tells the texture pack (render/TexturePack.h) that the game loaded the image `source` (a file path or a name such
/// as "segments\1a3") into `surface`, a DirectDraw surface of this renderer. No effect when the renderer is off.
void NameTexture(IUnknown* surface, std::string_view source) noexcept;

using DirectDrawCreateExFunction = HRESULT(WINAPI*)(GUID* guid, void** out, const IID& iid, IUnknown* outer);
/// ddraw.dll's DirectDrawCreateEx, for the real object kept for hardware queries.
[[nodiscard]] DirectDrawCreateExFunction RealDirectDrawCreateEx() noexcept;

}  // namespace bw::render
