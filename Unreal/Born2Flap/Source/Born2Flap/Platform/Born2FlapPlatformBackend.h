#pragma once

#include "CoreMinimal.h"
#include "Platform/Born2FlapPlatformTypes.h"
#include "Templates/UniquePtr.h"

class UGameInstance;

// Backend abstraction for store platform services: achievements, cloud saves,
// presence. The Born2Flap core module talks only to this interface — concrete
// backends live behind the optional Born2FlapOnline plugin (M3) and are
// injected through B2FBackendFactory. The default is the no-op FB2FNullBackend,
// so the MIT build behaves exactly as before (no store SDK linked).
class BORN2FLAP_API FB2FPlatformBackend
{
public:
    virtual ~FB2FPlatformBackend() = default;

    virtual void Initialize() {}
    virtual void Tick(float DeltaSeconds) {}
    virtual void Shutdown() {}

    // Achievements: fetch cached state from the store, unlock locally + remote.
    virtual void QueryAchievements() {}
    virtual void UnlockAchievement(EB2FAchievementId AchievementId) {}

    // Cloud saves: opaque named text slots. Return false when unavailable so
    // callers fall back to their local persistence (GConfig/ini, Saved files).
    virtual bool ReadCloudSlot(const FString& SlotName, FString& OutData) { return false; }
    virtual bool WriteCloudSlot(const FString& SlotName, const FString& Data) { return false; }

    // Presence: free-form rich presence (State = verb phrase, Context = detail).
    virtual void SetPresence(const FString& State, const FString& Context) {}

    // Identity & diagnostics.
    virtual FString IdentityName() const { return FString(); }
    virtual FString ServiceName() const { return TEXT("none"); }
    virtual bool IsAvailable() const { return false; }
};

// Factory seam: registered by store backends during their module startup.
// The core module never links against store SDKs; this pointer is the seam.
using FB2FBackendFactory = TUniquePtr<FB2FPlatformBackend> (*)(UGameInstance& GameInstance);

BORN2FLAP_API FB2FBackendFactory& B2FBackendFactory();
BORN2FLAP_API TUniquePtr<FB2FPlatformBackend> B2FCreateNullBackend();
