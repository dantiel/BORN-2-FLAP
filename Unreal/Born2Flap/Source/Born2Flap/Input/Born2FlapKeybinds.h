#pragma once
// Born2FlapKeybinds.h — the rebindable key-action registry.
//
// Every in-game hotkey (flight controls, camera, debug toggles, menu, radio)
// lives here as a named action with a default FKey. The bindings are persisted
// to Config/FlightPreferences.ini under [Keybinds] and read through one entry
// point, so the SETTINGS → KEY BINDINGS page and the runtime input polling
// share a single source of truth.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

namespace born2flap::keybinds
{

struct FAction
{
    const TCHAR* Id;       // stable semantic id + config key (e.g. "Throttle")
    const TCHAR* Label;    // settings-page display label
    const TCHAR* Category; // settings-page grouping header
    FKey Default;          // factory default binding
};

// The full bindable action catalog, in display order.
const TArray<FAction>& Catalog();

// Current binding for an action (loads persisted overrides once, lazily).
FKey Get(const TCHAR* Id);
FKey Get(const FString& Id);

// Rebind an action and persist immediately.
void Set(const TCHAR* Id, FKey Key);

// Restore every action to its factory default and persist.
void ResetAll();

// Human-readable key name for the settings page ("W", "Space Bar", "MMB"…).
FString DisplayName(FKey Key);

} // namespace born2flap::keybinds
