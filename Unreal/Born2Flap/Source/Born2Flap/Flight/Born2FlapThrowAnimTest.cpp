#include "Flight/Born2FlapFlightPawn.h"
#include "Avatar/Born2FlapAvatar.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// -B2FThrowAnimTest: empirical proof of the throw-animation chain.
// Simulates a grounded SPACE press + release, then measures whether the held
// bird rides the pilot's animated hands through the wind-up (the bird is
// attached to the CarrySocket, which UpdateCarrySocket repositions to the
// hand midpoint of the CURRENT pose every avatar tick — so bird travel during
// the throw IS hand travel). Verdict at t=6s.
void ABorn2FlapFlightPawn::CheckThrowAnimTest(float DeltaSeconds)
{
    ThrowAnimTestTime += DeltaSeconds;
    auto* PC = Cast<APlayerController>(GetController());
    if (!PC || !PC->PlayerInput) return;
    auto Key = [&](FKey K, bool Down) {
        PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    };

    // Timeline: the bird spawns at ~68 cm and must fall + land + settle before
    // the ground charge works (bCameraGrounded flips at ~2.0s here). Press at
    // 3.0s and release at 3.6s.
    const double T = ThrowAnimTestTime;
    if (T >= 3.0 && T < 3.1) Key(EKeys::SpaceBar, true);
    if (T >= 3.6 && T < 3.7) Key(EKeys::SpaceBar, false);

    // Periodic state so the settle/press window is fully visible in the log.
    static int32 LastHeartbeat = -1;
    const int32 Hb = FMath::FloorToInt(T * 4.0);
    if (Hb != LastHeartbeat)
    {
        LastHeartbeat = Hb;
        UE_LOG(LogTemp, Display, TEXT("ThrowAnimT t=%.2f flying=%d grounded=%d settle=%.2f alt=%.2f spd=%.2f held=%d throwing=%d %s"),
               T, bFlying ? 1 : 0, bCameraGrounded ? 1 : 0, GroundSettleTime, GetAltitude(), GetSpeed(),
               bBirdHeld ? 1 : 0, (Avatar && Avatar->IsThrowing()) ? 1 : 0,
               *(Avatar ? Avatar->DebugThrowState() : TEXT("none")));
    }

    const bool bHeldNow = bBirdHeld;
    const bool bThrowingNow = Avatar && Avatar->IsThrowing();
    if (bHeldNow) bSawHeld = true;
    if (bThrowingNow)
    {
        if (!bSawThrowing)
        {
            bSawThrowing = true;
            ThrowAnimStartPos = Body->GetComponentLocation();
            ThrowAnimStartTime = T;
        }
        const float Travel = FVector::Dist(Body->GetComponentLocation(), ThrowAnimStartPos);
        MaxHandTravel = FMath::Max(MaxHandTravel, Travel);
        if (Avatar)
            SeenAnimNames.Add(Avatar->GetCurrentAnimName());
    }
    else if (bSawThrowing && !bFlying && T < ThrowAnimStartTime + 1.6)
    {
        // Throwing flag cleared before the bird launched = clip was cut short.
        ThrowEndTime = T;
    }
    if (bSawThrowing && bFlying && ReleaseDelay < 0)
    {
        ReleaseDelay = float(T - ThrowAnimStartTime);
    }

    if (T >= 8.0)
    {
        const bool Pass = bSawHeld && bSawThrowing && MaxHandTravel > 15.f
            && ReleaseDelay > 0.4f && ReleaseDelay < 1.05f && ThrowEndTime < 0.f;
        FString Names;
        for (const FName& N : SeenAnimNames) Names += N.ToString() + TEXT(" ");
        UE_LOG(LogTemp, Display, TEXT("ThrowAnimTest %s held=%d throwing=%d maxHandTravel=%.1fcm releaseDelay=%.2fs throwCutShort=%.2fs anims={%s} grounded=%d flying=%d"),
               Pass ? TEXT("PASS") : TEXT("FAIL"), bSawHeld ? 1 : 0, bSawThrowing ? 1 : 0,
               MaxHandTravel, ReleaseDelay, ThrowEndTime, *Names, bCameraGrounded ? 1 : 0, bFlying ? 1 : 0);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
}