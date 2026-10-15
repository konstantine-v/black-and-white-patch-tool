#pragma once

#include "core/Windows.h"

#include <filesystem>
#include <string>

namespace bw {

/// The full path of `module` (nullptr: the process's executable), however long it is.
[[nodiscard]] inline std::filesystem::path ModulePath(HMODULE module)
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);  // truncated: GetModuleFileNameW filled the whole buffer
    }
}

/// The folder of `module` (nullptr: the process's executable, i.e. the game folder inside the game).
[[nodiscard]] inline std::filesystem::path ModuleDirectory(HMODULE module)
{
    return ModulePath(module).parent_path();
}

}  // namespace bw
