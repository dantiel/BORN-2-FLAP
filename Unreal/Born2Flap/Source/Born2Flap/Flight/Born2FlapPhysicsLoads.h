#pragma once

#include "CoreMinimal.h"

namespace Chaos { class FSingleParticlePhysicsProxy; }
class FPhysScene_Chaos;
class FBorn2FlapLoadCallback;

// World-space SI loads, in chronological order. Each sample owns its duration.
struct FBorn2FlapPhysicsLoad
{
    FVector Force;
    FVector Moment;
    double Duration;
};

// Transfers a frame's resolved wing/body reactions to the actual Chaos substeps.
// No pose or velocity is prescribed: Chaos still integrates loads and contacts.
class FBorn2FlapPhysicsLoads
{
public:
    explicit FBorn2FlapPhysicsLoads(FPhysScene_Chaos* InScene);
    ~FBorn2FlapPhysicsLoads();
    bool Submit(Chaos::FSingleParticlePhysicsProxy* Body, const TArray<FBorn2FlapPhysicsLoad>& Loads);
private:
    FPhysScene_Chaos* Scene;
    FBorn2FlapLoadCallback* Callback = nullptr;
    uint64 Sequence = 0;
};
