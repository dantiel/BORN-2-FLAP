#include "Game/Born2FlapHUD.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Game/Born2FlapGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Input/Born2FlapRcController.h"
void ABorn2FlapHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas)
        return;
    auto *Bird = Cast<ABorn2FlapFlightPawn>(GetOwningPawn());
    if (!Bird)
        return;
    const FLinearColor Cream(.94f, .94f, .84f), Gold(1, .66f, .2f), Muted(.55f, .76f, .76f);
    DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, 20, 445, 232);
    DrawText(TEXT("BORN 2 FLAP"), Cream, 38, 30, GEngine->GetLargeFont(), 1.7f);
    DrawText(TEXT("RC STICKS  /  AERODYNAMIC FLIGHT"), Gold, 40, 65, GEngine->GetSmallFont(), 1.1f);
    DrawText(Bird->GetFlightStatus(), Cream, 40, 93, GEngine->GetSmallFont(), 1.05f);
    DrawText(FString::Printf(TEXT("HEIGHT  %4.1f m     SPEED  %4.1f m/s"), Bird->GetAltitude(), Bird->GetSpeed()),
             Cream, 40, 123, GEngine->GetMediumFont(), 1.f);
    DrawText(FString::Printf(TEXT("CLIMB  %+.1f m/s    EFFORT  %.0f%%    BATTERY  %.0f%%"), Bird->GetClimbRate(),
                             Bird->GetEffort() * 100, Bird->GetBattery() * 100),
             Cream, 40, 156, GEngine->GetSmallFont(), 1.f);
    const FVector Sticks = Bird->GetRcSticks();
    const FVector2D Wings = Bird->GetWingAngles();
    DrawText(FString::Printf(TEXT("RC  YAW %+.2f    ROLL %+.2f    PITCH %+.2f"), Sticks.Z, Sticks.X, Sticks.Y), Gold,
             40, 184, GEngine->GetSmallFont(), 1.f);
    DrawText(FString::Printf(TEXT("WINGS  L %+.1f deg    R %+.1f deg"), Wings.X, Wings.Y), Muted, 40, 212,
             GEngine->GetSmallFont(), 1.f);
    if (auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        DrawText(Mode->IsNatureLevel() ? TEXT("WALDTAL  /  F4: Uebungsgelaende")
                                       : FString::Printf(TEXT("UEBUNG  %d / %d  /  F4: Waldtal"),
                                                         Mode->GetGatesPassed(), Mode->GetGateCount()),
                 Cream, Canvas->SizeX - 370, 36, GEngine->GetSmallFont(), 1.05f);
    const auto *Rc = Bird->GetRcController();
    if (Rc)
        DrawText(Rc->GetStatus(), Muted, 38, 263, GEngine->GetSmallFont(), 1.f);
    const float Y = Canvas->SizeY - 85;
    DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, Y, Canvas->SizeX - 40, 65);
    DrawText(TEXT("SPACE launch   W throttle   SHIFT+W full   A/D yaw   LEFT/RIGHT roll   UP/DOWN pitch   R reset"),
             Cream, 38, Y + 12, GEngine->GetSmallFont(), 1.05f);
    DrawText(TEXT("Kurze Taps: kleine Ausschlaege. W loslassen: gleiten.  F1: Kraefte   F3: RC-Sender   F4: Level"),
             Muted, 38, Y + 38, GEngine->GetSmallFont(), 1.f);
    if (Rc && Rc->IsPanelOpen())
        DrawRcPanel(*Rc);
}
void ABorn2FlapHUD::DrawRcPanel(const FBorn2FlapRcController &Rc)
{
    const float Width = FMath::Min(940.f, Canvas->SizeX - 40.f), X = (Canvas->SizeX - Width) * .5f;
    const float Y = FMath::Max(20.f, (Canvas->SizeY - 570.f) * .5f);
    const FLinearColor Cream(.94f, .94f, .87f), Accent(.55f, .82f, .64f), Muted(.63f, .71f, .72f);
    DrawRect(FLinearColor(0, 0, 0, .65f), 0, 0, Canvas->SizeX, Canvas->SizeY);
    DrawRect(FLinearColor(.025f, .048f, .045f, .98f), X, Y, Width, 570);
    DrawRect(Accent, X, Y, Width, 3);
    auto Text = [&](const FString &Line, float Dy, FLinearColor Colour = FLinearColor(.94f, .94f, .87f)) {
        DrawText(Line, Colour, X + 28, Y + Dy, GEngine->GetSmallFont(), 1.1f);
    };
    DrawText(TEXT("RC-SENDER VERBINDEN"), Cream, X + 28, Y + 22, GEngine->GetMediumFont(), 1.25f);
    Text(Rc.GetDeviceName() + (Rc.IsConnected() ? TEXT("  |  verbunden") : TEXT("  |  nicht verbunden")), 63, Accent);
    Text(Rc.GetStatus(), 90, Muted);
    Text(Rc.GetInstruction(), 128, Accent);
    Text(TEXT("Beim Zuordnen: alle anderen Knueppel neutral, Gas unten. Kalibrierung am Boden durchfuehren."), 155,
         Muted);
    Text(TEXT("LIVE-ACHSEN"), 192);
    for (int32 I = 0; I < 8; ++I)
    {
        const float Row = Y + 221 + I * 23;
        const bool Available = Rc.GetAvailableAxes()[I] && Rc.IsConnected();
        const float Value = Available ? FMath::Clamp(float(Rc.GetRawAxes()[I]), 0.f, 1.f) : 0;
        DrawText(FString::Printf(TEXT("%d"), I + 1), Muted, X + 30, Row, GEngine->GetSmallFont(), 1.f);
        DrawRect(FLinearColor(.12f, .18f, .17f), X + 60, Row + 3, 270, 12);
        DrawRect(Available ? Accent : Muted, X + 60, Row + 3, Value * 270, 12);
        DrawText(Available ? FString::Printf(TEXT("%3.0f%%"), Value * 100) : TEXT("--"), Cream, X + 344, Row,
                 GEngine->GetSmallFont(), 1.f);
    }
    for (int32 I = 0; I < 4; ++I)
        DrawText(Rc.GetMapping(I), Cream, X + Width * .52, Y + 223 + I * 37, GEngine->GetSmallFont(), 1.1f);
    Text(Rc.GetButtons(), 420, Muted);
    Text(Rc.Notice, 450, Accent);
    Text(TEXT("TAB Geraet   C Kalibrieren   ENTER Weiter   X Abbrechen   G RC/Tastatur   F3 Schliessen"), 493);
    Text(TEXT("Kanaele werden pro Geraet gespeichert. Nach Verbindungsabbruch Gas kurz auf Minimum."), 525, Muted);
}
