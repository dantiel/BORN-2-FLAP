#include "Input/Born2FlapKeybinds.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace
{

FString PreferencesPath()
{
    return FPaths::ProjectSavedDir() / (FParse::Param(FCommandLine::Get(), TEXT("B2FDesktopInputTest"))
        ? TEXT("Automation/FlightPreferences.ini") : TEXT("Config/FlightPreferences.ini"));
}

// Factory defaults — the catalogue itself. Order = display order on the page.
const born2flap::keybinds::FAction DefaultCatalog[] = {
    // ---- FLIGHT ----
    { TEXT("Throttle"),     TEXT("THROTTLE"),          TEXT("FLIGHT"), EKeys::W },
    { TEXT("PitchUp"),      TEXT("PITCH UP"),          TEXT("FLIGHT"), EKeys::Up },
    { TEXT("PitchDown"),    TEXT("PITCH DOWN"),        TEXT("FLIGHT"), EKeys::S },
    { TEXT("PitchDownAlt"), TEXT("PITCH DOWN (ALT)"),  TEXT("FLIGHT"), EKeys::Down },
    { TEXT("RollLeft"),     TEXT("ROLL LEFT"),         TEXT("FLIGHT"), EKeys::Left },
    { TEXT("RollRight"),    TEXT("ROLL RIGHT"),        TEXT("FLIGHT"), EKeys::Right },
    { TEXT("YawLeft"),      TEXT("YAW LEFT"),          TEXT("FLIGHT"), EKeys::A },
    { TEXT("YawRight"),     TEXT("YAW RIGHT"),         TEXT("FLIGHT"), EKeys::D },
    { TEXT("Launch"),       TEXT("LAUNCH"),            TEXT("FLIGHT"), EKeys::SpaceBar },
    { TEXT("Reset"),        TEXT("RESET"),             TEXT("FLIGHT"), EKeys::R },
    { TEXT("ThrottleMode"), TEXT("THROTTLE MODE"),     TEXT("FLIGHT"), EKeys::T },
    // ---- CAMERA ----
    { TEXT("GroundView"),   TEXT("GROUND VIEW"),       TEXT("CAMERA"), EKeys::F6 },
    { TEXT("FpvView"),      TEXT("FPV VIEW"),          TEXT("CAMERA"), EKeys::V },
    { TEXT("FpvAngleDown"), TEXT("FPV ANGLE DOWN"),    TEXT("CAMERA"), EKeys::Q },
    { TEXT("FpvAngleUp"),   TEXT("FPV ANGLE UP"),      TEXT("CAMERA"), EKeys::E },
    { TEXT("FreeCamera"),   TEXT("FREE CAMERA"),       TEXT("CAMERA"), EKeys::MiddleMouseButton },
    // ---- UI ----
    { TEXT("TogglePoi"),    TEXT("POI OVERLAY"),       TEXT("UI"),     EKeys::F8 },
    { TEXT("ToggleMenu"),   TEXT("MAIN MENU"),         TEXT("UI"),     EKeys::F10 },
    { TEXT("ToggleRc"),     TEXT("RC SETTINGS"),       TEXT("UI"),     EKeys::F3 },
    { TEXT("ToggleUi"),     TEXT("HIDE ALL UI"),       TEXT("UI"),     EKeys::F7 },
    { TEXT("ClosePanel"),   TEXT("CLOSE / BACK"),      TEXT("UI"),     EKeys::Escape },
    // ---- DEBUG / HUD ----
    { TEXT("ToggleVectors"), TEXT("VECTORS"),          TEXT("DEBUG"),  EKeys::F1 },
    { TEXT("ToggleChannels"),TEXT("CHANNELS"),         TEXT("DEBUG"),  EKeys::F2 },
    { TEXT("ToggleBlind"),   TEXT("BLIND FLIGHT"),     TEXT("DEBUG"),  EKeys::F5 },
    { TEXT("ToggleMouseInd"),TEXT("MOUSE INDICATOR"),  TEXT("DEBUG"),  EKeys::F9 },
    // ---- AUDIO ----
    { TEXT("RadioMute"),   TEXT("RADIO MUTE"),         TEXT("AUDIO"),  EKeys::M },
    { TEXT("RadioVolDown"),TEXT("RADIO VOLUME DOWN"),  TEXT("AUDIO"),  EKeys::LeftBracket },
    { TEXT("RadioVolUp"),  TEXT("RADIO VOLUME UP"),    TEXT("AUDIO"),  EKeys::RightBracket },
};

TArray<born2flap::keybinds::FAction>& MutableCatalog()
{
    static TArray<born2flap::keybinds::FAction> C;
    if (C.Num() == 0)
    {
        C.Reserve(sizeof(DefaultCatalog) / sizeof(DefaultCatalog[0]));
        for (const auto& D : DefaultCatalog)
            C.Add(D);
    }
    return C;
}

// Persisted overrides: action id → bound key. Only non-default entries live
// here; Get() falls back to the catalogue default when absent.
TMap<FString, FKey>& Overrides()
{
    static TMap<FString, FKey> M;
    return M;
}

bool& Loaded()
{
    static bool b = false;
    return b;
}

void LoadIfNeeded()
{
    if (Loaded())
        return;
    Loaded() = true;

    FConfigFile Config;
    Config.Read(PreferencesPath());
    for (const auto& A : MutableCatalog())
    {
        FString Value;
        if (Config.GetString(TEXT("Keybinds"), A.Id, Value) && !Value.IsEmpty())
        {
            const FKey K(*Value);
            if (K.IsValid())
                Overrides().Add(FString(A.Id), K);
        }
    }
}

} // namespace

namespace born2flap::keybinds
{

const TArray<FAction>& Catalog()
{
    return MutableCatalog();
}

FKey Get(const TCHAR* Id)
{
    LoadIfNeeded();
    if (const FKey* K = Overrides().Find(FString(Id)))
        return *K;
    for (const auto& A : MutableCatalog())
        if (FCString::Stricmp(A.Id, Id) == 0)
            return A.Default;
    return EKeys::Invalid;
}

FKey Get(const FString& Id)
{
    return Get(*Id);
}

void Set(const TCHAR* Id, FKey Key)
{
    LoadIfNeeded();
    if (!Key.IsValid())
        return;
    for (const auto& A : MutableCatalog())
    {
        if (FCString::Stricmp(A.Id, Id) == 0)
        {
            Overrides().Add(FString(A.Id), Key);
            break;
        }
    }
    // Persist: rewrite the whole [Keybinds] section from the override map.
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    Config.bCanSaveAllSections = true;
    for (const auto& Pair : Overrides())
    {
        const FString Value = Pair.Value.ToString();
        Config.SetString(TEXT("Keybinds"), *Pair.Key, *Value);
    }
    Config.Write(Path);
}

void ResetAll()
{
    LoadIfNeeded();
    Overrides().Reset();
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;
    // Clear every known key (the section may contain stale keys from older
    // builds); removing the whole section is the cleanest reset.
    Config.Remove(FString(TEXT("Keybinds")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    Config.Write(Path);
}

FString DisplayName(FKey Key)
{
    const FString Raw = Key.GetDisplayName().ToString();
    if (Raw.IsEmpty())
        return Key.ToString();
    // Friendly names for the two mouse buttons that act as modifiers/verbs.
    if (Key == EKeys::MiddleMouseButton) return TEXT("MMB");
    if (Key == EKeys::LeftMouseButton)   return TEXT("LMB");
    if (Key == EKeys::RightMouseButton)  return TEXT("RMB");
    if (Key == EKeys::LeftControl)       return TEXT("LCTRL");
    if (Key == EKeys::RightControl)      return TEXT("RCTRL");
    if (Key == EKeys::LeftShift)         return TEXT("LSHIFT");
    if (Key == EKeys::RightShift)        return TEXT("RSHIFT");
    if (Key == EKeys::LeftAlt)           return TEXT("LALT");
    if (Key == EKeys::RightAlt)          return TEXT("RALT");
    if (Key == EKeys::SpaceBar)          return TEXT("SPACE");
    return Raw;
}

} // namespace born2flap::keybinds