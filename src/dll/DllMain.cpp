// BWDecomp.dll: loaded into runblack.exe by BWLaunch.exe before the game's entry point runs. Verifies the image,
// applies the fixes and redirects every decompiled function from the original code to this DLL.

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Image.h"
#include "core/Log.h"
#include "core/Version.h"
#include "core/Windows.h"
#include "fixes/Fixes.h"

#include <filesystem>

namespace {

std::filesystem::path ModuleDirectory(HMODULE module)
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    return std::filesystem::path(path).parent_path();
}

bool HostIsNamedRunblack(HMODULE game)
{
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(game, path.data(), static_cast<DWORD>(path.size())));
    return _wcsicmp(std::filesystem::path(path).filename().c_str(), L"runblack.exe") == 0;
}

/// Returns false when the DLL must refuse to load (wrong game build or hooks rejected).
bool Attach(HMODULE self)
{
    const HMODULE game = GetModuleHandleW(nullptr);
    const std::filesystem::path directory = ModuleDirectory(self);

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

    bw::fixes::InstallAll(game, bw::GetConfig());

    const std::optional<bw::image::Section> text = bw::image::FindSection(game, ".text");
    const bw::HookInstallReport report =
        bw::HookRegistry::InstallAll(text->begin, text->end, bw::GetConfig().disabledHooks);
    bw::log::Info("Decompiled functions: {} active, {} disabled, {} rejected", report.installed, report.disabled,
                  report.rejected);
    return report.rejected == 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
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
