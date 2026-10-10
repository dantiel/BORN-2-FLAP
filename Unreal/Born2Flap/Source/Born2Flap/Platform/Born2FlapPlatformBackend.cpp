#include "Platform/Born2FlapPlatformBackend.h"

FB2FBackendFactory& B2FBackendFactory()
{
    static FB2FBackendFactory Factory = nullptr;
    return Factory;
}

// No-op backend: the MIT build stays fully offline — every call falls back to
// local persistence. All operations log at Verbose for diagnostics.
class FB2FNullBackend final : public FB2FPlatformBackend
{
public:
    virtual void Initialize() override
    {
        UE_LOG(LogTemp, Log, TEXT("B2FPlatform: null backend active (no store service)"));
    }
    virtual void UnlockAchievement(EB2FAchievementId AchievementId) override
    {
        UE_LOG(LogTemp, Verbose, TEXT("B2FPlatform: achievement unlock skipped (null backend): %d"),
               static_cast<int32>(AchievementId));
    }
    virtual void SetPresence(const FString& State, const FString& Context) override
    {
        UE_LOG(LogTemp, Verbose, TEXT("B2FPlatform: presence skipped (null backend): %s | %s"), *State, *Context);
    }
    virtual FString ServiceName() const override { return TEXT("none"); }
};

TUniquePtr<FB2FPlatformBackend> B2FCreateNullBackend()
{
    return MakeUnique<FB2FNullBackend>();
}
