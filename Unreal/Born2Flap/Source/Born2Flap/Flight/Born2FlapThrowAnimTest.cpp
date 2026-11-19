#include "Flight/Born2FlapFlightPawn.h"
#include "Avatar/Born2FlapAvatar.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// -B2FThrowAnimTest: empirical proof of the split-phase throw-animation chain.
// Simulates a grounded SPACE press + release, then verifies that (1) the held
// bird rides the pilot's hand UP to the cocked position during the SPACE hold
// (the wind-up — bird travel IS hand travel), and (2) releasing SPACE fires the
// throw almost immediately because the hand is already cocked. Verdict at t=8s.
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
    // 3.0s (wind-up begins) and release at 4.0s (throw completes). The charge
    // ramps to full strength during the hold, which deepens the reach-back:
    // the cock pose follows the charge (35%-85% of the hand apex), so the
    // strong throw winds the arm further back before the release finishes the
    // reach-back at the slow rate and throws forth at normal speed from that
    // more wound-up position.
    const double T = ThrowAnimTestTime;
    if (T >= 3.0 && T < 3.1) Key(EKeys::SpaceBar, true);
    if (T >= 4.0 && T < 4.1) Key(EKeys::SpaceBar, false);
    // Ramp the throw strength during the hold, exactly as the wheel does (the
    // wheel feeds ThrowCharge, which the charge block forwards to the avatar).
    // Full charge by release so the wind-up follows the deep reach-back and
    // the throw starts from that more wound-up position.
    if (bThrowCharging && T < 4.0)
        ThrowCharge = FMath::Min(1.f, ThrowCharge + DeltaSeconds * 1.5f);

    // Periodic state so the settle/press/hold window is fully visible in the log.
    static int32 LastHeartbeat = -1;
    const int32 Hb = FMath::FloorToInt(T * 4.0);
    if (Hb != LastHeartbeat)
    {
        LastHeartbeat = Hb;
        UE_LOG(LogTemp, Display, TEXT("ThrowAnimT t=%.2f flying=%d grounded=%d settle=%.2f alt=%.2f spd=%.2f held=%d windup=%d throwing=%d %s"),
               T, bFlying ? 1 : 0, bCameraGrounded ? 1 : 0, GroundSettleTime, GetAltitude(), GetSpeed(),
               bBirdHeld ? 1 : 0,
               (Avatar && Avatar->IsThrowWindup()) ? 1 : 0,
               (Avatar && Avatar->IsThrowing()) ? 1 : 0,
               *(Avatar ? Avatar->DebugThrowState() : TEXT("none")));
    }

    // Wind-up travel: while the bird is held and winding up, its travel IS the
    // hand raising to the cocked position (the bird is attached to the carry
    // socket, which UpdateCarrySocket repositions to the hand every tick).
    const bool bWindupNow = Avatar && Avatar->IsThrowWindup();
    if (bWindupNow)
    {
        if (!bSawWindup)
        {
            bSawWindup = true;
            WindupStartPos = Body->GetComponentLocation();
        }
        MaxHandTravel = FMath::Max(MaxHandTravel, FVector::Dist(Body->GetComponentLocation(), WindupStartPos));
        if (Avatar)
            SeenAnimNames.Add(Avatar->GetCurrentAnimName());
    }

    const bool bThrowingNow = Avatar && Avatar->IsThrowing();
    if (bThrowingNow)
    {
        if (!bSawThrowing)
        {
            bSawThrowing = true;
            ThrowAnimStartTime = T;
        }
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

    const bool bHeldNow = bBirdHeld;
    if (bHeldNow) bSawHeld = true;

    if (T >= 9.0)
    {
        // Split-phase throw: the wind-up raises the hand to the cocked top and
        // halts there; releasing SPACE completes the throw with the remaining
        // forward sweep (release frame minus halt, < 1 s for the baked clips).
        const float Halt = Avatar ? Avatar->GetThrowHaltTime() : -1.f;
        const bool Pass = bSawHeld && bSawWindup && bSawThrowing && MaxHandTravel > 15.f
            && ReleaseDelay >= 0.f && ReleaseDelay < 1.2f && ThrowEndTime < 0.f && Halt > 0.f;
        FString Names;
        for (const FName& N : SeenAnimNames) Names += N.ToString() + TEXT(" ");
        UE_LOG(LogTemp, Display, TEXT("ThrowAnimTest %s held=%d windup=%d throwing=%d maxHandTravel=%.1fcm releaseDelay=%.2fs throwCutShort=%.2fs halt=%.3fs anims={%s} grounded=%d flying=%d"),
               Pass ? TEXT("PASS") : TEXT("FAIL"), bSawHeld ? 1 : 0, bSawWindup ? 1 : 0, bSawThrowing ? 1 : 0,
               MaxHandTravel, ReleaseDelay, ThrowEndTime, Halt, *Names, bCameraGrounded ? 1 : 0, bFlying ? 1 : 0);
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
}