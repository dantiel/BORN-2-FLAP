#include "Platform/Born2FlapPlatformSubsystem.h"
#include "Platform/Born2FlapPlatformBackend.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"

void UBorn2FlapPlatformSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    FB2FBackendFactory& Factory = B2FBackendFactory();
    UGameInstance* GameInstance = GetGameInstance();
    if (Factory && GameInstance && FModuleManager::Get().IsModuleLoaded(TEXT("Born2FlapOnline")))
    {
        Backend = Factory(*GameInstance);
    }
    if (!Backend)
    {
        Backend = B2FCreateNullBackend();
    }
    Backend->Initialize();
    UE_LOG(LogTemp, Log, TEXT("B2FPlatform: backend=%s store=%s"), *Backend->ServiceName(),
           Backend->IsAvailable() ? TEXT("yes") : TEXT("no"));
}

void UBorn2FlapPlatformSubsystem::Deinitialize()
{
    if (Backend)
    {
        Backend->Shutdown();
    }
    Backend.Reset();
    Super::Deinitialize();
}

UBorn2FlapPlatformSubsystem* UBorn2FlapPlatformSubsystem::Get(const UObject* WorldContextObject)
{
    if (!WorldContextObject || !GEngine)
    {
        return nullptr;
    }
    const UWorld* World =
        GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UBorn2FlapPlatformSubsystem>() : nullptr;
}

void UBorn2FlapPlatformSubsystem::Tick(float DeltaSeconds)
{
    if (Backend)
    {
        Backend->Tick(DeltaSeconds);
    }
}

void UBorn2FlapPlatformSubsystem::QueryAchievements()
{
    if (Backend)
    {
        Backend->QueryAchievements();
    }
}

void UBorn2FlapPlatformSubsystem::UnlockAchievement(EB2FAchievementId AchievementId)
{
    if (Backend && B2FIsValidAchievement(AchievementId))
    {
        Backend->UnlockAchievement(AchievementId);
    }
}

bool UBorn2FlapPlatformSubsystem::ReadCloudSlot(EB2FCloudSlot Slot, FString& OutData) const
{
    return Backend && B2FIsValidCloudSlot(Slot) && Backend->ReadCloudSlot(B2FCloudSlotName(Slot), OutData);
}

bool UBorn2FlapPlatformSubsystem::WriteCloudSlot(EB2FCloudSlot Slot, const FString& Data) const
{
    return Backend && B2FIsValidCloudSlot(Slot) && Backend->WriteCloudSlot(B2FCloudSlotName(Slot), Data);
}

void UBorn2FlapPlatformSubsystem::SetPresence(const FString& State, const FString& Context)
{
    if (Backend)
    {
        Backend->SetPresence(State, Context);
    }
}

FString UBorn2FlapPlatformSubsystem::IdentityName() const
{
    return Backend ? Backend->IdentityName() : FString();
}

FString UBorn2FlapPlatformSubsystem::ServiceName() const
{
    return Backend ? Backend->ServiceName() : TEXT("none");
}

bool UBorn2FlapPlatformSubsystem::IsStoreAvailable() const
{
    return Backend && Backend->IsAvailable();
}