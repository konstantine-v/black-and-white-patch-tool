#pragma once

// Single place that includes <windows.h>, with the macros that keep it from leaking min/max and rarely used APIs.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
