#pragma once

// The Direct3D 9 renderer ([Display] Renderer=1): DirectDrawCreateEx is answered with this project's DirectDraw and
// Direct3D 7 objects (render/DirectDraw.h, Surface.h, Device.h), which draw through Direct3D 9Ex (render/Gpu.h).
// See docs/RENDERER.md.

#include "core/Config.h"
#include "core/Windows.h"
#include "render/Gpu.h"

#include <unknwn.h>

namespace bw::render {

void Install(HMODULE game, const Config& config) noexcept;

/// True when the Direct3D 9 renderer replaces DirectDraw.
[[nodiscard]] bool Active() noexcept;

[[nodiscard]] const gpu::Options& Options() noexcept;

using DirectDrawCreateExFunction = HRESULT(WINAPI*)(GUID* guid, void** out, const IID& iid, IUnknown* outer);
/// ddraw.dll's DirectDrawCreateEx, for the real object kept for hardware queries.
[[nodiscard]] DirectDrawCreateExFunction RealDirectDrawCreateEx() noexcept;

}  // namespace bw::render
