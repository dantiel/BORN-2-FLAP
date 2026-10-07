#pragma once
#include "CoreMinimal.h"

class AActor;
class USceneComponent;

// Procedural raven-crow airframe shared by the player pawn and the replay
// spirits, so every shadow doppelgänger uses the exact same model as the original.
namespace Born2FlapRaven
{
    // Builds the folded-shard raven-crow mesh under Parent. Creates its own
    // "RavenCrow" scene root (returned). OutLeftShoulder/OutRightShoulder
    // receive the two components that drive wing flap. Safe to call once per
    // owner; returns nullptr when Owner or Parent is null.
    USceneComponent* Build(AActor* Owner, USceneComponent* Parent,
                           TObjectPtr<USceneComponent>& OutLeftShoulder,
                           TObjectPtr<USceneComponent>& OutRightShoulder,
                           TObjectPtr<USceneComponent>& OutTailPivot, int32 Design=0);
}