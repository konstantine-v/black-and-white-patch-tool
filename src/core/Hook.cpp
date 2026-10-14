#include "core/Hook.h"

#include "core/Log.h"
#include "core/Patch.h"

#include <algorithm>
#include <vector>

namespace bw {

namespace {

// Function-local static: registrations run during static initialisation of other translation units.
std::vector<HookDescriptor>& Registry() noexcept
{
    static std::vector<HookDescriptor> registry;
    return registry;
}

bool g_sorted = false;

}  // namespace

std::string_view ToString(CallingConvention convention) noexcept
{
    switch (convention) {
    case CallingConvention::Cdecl: return "cdecl";
    case CallingConvention::Stdcall: return "stdcall";
    case CallingConvention::Thiscall: return "thiscall";
    }
    return "?";
}

void HookRegistry::Register(const HookDescriptor& descriptor) noexcept
{
    try {
        Registry().push_back(descriptor);
        g_sorted = false;
    }
    catch (const std::bad_alloc&) {
        // Registration happens at DLL start-up; running out of memory there is unrecoverable.
        std::terminate();
    }
}

std::span<const HookDescriptor> HookRegistry::All() noexcept
{
    auto& registry = Registry();
    if (!g_sorted) {
        std::ranges::sort(registry, {}, &HookDescriptor::original);
        g_sorted = true;
    }
    return registry;
}

HookInstallReport HookRegistry::InstallAll(Address textBegin, Address textEnd, std::span<const Address> disabled) noexcept
{
    HookInstallReport report;
    const auto hooks = All();

    for (std::size_t i = 0; i + 1 < hooks.size(); ++i) {
        if (hooks[i].original == hooks[i + 1].original) {
            log::Error("Duplicate hooks for 0x{:08x}: {} and {}; no hooks installed", hooks[i].original,
                       hooks[i].name, hooks[i + 1].name);
            report.rejected = hooks.size();
            return report;
        }
    }

    const patch::ScopedProtection writable(textBegin, textEnd - textBegin, patch::Protection::ReadWriteExecute);
    if (!writable) {
        log::Error("Cannot make .text writable; no hooks installed");
        report.rejected = hooks.size();
        return report;
    }

    for (const HookDescriptor& hook : hooks) {
        if (std::ranges::find(disabled, hook.original) != disabled.end()) {
            log::Info("Hook disabled by configuration: 0x{:08x} {}", hook.original, hook.name);
            ++report.disabled;
            continue;
        }
        if (hook.original < textBegin || hook.original + patch::kJumpSize > textEnd) {
            log::Error("Hook outside .text rejected: 0x{:08x} {}", hook.original, hook.name);
            ++report.rejected;
            continue;
        }
        patch::WriteJump(hook.original, hook.replacement);
        log::Debug("Hooked 0x{:08x} {} ({}, ret {})", hook.original, hook.name, ToString(hook.convention),
                   hook.calleeStackBytes);
        ++report.installed;
    }
    patch::FlushCode(textBegin, textEnd - textBegin);
    return report;
}

}  // namespace bw
