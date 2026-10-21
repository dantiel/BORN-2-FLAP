#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Born2FlapStaticBird.generated.h"

// Non-simulating source assembly for editor baking and museum displays.
UCLASS()
class BORN2FLAP_API ABorn2FlapStaticBird : public AActor
{
    GENERATED_BODY()
public:
    ABorn2FlapStaticBird();
    UFUNCTION(BlueprintCallable, Category="Bird")
    void BuildKestrel();
};
