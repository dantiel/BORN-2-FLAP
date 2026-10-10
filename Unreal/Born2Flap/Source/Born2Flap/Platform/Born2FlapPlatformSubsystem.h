#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Platform/Born2FlapPlatformBackend.h"
#include "Templates/UniquePtr.h"
#include "Born2FlapPlatformSubsystem.generated.h"

// Game-instance scoped platform service hub. Backend selection at Initialize:
//   1. Store backend factory registered by the optional Born2FlapOnline plugin
//      (FModuleManager::IsModuleLoaded("Born2FlapOnline") + B2FBackendFactory)
//   2. FB2FNullBackend (default) — MIT build, no store interaction.
// All passthroughs are null-safe and may be called from any gameplay code.
UCLASS()
class BORN2FLAP_API UBorn2FlapPlatformSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
  public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // World-context helper; returns null when no game instance exists.
    static UBorn2FlapPlatformSubsystem* Get(const UObject* WorldContextObject);

    FB2FPlatformBackend* GetBackend() const { return Backend.Get(); }

    void Tick(float DeltaSeconds);
    void QueryAchievements();
    void UnlockAchievement(EB2FAchievementId AchievementId);
    bool ReadCloudSlot(EB2FCloudSlot Slot, FString& OutData) const;
    bool WriteCloudSlot(EB2FCloudSlot Slot, const FString& Data) const;
    void SetPresence(const FString& State, const FString& Context);
    FString IdentityName() const;
    FString ServiceName() const;
    bool IsStoreAvailable() const;

  private:
    TUniquePtr<FB2FPlatformBackend> Backend;
};