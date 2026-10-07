#pragma once

#include "CoreMinimal.h"

// Full-screen level-loading screen. The menu's FLY action calls this just
// before OpenLevel so the viewport stays covered (the main window stays
// hidden) until the chosen world has finished loading — the loading screen is
// the only full-bleed surface; the startup splash is a small centred logo.
BORN2FLAP_API void Born2FlapShowLoadingScreen();