#pragma once

// Import-address-table slots of runblack.exe used by decompiled code. Calling Windows through the game's own IAT keeps
// behaviour identical to the original (same DLL instance, e.g. a ddraw.dll wrapper in the game folder) and routes
// through any import patch installed by src/fixes.

#include "core/Memory.h"
#include "core/Windows.h"

#include <ddraw.h>

namespace black::imports {

BW_IMPORT(DirectDrawCreateEx, s_DirectDrawCreateEx, 0x007eb028);
BW_IMPORT(DirectDrawEnumerateA, s_DirectDrawEnumerateA, 0x007eb02c);

}  // namespace black::imports
