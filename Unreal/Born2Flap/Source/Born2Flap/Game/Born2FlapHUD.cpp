#include "Game/Born2FlapHUD.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Game/Born2FlapGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
void ABorn2FlapHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
        return;
    auto *Bird = Cast<ABorn2FlapFlightPawn>(GetOwningPawn());
    if (!Bird)
        return;
    const FLinearColor Cream(.94f, .94f, .84f), Gold(1, .66f, .2f), Muted(.55f, .76f, .76f);
    DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, 20, 445, 182);
    DrawText(TEXT("BORN 2 FLAP"), Cream, 38, 30, GEngine->GetLargeFont(), 1.7f);
    DrawText(Bird->HasAttitudeAssist() ? TEXT("WING-POWERED  /  ATTITUDE ASSIST")
                                       : TEXT("WING-POWERED  /  FREE ATTITUDE"),
             Gold, 40, 65, GEngine->GetSmallFont(), 1.1f);
    DrawText(Bird->GetFlightStatus(), Cream, 40, 93, GEngine->GetSmallFont(), 1.05f);
    DrawText(FString::Printf(TEXT("HEIGHT  %4.1f m     SPEED  %4.1f m/s"), Bird->GetAltitude(), Bird->GetSpeed()),
             Cream, 40, 123, GEngine->GetMediumFont(), 1.f);
    DrawText(FString::Printf(TEXT("CLIMB  %+.1f m/s    EFFORT  %.0f%%    BATTERY  %.0f%%"), Bird->GetClimbRate(),
                             Bird->GetEffort() * 100, Bird->GetBattery() * 100),
             Cream, 40, 156, GEngine->GetSmallFont(), 1.f);
    if (auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        DrawText(FString::Printf(TEXT("GATES  %d / %d"), Mode->GetGatesPassed(), Mode->GetGateCount()), Gold,
                 Canvas->SizeX - 185, 36, GEngine->GetMediumFont(), 1.f);
    const float Y = Canvas->SizeY - 85;
    DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, Y, Canvas->SizeX - 40, 65);
    DrawText(TEXT("SPACE launch   W flap   SHIFT+W power   RELEASE glide   A/D bank   UP/DOWN pitch   R reset"), Cream,
             38, Y + 12, GEngine->GetSmallFont(), 1.05f);
    DrawText(TEXT("Gain airspeed before pulling up.  F1: aero / gravity vectors   F2: attitude assist"), Muted, 38,
             Y + 38, GEngine->GetSmallFont(), 1.f);
}
