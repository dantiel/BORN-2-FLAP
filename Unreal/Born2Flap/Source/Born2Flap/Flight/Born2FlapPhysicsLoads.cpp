#include "Flight/Born2FlapPhysicsLoads.h"
#include "Chaos/SimCallbackObject.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

struct FBorn2FlapLoadInput : Chaos::FSimCallbackInput
{
    Chaos::FSingleParticlePhysicsProxy* Body = nullptr;
    TArray<FBorn2FlapPhysicsLoad> Loads;
    uint64 Sequence = 0;
    void Reset() { Body = nullptr; Loads.Reset(); Sequence = 0; }
};

class FBorn2FlapLoadCallback : public Chaos::TSimCallbackObject<
    FBorn2FlapLoadInput, Chaos::FSimCallbackNoOutput,
    Chaos::ESimCallbackOptions::PreIntegrate | Chaos::ESimCallbackOptions::PostIntegrate |
    Chaos::ESimCallbackOptions::ParticleUnregister>
{
    uint64 Sequence = 0;
    int32 Index = 0;
    double Used = 0;
    Chaos::FSingleParticlePhysicsProxy* ActiveBody = nullptr;
    FVector AddedAcceleration = FVector::ZeroVector;
    FVector AddedAngularAcceleration = FVector::ZeroVector;

    virtual void OnPreSimulate_Internal() override {}

    virtual void OnPreIntegrate_Internal() override
    {
        const auto* Input = GetConsumerInput_Internal();
        if (!Input) return;
        if (Input->Sequence != Sequence)
        {
            Sequence = Input->Sequence;
            Index = 0;
            Used = 0;
            ActiveBody = Input->Body;
        }
        if (!ActiveBody || Index >= Input->Loads.Num()) return;
        auto* Body = ActiveBody->GetPhysicsThreadAPI();
        if (!Body) return;
        const auto* RigidSolver = static_cast<const Chaos::FPBDRigidsSolver*>(GetSolver());
        const double Limit = RigidSolver->GetMaxDeltaTime_External();
        const double Dt = Limit > 0 ? FMath::Min(double(GetDeltaTime_Internal()), Limit)
                                    : double(GetDeltaTime_Internal());
        if (Dt <= 0) return;
        double Remaining = Dt;
        FVector Impulse = FVector::ZeroVector, AngularImpulse = FVector::ZeroVector;
        while (Remaining > 1.e-12 && Index < Input->Loads.Num())
        {
            const auto& Load = Input->Loads[Index];
            const double Take = FMath::Min(Remaining, Load.Duration - Used);
            Impulse += Load.Force * Take;
            AngularImpulse += Load.Moment * Take;
            Remaining -= Take;
            Used += Take;
            if (Used >= Load.Duration - 1.e-12) { ++Index; Used = 0; }
        }
        const FVector BeforeLinear = Body->Acceleration();
        const FVector BeforeAngular = Body->AngularAcceleration();
        Body->AddForce(Impulse * (100.0 / Dt));
        Body->AddTorque(AngularImpulse * (10000.0 / Dt));
        AddedAcceleration = Body->Acceleration() - BeforeLinear;
        AddedAngularAcceleration = Body->AngularAcceleration() - BeforeAngular;
        if (FParse::Param(FCommandLine::Get(), TEXT("B2FChronologyTrace")))
            UE_LOG(LogTemp, Display, TEXT("ChronologyPre seq=%llu index=%d dt=%.12f torque=%.9f w=%.9f accel=%.9f"),
                Sequence, Index, Dt, AngularImpulse.X / Dt, Body->W().X, Body->AngularAcceleration().X);
    }

    virtual void OnPostIntegrate_Internal() override
    {
        // A force is consumed exactly once. Preserve gravity and other systems'
        // loads if Chaos performs several integration steps before EndFrame.
        if (ActiveBody)
            if (auto* Body = ActiveBody->GetPhysicsThreadAPI())
            {
                if (FParse::Param(FCommandLine::Get(), TEXT("B2FChronologyTrace")))
                    UE_LOG(LogTemp, Display, TEXT("ChronologyPost seq=%llu w=%.9f rX=%.12f"), Sequence, Body->W().X, Body->R().X);
                Body->SetAcceleration(Body->Acceleration() - AddedAcceleration, false);
                Body->SetAngularAcceleration(Body->AngularAcceleration() - AddedAngularAcceleration, false);
            }
        AddedAcceleration = AddedAngularAcceleration = FVector::ZeroVector;
    }

    virtual void OnParticleUnregistered_Internal(
        TArray<TTuple<Chaos::FUniqueIdx, Chaos::FSingleParticlePhysicsProxy*>>& Proxies) override
    {
        for (const auto& Proxy : Proxies)
        {
            if (Proxy.Get<1>() == ActiveBody) ActiveBody = nullptr;
            if (const auto* Input = GetConsumerInput_Internal())
                if (Proxy.Get<1>() == Input->Body)
                {
                    Sequence = Input->Sequence;
                    Index = Input->Loads.Num();
                }
        }
    }
};

FBorn2FlapPhysicsLoads::FBorn2FlapPhysicsLoads(FPhysScene_Chaos* InScene) : Scene(InScene)
{
    if (Scene && Scene->GetSolver())
        Callback = Scene->GetSolver()->CreateAndRegisterSimCallbackObject_External<FBorn2FlapLoadCallback>();
}

FBorn2FlapPhysicsLoads::~FBorn2FlapPhysicsLoads()
{
    if (Callback && Scene && Scene->GetSolver())
        Scene->GetSolver()->UnregisterAndFreeSimCallbackObject_External(Callback);
}

bool FBorn2FlapPhysicsLoads::Submit(Chaos::FSingleParticlePhysicsProxy* Body,
                                  const TArray<FBorn2FlapPhysicsLoad>& Loads)
{
    if (!Callback || !Body) return false;
    auto* Input = Callback->GetProducerInputData_External();
    Input->Body = Body;
    Input->Loads = Loads;
    Input->Sequence = ++Sequence;
    return true;
}
