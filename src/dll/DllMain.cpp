// BWDecomp.dll: loaded into runblack.exe by BWLaunch.exe before the game's entry point runs. Verifies the image,
// applies the fixes and redirects every decompiled function from the original code to this DLL.

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Image.h"
#include "core/Log.h"
#include "core/Module.h"
#include "core/Version.h"
#include "core/Windows.h"
#include "diagnostics/Diagnostics.h"
#include "diagnostics/DirectXTrace.h"
#include "fixes/Fixes.h"
#include "render/Renderer.h"

#include <filesystem>

namespace {

bool HostIsNamedRunblack(HMODULE game)
{
    return _wcsicmp(bw::ModulePath(game).filename().c_str(), L"runblack.exe") == 0;
}

/// Returns false when the DLL must refuse to load (wrong game build or hooks rejected).
bool Attach(HMODULE self)
{
    const HMODULE game = GetModuleHandleW(nullptr);
    const std::filesystem::path directory = bw::ModuleDirectory(self);

    bw::Config config = bw::Config::Load(directory / L"BWDecomp.ini");
    bw::log::Open(directory / L"BWDecomp.log", config.logDebug);
    bw::SetConfig(config);
    bw::log::Info("BWDecomp {}", bw::kVersion);

    if (!bw::image::IsSupportedGame(game)) {
        if (HostIsNamedRunblack(game)) {
            bw::log::Error("runblack.exe is not the supported build (see README: supported executable); refusing to "
                           "patch it");
            return false;
        }
        // Loaded by a tool or test host: stay inert.
        bw::log::Info("Host is not runblack.exe; nothing installed");
        return true;
    }

    bw::diagnostics::Install(game, bw::GetConfig());
    bw::render::Install(game, bw::GetConfig());
    if (bw::GetConfig().traceDirectX) {
        if (bw::render::Active()) {
            bw::log::Info("TraceDirectX applies to the original renderer only; ignored");
        }
        else {
            bw::diagnostics::InstallDirectXTrace(game, directory / L"BWDecomp.directx.csv");
        }
    }
    bw::fixes::InstallAll(game, bw::GetConfig());

    const std::optional<bw::image::Section> text = bw::image::FindSection(game, ".text");
    if (!text) {
        return false;  // IsSupportedGame hashed .text, so this cannot happen
    }
    const bw::HookInstallReport report =
        bw::HookRegistry::InstallAll(text->begin, text->end, bw::GetConfig().disabledHooks);
    bw::log::Info("Decompiled functions: {} active, {} disabled, {} rejected", report.installed, report.disabled,
                  report.rejected);
    return report.rejected == 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_DETACH) {
        bw::diagnostics::WriteDirectXTrace();
        return TRUE;
    }
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(module);
    try {
        return Attach(module) ? TRUE : FALSE;
    }
    catch (...) {
        return FALSE;
    }
}
