#pragma once

#include "../Adafruit_GFX.h"

// Native stand-in for Adafruit_GFX's bundled font -- just a dummy instance
// so MenuSystem's setFont(&FreeSans24pt7b) compiles and links. Never
// actually rendered natively (see GFXfont in Adafruit_GFX.h).
inline GFXfont FreeSans24pt7b{};
