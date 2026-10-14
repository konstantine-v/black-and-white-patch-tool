#pragma once

#include <cstddef>
#include <cstdint>

namespace bw {

/// An absolute virtual address inside the original runblack.exe image.
///
/// The executable is linked at 0x00400000 with relocations stripped (IMAGE_FILE_RELOCS_STRIPPED), so Windows
/// always maps it there and every address in symbols/functions.csv is valid at run time without rebasing.
using Address = std::uintptr_t;

inline constexpr Address kImageBase = 0x0040'0000;

/// A typed reference to a global variable that still lives in the original image.
template <typename T>
[[nodiscard]] inline T& GlobalRef(Address address) noexcept
{
    return *reinterpret_cast<T*>(address);
}

}  // namespace bw

/// Declares `name` as a typed reference to an original global variable:
///     BW_GLOBAL(std::int32_t, g_lowVideoMemory, 0x00df820c);
#define BW_GLOBAL(Type, name, address) inline Type& name = ::bw::GlobalRef<Type>(address)

/// Declares `name` as a reference to the original image's import-address-table slot for `function`, so decompiled
/// code calls Windows exactly as the game does (and through any import patches installed by fixes):
///     BW_IMPORT(DirectDrawEnumerateA, s_DirectDrawEnumerateA, 0x007eb02c);
#define BW_IMPORT(function, name, slotAddress) \
    inline decltype(&::function)& name = ::bw::GlobalRef<decltype(&::function)>(slotAddress)

/// Declares `name()` as an accessor for a variable another DLL exports to the game (the IAT slot holds its address).
/// A function rather than a reference: the slot is only filled once the loader has resolved the game's imports.
///     BW_IMPORTED_DATA(std::int32_t, LHAssertIgnoreAllAsserts, 0x007eb32c);
#define BW_IMPORTED_DATA(Type, name, slotAddress) \
    [[nodiscard]] inline Type& name() noexcept { return *::bw::GlobalRef<Type*>(slotAddress); }
