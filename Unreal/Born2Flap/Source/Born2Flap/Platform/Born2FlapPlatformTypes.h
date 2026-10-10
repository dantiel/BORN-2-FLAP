#pragma once

#include "CoreMinimal.h"

// Platform integration types for the Born2Flap core module (MIT-clean).
// No store SDK is referenced here: store backends arrive via the optional
// Born2FlapOnline plugin and implement FB2FPlatformBackend (see
// Born2FlapPlatformBackend.h). The canonical catalog lives in
// Brain/lib/born2flap/achievements/achievements.json; the generated header
// Born2FlapAchievements.h (M2) mirrors this contract: enum + catalog table.

// Achievement identifiers. ApiName maps 1:1 to store achievement API names;
// I18nKey points into the locale tables (ach.<key> / ach.<key>.desc).
UENUM()
enum class EB2FAchievementId : uint8
{
    None = 0,

    // Onboarding & flight
    FirstHandLaunch,   // First hand launch ever
    HighFlier,         // Altitude above 200 m
    LongGlide,         // Single flight of 60+ seconds
    CrashLanding,      // Hard impact while airborne

    // Gate-race course (Training)
    GateNovice,        // First gate passed
    CourseClear,       // Full gate course cleared once
    HotLap,            // Best lap under 60 s
    SpeedDemon,        // Best lap under 30 s
    GateMarathon,      // Gate course cleared 5 times

    // World & environment
    WaterLanding,      // First water landing
    FieldReturn,       // First boundary return
    Boomerang,         // 10 boundary returns total
    SafetyNet,         // Flight safety reset triggered
    RadioWave,         // Radio station tuned
    PoiTourist,        // First point of interest visited
    PoiCartographer,   // All POIs of a level visited

    // RC controller
    RcPilot,           // First flight steered with an RC controller
    RcAce,             // 10+ minutes of RC-steered flight total

    // Levels
    NatureExplorer,    // Nature level reached
    CoastExplorer,     // Coast level reached

    // Meta
    Marathon,          // 1+ hour of flight time total
    Completionist,     // All other achievements unlocked

    Count UMETA(Hidden)
};

// Named cloud-save slots mirrored to the platform store when available.
// Local fallbacks (GConfig/ini, Saved/Racing) remain intact for the MIT build.
enum class EB2FCloudSlot : uint8
{
    FlightSettings,   // Flight/tuning settings (FlightSettings ini mirror)
    RcProfiles,       // RC transmitter profiles & calibration
    ChampionSpirit,   // Best race spirit metadata

    Count UMETA(Hidden)
};

BORN2FLAP_API const TCHAR* B2FCloudSlotName(EB2FCloudSlot Slot);

// Range validation: reject None/Count sentinels and out-of-range casts so
// gameplay code can never hand an invalid id/slot to a store backend.
BORN2FLAP_API bool B2FIsValidAchievement(EB2FAchievementId Id);
BORN2FLAP_API bool B2FIsValidCloudSlot(EB2FCloudSlot Slot);

// Achievement metadata: store API name, human-readable unlock rule, i18n key
// (name = ach.<key>, description = ach.<key>.desc), hidden flag.
struct FB2FAchievementDef
{
    EB2FAchievementId Id;
    const TCHAR* ApiName;
    const TCHAR* UnlockRule;
    const TCHAR* I18nKey;
    bool bHidden;
};

// Canonical achievement catalog (static, read-only).
BORN2FLAP_API const TArray<FB2FAchievementDef>& B2FAchievementCatalog();

#include "Born2FlapPlatformTypes.generated.h"