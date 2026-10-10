#include "Platform/Born2FlapPlatformTypes.h"

const TCHAR* B2FCloudSlotName(EB2FCloudSlot Slot)
{
    switch (Slot)
    {
    case EB2FCloudSlot::FlightSettings: return TEXT("flight_settings");
    case EB2FCloudSlot::RcProfiles:     return TEXT("rc_profiles");
    case EB2FCloudSlot::ChampionSpirit: return TEXT("champion_spirit");
    default:                            return TEXT("");
    }
}

bool B2FIsValidAchievement(EB2FAchievementId Id)
{
    // Bounds-check the uint8 range: only the cataloged ids between the None and
    // Count sentinels are valid. Out-of-range casts (e.g. 255) are rejected.
    return static_cast<uint8>(Id) > static_cast<uint8>(EB2FAchievementId::None)
        && static_cast<uint8>(Id) < static_cast<uint8>(EB2FAchievementId::Count);
}

bool B2FIsValidCloudSlot(EB2FCloudSlot Slot)
{
    // First valid slot is 0 (no None sentinel); Count bounds the upper end.
    return static_cast<uint8>(Slot) < static_cast<uint8>(EB2FCloudSlot::Count);
}

const TArray<FB2FAchievementDef>& B2FAchievementCatalog()
{
    static const TArray<FB2FAchievementDef> Catalog = {
        { EB2FAchievementId::FirstHandLaunch, TEXT("ACH_FIRST_HAND_LAUNCH"), TEXT("Perform your first hand launch"), TEXT("ach.first_hand_launch"), false },
        { EB2FAchievementId::HighFlier,       TEXT("ACH_HIGH_FLIER"),        TEXT("Reach an altitude of 200 m"), TEXT("ach.high_flier"), false },
        { EB2FAchievementId::LongGlide,       TEXT("ACH_LONG_GLIDE"),        TEXT("Stay airborne for 60 s in one flight"), TEXT("ach.long_glide"), false },
        { EB2FAchievementId::CrashLanding,    TEXT("ACH_CRASH_LANDING"),     TEXT("Survive a hard impact while airborne"), TEXT("ach.crash_landing"), false },
        { EB2FAchievementId::GateNovice,      TEXT("ACH_GATE_NOVICE"),       TEXT("Pass your first flight gate"), TEXT("ach.gate_novice"), false },
        { EB2FAchievementId::CourseClear,     TEXT("ACH_COURSE_CLEAR"),      TEXT("Clear the full gate course"), TEXT("ach.course_clear"), false },
        { EB2FAchievementId::HotLap,          TEXT("ACH_HOT_LAP"),           TEXT("Set a best lap under 60 s"), TEXT("ach.hot_lap"), false },
        { EB2FAchievementId::SpeedDemon,      TEXT("ACH_SPEED_DEMON"),       TEXT("Set a best lap under 30 s"), TEXT("ach.speed_demon"), false },
        { EB2FAchievementId::GateMarathon,    TEXT("ACH_GATE_MARATHON"),     TEXT("Clear the gate course 5 times"), TEXT("ach.gate_marathon"), false },
        { EB2FAchievementId::WaterLanding,    TEXT("ACH_WATER_LANDING"),     TEXT("Make your first water landing"), TEXT("ach.water_landing"), false },
        { EB2FAchievementId::FieldReturn,     TEXT("ACH_FIELD_RETURN"),      TEXT("Reach the edge of the field"), TEXT("ach.field_return"), false },
        { EB2FAchievementId::Boomerang,       TEXT("ACH_BOOMERANG"),         TEXT("Reach the field edge 10 times"), TEXT("ach.boomerang"), false },
        { EB2FAchievementId::SafetyNet,       TEXT("ACH_SAFETY_NET"),        TEXT("Trigger the flight safety reset"), TEXT("ach.safety_net"), false },
        { EB2FAchievementId::RadioWave,       TEXT("ACH_RADIO_WAVE"),        TEXT("Tune the valley radio station"), TEXT("ach.radio_wave"), false },
        { EB2FAchievementId::PoiTourist,      TEXT("ACH_POI_TOURIST"),       TEXT("Visit your first point of interest"), TEXT("ach.poi_tourist"), false },
        { EB2FAchievementId::PoiCartographer, TEXT("ACH_POI_CARTOGRAPHER"),  TEXT("Visit every point of interest on a level"), TEXT("ach.poi_cartographer"), false },
        { EB2FAchievementId::RcPilot,         TEXT("ACH_RC_PILOT"),          TEXT("Fly with an RC controller for the first time"), TEXT("ach.rc_pilot"), false },
        { EB2FAchievementId::RcAce,           TEXT("ACH_RC_ACE"),            TEXT("Log 10 minutes of RC-steered flight"), TEXT("ach.rc_ace"), false },
        { EB2FAchievementId::NatureExplorer,  TEXT("ACH_NATURE_EXPLORER"),   TEXT("Reach the nature level"), TEXT("ach.nature_explorer"), false },
        { EB2FAchievementId::CoastExplorer,   TEXT("ACH_COAST_EXPLORER"),    TEXT("Reach the coast level"), TEXT("ach.coast_explorer"), false },
        { EB2FAchievementId::Marathon,        TEXT("ACH_MARATHON"),          TEXT("Log 1 hour of flight time"), TEXT("ach.marathon"), false },
        { EB2FAchievementId::Completionist,   TEXT("ACH_COMPLETIONIST"),     TEXT("Unlock every other achievement"), TEXT("ach.completionist"), false },
    };
    return Catalog;
}