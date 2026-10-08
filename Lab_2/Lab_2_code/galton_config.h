#pragma once
#include <stdbool.h>

// Rebuild after changing this. false removes all timing instrumentation and HUD.
// Shared by the application and VGA driver so both compile the same setting.
#ifndef GALTON_TIMING_DISPLAY
#define GALTON_TIMING_DISPLAY true
#endif
