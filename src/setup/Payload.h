#pragma once

#include "setup/Install.h"

#include <cstddef>
#include <span>
#include <vector>

namespace bw::setup {

/// The files setup installs: resources of this executable (BWLaunch.exe, BWDecomp.dll, settings, documentation)
/// and the executable itself as the uninstaller.
class Payload {
public:
    /// Throws SetupError when a resource or the executable cannot be read.
    [[nodiscard]] static Payload Load();

    [[nodiscard]] std::span<const PayloadFile> Files() const noexcept { return m_files; }

    // Moving keeps m_uninstaller's buffer, which m_files points into; copying would not.
    Payload(Payload&&) noexcept = default;
    Payload& operator=(Payload&&) noexcept = default;
    Payload(const Payload&) = delete;
    Payload& operator=(const Payload&) = delete;

private:
    Payload() = default;

    std::vector<std::byte> m_uninstaller;
    std::vector<PayloadFile> m_files;  // spans into resources and m_uninstaller
};

}  // namespace bw::setup
