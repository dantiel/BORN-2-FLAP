// Born2FlapPoi.h — points of interest per level.
//
// A POI is a named, selectable reset/launch point. Each level (Ravenstonefield,
// Shiomori, Training) contributes its own list: a launch point plus named
// landmarks. The player cycles the selection (the "[" / "]" keys) and the R
// reset key re-places the bird at the chosen POI instead of the world origin.
//
// Key is a semantic i18n key ("poi.raven_watch"); Position is the absolute
// world spawn location in centimetres (already at ground + clearance, so the
// existing hand-launch mechanic works unchanged).

#pragma once

#include "CoreMinimal.h"

struct FBorn2FlapPoi
{
    FString Key;      // semantic i18n key, e.g. "poi.raven_watch"
    FVector Position; // absolute spawn location, cm (ground + clearance baked in)
    float Yaw = 0.f;  // spawn heading, degrees
    int32 Number = 0; // optional 1-based suffix ("GATE 3"), 0 = none
};