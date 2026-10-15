#include "render/Renderer.h"

#include "core/Log.h"
#include "core/Module.h"
#include "core/Patch.h"
#include "render/DirectDraw.h"
#include "render/Surface.h"
#include "render/TexturePack.h"


namespace bw::render {

namespace {

bool g_active = false;
gpu::Options g_options;
DirectDrawCreateExFunction g_realDirectDrawCreateEx = nullptr;

HRESULT WINAPI DirectDrawCreateExReplacement(GUID* guid, void** out, const IID& iid, IUnknown* /*outer*/)
{
    return DirectDraw::Create(guid, iid, out);
}

}  // namespace

void Install(HMODULE game, const Config& config) noexcept
{
    if (config.renderer != Renderer::Direct3D9) {
        return;
    }
    if (!gpu::Available()) {
        log::Warning("Renderer: Direct3D 9Ex is not available; keeping the original renderer");
        return;
    }
    g_options.vsync = config.vsync;
    g_options.anisotropy = config.anisotropy;
    g_options.multisample = config.antiAliasing;
    g_options.fullPrecisionTextures = config.fullPrecisionTextures;
    g_options.keepAspectRatio = config.keepAspectRatio;
    g_realDirectDrawCreateEx = patch::ReplaceImport(game, "DDRAW.dll", "DirectDrawCreateEx", &DirectDrawCreateExReplacement);
    if (g_realDirectDrawCreateEx == nullptr) {
        log::Error("Renderer: DirectDrawCreateEx is not imported; keeping the original renderer");
        return;
    }
    g_active = true;

    textures::Initialise({config.dumpTextures, config.replaceTextures, ModuleDirectory(game) / L"Textures"});
    log::Info("Renderer: Direct3D 9 (vsync {}, anisotropy {}, anti-aliasing {}, full-precision textures {})",
              config.vsync ? "on" : "off", config.anisotropy, config.antiAliasing,
              config.fullPrecisionTextures ? "on" : "off");
}

void NameTexture(IUnknown* surface, std::string_view source) noexcept
{
    if (!g_active || surface == nullptr) {
        return;
    }
    try {
        // Every surface the game holds was created by this renderer as an IDirectDrawSurface7.
        Surface::From(static_cast<IDirectDrawSurface7*>(surface))->SetSourceName(textures::NormaliseName(source));
    }
    catch (const std::bad_alloc&) {
    }
}

bool Active() noexcept
{
    return g_active;
}

const gpu::Options& Options() noexcept
{
    return g_options;
}

DirectDrawCreateExFunction RealDirectDrawCreateEx() noexcept
{
    return g_realDirectDrawCreateEx;
}

}  // namespace bw::render
