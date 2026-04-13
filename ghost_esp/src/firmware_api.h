#pragma once
#include <furi.h>
#include <furi_hal_version.h>

// Check if we're building for Moon firmware
#if defined(FW_ORIGIN_Moon)
#include <moon/moon.h>
#define HAS_MOMENTUM_SUPPORT 1
#endif
