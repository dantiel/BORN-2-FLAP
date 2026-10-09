#include "Game/Born2FlapHUD.h"
#include "Flight/Born2FlapFlightPawn.h"
#include "Game/Born2FlapGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Input/Born2FlapRcController.h"
#include "Input/Born2FlapKeybinds.h"
#include "World/Born2FlapValley.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Racing/Born2FlapRacing.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Born2FlapFlightHUD.h"
#include "UI/Born2FlapI18n.h"

namespace
{
// The brand logo shared with the main-menu sidebar (Born2FlapMenu.cpp), drawn in
// place of the old "BORN 2 FLAP" text so the imperative overlay matches the menu.
UTexture2D* BrandLogo()
{
    static TStrongObjectPtr<UTexture2D> Logo(
        LoadObject<UTexture2D>(nullptr, TEXT("/Game/Splash/born2flap-logo.born2flap-logo")));
    return Logo.Get();
}
}  // namespace

void ABorn2FlapHUD::DrawHUD()
{
    Super::DrawHUD();
    if(FParse::Param(FCommandLine::Get(),TEXT("B2FCoastTest"))) return;
    if (!Canvas)
        return;
    auto *Bird = Cast<ABorn2FlapFlightPawn>(GetOwningPawn());
    if (!Bird)
        return;
    // F9 stream overlay: a faint line from screen centre showing the mouse
    // control-input direction (not the cursor position). Drawn before the Brain
    // early-return so it also shows while the Ruby Brain authors the panels —
    // and deliberately NOT gated on IsUIHidden, so F7 strips every other HUD
    // element but leaves this control-reference visible.
    if (auto* PC = GetOwningPlayerController())
        if (PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("ToggleMouseInd"))))
            bShowMouseIndicator = !bShowMouseIndicator;
    if (bShowMouseIndicator)
        DrawMouseIndicator(*Bird);
    // Brain path: the Ruby Brain authors every panel (cockpit / RC / channels /
    // radio / menu / poi / splash) — the imperative canvas stays dormant.
    if (auto* GM = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
        if (GM->IsBrainActive())
            return;
    if (Bird->IsFlightSettingsOpen()) return;
    auto* PC = GetOwningPlayerController();
    if (PC->WasInputKeyJustPressed(born2flap::keybinds::Get(TEXT("ToggleChannels")))) bShowChannels = !bShowChannels;
    if (Bird->IsUIHidden()) return;  // F7 (owned by FlightPawn) removes ALL UI
    const auto* Controller = Bird->GetRcController();
    if (Controller && Controller->IsPanelOpen()) { DrawRcPanel(*Controller); return; }
    if (FParse::Param(FCommandLine::Get(), TEXT("B2FRavenCapture"))) return;
    // When the semantic glass cockpit (ABorn2FlapFlightHUD) is alive, it owns
    // the brand + telemetry readouts — the imperative canvas keeps only the
    // functional bits (RC panel, channels, radio, key hints, camera label).
    const bool bSemanticHUD = (bool)TActorIterator<ABorn2FlapFlightHUD>(GetWorld());
    if (auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()); Mode && Mode->IsNatureLevel())
    {
        const auto *Rc = Bird->GetRcController();
        if (Rc && Rc->IsPanelOpen()) { DrawRcPanel(*Rc); return; }
        if (FParse::Param(FCommandLine::Get(), TEXT("B2FRavenCapture"))) return;
        const FLinearColor Ivory(.93,.91,.81), Muted(.68,.74,.70), Amber(.88,.63,.31), Ink(.014,.028,.025,.72);
        const float W = Canvas->SizeX, H = Canvas->SizeY;
        if (!bSemanticHUD)
        {
            DrawRect(Amber,32,35,3,63);
            DrawText(Mode->IsCoastLevel()?TEXT("B O R N  2  F L A P   /   S E A  B R E E Z E"):TEXT("B O R N  2  F L A P   /   A U T U M N"),Muted,47,33,GEngine->GetSmallFont(),.9);
            DrawText(Mode->IsCoastLevel()?TEXT("SHIOMORI BAY"):TEXT("RAVENSTONEFIELD"),Ivory,45,51,GEngine->GetLargeFont(),1.15);
            const FVector P=Bird->GetActorLocation();
            DrawText(Mode->IsCoastLevel()?TEXT("TIDEWALK / BASALT COVE"):ABorn2FlapValley::PlaceName(P.X,P.Y),Amber,47,85,GEngine->GetSmallFont(),.95);
            DrawRect(Ink,32,H-120,425,83);
            // Readouts split into bright digits + quiet units (and a quiet
            // sign where a value can go negative) so the line never jitters.
            auto Readout = [&](const FString& Num, const FString& Unit, float Xr, float Yr)
            {
                DrawText(Num, Ivory, Xr, Yr, GEngine->GetMediumFont(), .9f);
                DrawText(Unit, Muted, Xr + 44.f, Yr + 3.f, GEngine->GetSmallFont(), .72f);
            };
            Readout(FString::Printf(TEXT("%.0f"), Bird->GetAltitude()), TEXT("m"), 48, H-109);
            Readout(FString::Printf(TEXT("%.1f"), Bird->GetSpeed()), TEXT("m/s"), 148, H-109);
            Readout(FString::Printf(TEXT("%.0f"), Bird->GetBattery()*100), TEXT("%"), 248, H-109);
            DrawText(Bird->GetFlightStatus(),Muted,48,H-79,GEngine->GetSmallFont(),.9);
            DrawText(TEXT("SPACE launch  W / CTRL+W / SHIFT+W  WHEEL fine"),Ivory,48,H-55,GEngine->GetSmallFont(),.85);
        }
        // The radio is rendered by the semantic ABorn2FlapRadioHUD (new UI
        // system), not the imperative canvas.
        if (bShowChannels) DrawChannels(*Bird);
        return;
    }
    const FLinearColor Cream(.94f, .94f, .84f), Gold(1, .66f, .2f), Muted(.55f, .76f, .76f), Amber(.88f, .63f, .31f);
    if (Bird->IsBlindFlight())
    {
        const auto *RcBlind = Bird->GetRcController();
        if (RcBlind && RcBlind->IsPanelOpen())
        {
            DrawRcPanel(*RcBlind);
            return;
        }
        DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, 20, 360, 72);
        DrawText(TEXT("BLINDFLUG — NUR KLANG"), Gold, 38, 30, GEngine->GetLargeFont(), 1.5f);
        DrawText(TEXT("F5: Vogel zeigen    R: Reset    SPACE: Start"), Muted, 40, 68, GEngine->GetSmallFont(), 1.f);
        return;
    }
    if (!bSemanticHUD)
    {
        DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, 20, 445, 250);
        if (UTexture2D* Logo = BrandLogo())
        {
            Canvas->DrawTile(Logo, 38, 20, 260, 80, 0, 0, Logo->GetSizeX(), Logo->GetSizeY());
        }
        DrawText(TEXT("RC STICKS  /  AERODYNAMIC FLIGHT"), Gold, 40, 108, GEngine->GetSmallFont(), 1.1f);
        DrawText(Bird->GetFlightStatus(), Cream, 40, 136, GEngine->GetSmallFont(), 1.05f);
        // Bright digits, quiet units — and the climb sign gets its own soft
        // amber glyph so a descending "-" reads at a glance.
        auto Stat2 = [&](const FString& Label, float Xl, float Xv, const FString& Num, const FString& Unit, float Yr,
                         const FLinearColor& ValueColour)
        {
            DrawText(Label, Muted, Xl, Yr, GEngine->GetSmallFont(), 1.f);
            DrawText(Num, ValueColour, Xv, Yr, GEngine->GetMediumFont(), 1.f);
            DrawText(Unit, Muted, Xv + 44.f, Yr + 3.f, GEngine->GetSmallFont(), .72f);
        };
        Stat2(TEXT("HEIGHT"), 40, 118, FString::Printf(TEXT("%.0f"), Bird->GetAltitude()), TEXT("m"), 166, Cream);
        Stat2(TEXT("SPEED"), 205, 278, FString::Printf(TEXT("%.1f"), Bird->GetSpeed()), TEXT("m/s"), 166, Cream);
        {
            const float Climb = Bird->GetClimbRate();
            const FString Sign = Climb < -0.05f ? TEXT("-") : (Climb > 0.05f ? TEXT("+") : TEXT(" "));
            DrawText(TEXT("CLIMB"), Muted, 40, 199, GEngine->GetSmallFont(), 1.f);
            DrawText(Sign, Amber, 118, 199, GEngine->GetMediumFont(), 1.f);
            DrawText(FString::Printf(TEXT("%.1f"), FMath::Abs(Climb)), Cream, 130, 199, GEngine->GetMediumFont(), 1.f);
            DrawText(TEXT("m/s"), Muted, 174, 202, GEngine->GetSmallFont(), .72f);
        }
        Stat2(TEXT("EFFORT"), 205, 278, FString::Printf(TEXT("%.0f"), Bird->GetEffort() * 100), TEXT("%"), 199, Cream);
        Stat2(TEXT("BATTERY"), 330, 405, FString::Printf(TEXT("%.0f"), Bird->GetBattery() * 100), TEXT("%"), 199, Cream);
        const FVector Sticks = Bird->GetRcSticks();
        const FVector2D Wings = Bird->GetWingAngles();
        DrawText(FString::Printf(TEXT("RC  YAW %+.2f    ROLL %+.2f    PITCH %+.2f"), Sticks.Z, Sticks.X, Sticks.Y), Gold,
                 40, 227, GEngine->GetSmallFont(), 1.f);
        DrawText(FString::Printf(TEXT("WINGS  L %+.1f deg    R %+.1f deg"), Wings.X, Wings.Y), Muted, 40, 255,
                 GEngine->GetSmallFont(), 1.f);
    }
    if (auto *Mode = Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode()))
    {
        FString RaceLine;
        if (Mode->IsNatureLevel())
            RaceLine = TEXT("WALDTAL  /  F4: Uebungsgelaende");
        else if (Mode->IsRaceComplete())
            RaceLine = FString::Printf(TEXT("%s  %.2fs  /  %s %.2fs"),
                                       *Born2Flap::I18n::T("race.complete"), Mode->GetRaceTime(),
                                       *Born2Flap::I18n::T("race.best"), Mode->GetBestLapTime());
        else
            RaceLine = FString::Printf(TEXT("GATES %d / %d   %.2fs   /  F4: Waldtal"),
                                       Mode->GetGatesPassed(), Mode->GetGateCount(), Mode->GetRaceTime());
        DrawText(RaceLine, Cream, Canvas->SizeX - 370, 36, GEngine->GetSmallFont(), 1.05f);
    }
    if (!CachedRacing.IsValid())
        CachedRacing = Cast<ABorn2FlapRacingManager>(
            UGameplayStatics::GetActorOfClass(GetWorld(), ABorn2FlapRacingManager::StaticClass()));
    if (auto *Racing = CachedRacing.Get())
    {
        DrawText(Racing->GetRacingStatus(), Gold, Canvas->SizeX - 370, 62, GEngine->GetSmallFont(), 1.05f);
        DrawText(Racing->IsSurpassingBest() ? TEXT("SCHATTEN UEBERHOLT — neue Bestmarke!")
                                            : Racing->GetBestLine(),
                 Racing->IsSurpassingBest() ? FLinearColor(1, .55f, .2f) : Cream, Canvas->SizeX - 370, 84,
                 GEngine->GetSmallFont(), 1.f);
    }
    const auto *Rc = Bird->GetRcController();
    if (Rc)
        DrawText(Rc->GetStatus(), Muted, 38, 263, GEngine->GetSmallFont(), 1.f);
    const float Y = Canvas->SizeY - 85;
    DrawRect(FLinearColor(.015f, .035f, .045f, .82f), 20, Y, Canvas->SizeX - 40, 65);
    DrawText(TEXT("SPACE launch   W throttle   CTRL+W low   SHIFT+W full   WHEEL fine   MOUSE + ARROWS/A/D steering   R reset"),
             Cream, 38, Y + 12, GEngine->GetSmallFont(), 1.05f);
    DrawText(TEXT("CLICK centres mouse   F6 ground / air   V chase / FPV   F8 bird + controls   F2 channels   F3 RC   F4 level   F7 HUD"),
             Muted, 38, Y + 38, GEngine->GetSmallFont(), 1.f);
    if (Rc && Rc->IsPanelOpen())
        DrawRcPanel(*Rc);
    DrawText(Bird->GetCameraLabel(), FLinearColor(.68f,.74f,.70f), 47, 110, GEngine->GetSmallFont(), .8f);
        if (bShowChannels) DrawChannels(*Bird);
}
void ABorn2FlapHUD::DrawMouseIndicator(const ABorn2FlapFlightPawn& Bird)
{
    // Dropped below the screen centre so it stays clear of the centred bird.
    const float CX = Canvas->SizeX * 0.5f;
    const float CY = Canvas->SizeY * 0.62f;
    const FVector2D Raw = Bird.GetMouseControl();
    const float Mag = FMath::Clamp(Raw.Size(), 0.f, 1.f);
    const FVector2D Input = Mag > 0.0001f ? Raw * (Mag / Raw.Size()) : FVector2D::ZeroVector;
    const FLinearColor Line(0.42f, 0.85f, 0.9f, 0.55f);

    // Centre dot — layered passes read as a soft glow, not a hard 2×2 block.
    DrawRect(FLinearColor(Line.R, Line.G, Line.B, Line.A * 0.22f), CX - 3.f, CY - 3.f, 6.f, 6.f);
    DrawRect(FLinearColor(Line.R, Line.G, Line.B, Line.A * 0.6f), CX - 1.5f, CY - 1.5f, 3.f, 3.f);
    DrawRect(Line, CX - 0.5f, CY - 0.5f, 1.f, 1.f);

    // Direction line: three strokes (wide faint → mid → crisp thin) approximate
    // anti-aliasing. Its length encodes the input magnitude (0…fixed radius).
    const float Reach = 90.f;
    const float DX = Input.X * Reach;
    const float DY = Input.Y * Reach;
    DrawLine(CX, CY, CX + DX, CY + DY, FLinearColor(Line.R, Line.G, Line.B, Line.A * 0.16f), 5.f);
    DrawLine(CX, CY, CX + DX, CY + DY, FLinearColor(Line.R, Line.G, Line.B, Line.A * 0.5f), 2.4f);
    DrawLine(CX, CY, CX + DX, CY + DY, Line, 1.f);

    // The outer ring has a FIXED diameter; only the section's angular span
    // follows the inner line's length (a sliver at rest, nearly a full circle
    // at full deflection), and the whole halo's presence scales with the line
    // too — so the ring is always an echo of the input, never a hard frame.
    const float Radius = 90.f;
    const float HalfSpan = FMath::DegreesToRadians(FMath::Lerp(5.f, 160.f, Mag));
    const float Presence = 0.08f + 0.92f * Mag;
    const FLinearColor Ring(0.42f, 0.85f, 0.9f, 0.34f * Presence);
    const float TouchAngle = FMath::Atan2(Input.Y, Input.X);
    const int32 Segments = 128;
    for (int32 I = 0; I < Segments; ++I)
    {
        const float A0 = -HalfSpan + (2.f * HalfSpan * I) / Segments;
        const float A1 = -HalfSpan + (2.f * HalfSpan * (I + 1)) / Segments;
        // Cosine falloff sampled at both endpoints so the section dissolves to
        // nothing at its two ends (no hard clip, no visible joints).
        const float Alpha = 0.5f * (FMath::Cos(FMath::Clamp(A0 / HalfSpan, -1.f, 1.f) * HALF_PI)
                                  + FMath::Cos(FMath::Clamp(A1 / HalfSpan, -1.f, 1.f) * HALF_PI));
        const float X0 = CX + FMath::Cos(TouchAngle + A0) * Radius;
        const float Y0 = CY + FMath::Sin(TouchAngle + A0) * Radius;
        const float X1 = CX + FMath::Cos(TouchAngle + A1) * Radius;
        const float Y1 = CY + FMath::Sin(TouchAngle + A1) * Radius;
        // Two layered passes smooth the arc edges (Canvas lines are aliased).
        DrawLine(X0, Y0, X1, Y1, FLinearColor(Ring.R, Ring.G, Ring.B, Ring.A * Alpha * 0.32f), 2.2f);
        DrawLine(X0, Y0, X1, Y1, FLinearColor(Ring.R, Ring.G, Ring.B, Ring.A * Alpha), 0.8f);
    }
    DrawText(TEXT("MOUSE"), FLinearColor(Line.R, Line.G, Line.B, Line.A * (0.3f + 0.7f * Mag)),
             CX + 8.f, CY + 8.f, GEngine->GetSmallFont(), .7f);
}
void ABorn2FlapHUD::DrawChannels(const ABorn2FlapFlightPawn& Bird)
{
    // The channel monitor in the glass-cockpit language: hairline frame, amber
    // head bar, dim labels, centred tracks with a soft halo under the fill,
    // and steady (frame-rate independent, damped) readouts.
    const float X = Canvas->SizeX - 272.f, Y = 143.f, W = 240.f, H = 152.f;
    const FLinearColor Ivory(.93f, .91f, .81f), Muted(.66f, .73f, .71f), Amber(.88f, .63f, .31f), Teal(.42f, .85f, .9f);
    const FLinearColor Ink(.012f, .022f, .020f, .62f);
    const auto* Rc = Bird.GetRcController();
    const bool Hardware = Rc && Rc->IsEnabled();

    // Glass panel: dark backing, hairline frame, amber head bar.
    DrawRect(Ink, X, Y, W, H);
    DrawRect(FLinearColor(.6f, .7f, .65f, .10f), X, Y + 2, W, 1);
    DrawRect(Amber, X, Y, W, 2);
    DrawRect(FLinearColor(.6f, .7f, .65f, .07f), X, Y, H - 1, 1);
    DrawRect(FLinearColor(.6f, .7f, .65f, .07f), X + W - 1, Y, 1, H);

    // Header: accent tick + title, F2 key hint quiet on the right.
    DrawRect(Amber, X + 14, Y + 12, 3, 14);
    DrawText(FString::Printf(TEXT("CHANNELS / %s"), Hardware ? TEXT("RC") : TEXT("KEYS")),
             Ivory, X + 24, Y + 9, GEngine->GetSmallFont(), .95f);
    DrawText(TEXT("F2"), Muted, X + W - 28, Y + 9, GEngine->GetSmallFont(), .8f);

    const FVector Sticks = Bird.GetRcSticks();
    const float Targets[] = { Bird.GetEffort(), float(Sticks.Y), float(Sticks.Z), float(Sticks.X) };
    const TCHAR* Names[] = { TEXT("THR"), TEXT("PIT"), TEXT("YAW"), TEXT("ROLL") };

    // Frame-rate independent damping so the bars read steadily.
    const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 1.f / 60.f;
    const float Blend = 1.f - FMath::Exp(-Dt * 9.f);

    const float TrackX = X + 56.f, TrackW = 96.f;
    for (int32 I = 0; I < 4; ++I)
    {
        ChannelDisplay[I] = FMath::Lerp(ChannelDisplay[I], Targets[I], Blend);
        const float Value = FMath::Clamp(ChannelDisplay[I], -1.f, 1.f);
        const float Row = Y + 34 + I * 23;
        const bool bBipolar = I > 0;
        const FLinearColor Fill = bBipolar ? Teal : Amber;

        // Label + dim track + centre tick for the bipolar channels.
        DrawText(Names[I], Muted, X + 14, Row - 2, GEngine->GetSmallFont(), .8f);
        DrawRect(FLinearColor(.5f, .6f, .55f, .10f), TrackX, Row + 4, TrackW, 3);
        const float Centre = bBipolar ? TrackX + TrackW * .5f : TrackX;
        const float End = bBipolar ? Centre + Value * TrackW * .5f : TrackX + Value * TrackW;
        DrawRect(FLinearColor(Fill.R, Fill.G, Fill.B, .20f), FMath::Min(Centre, End), Row + 2, FMath::Abs(End - Centre), 7);
        DrawRect(Fill, FMath::Min(Centre, End), Row + 4, FMath::Abs(End - Centre), 3);
        if (bBipolar)
            DrawRect(FLinearColor(Ivory.R, Ivory.G, Ivory.B, .5f), Centre, Row + 2, 1, 7);

        // Readout: sign quiet + digits bright (THR shows percent instead).
        if (I == 0)
        {
            DrawText(FString::Printf(TEXT("%.0f%%"), Value * 100.f), Ivory, X + 160, Row - 2,
                     GEngine->GetSmallFont(), .8f);
        }
        else
        {
            const FString Readout = FString::Printf(TEXT("%+.0f"), Value * 100.f);
            DrawText(Readout.Left(1), Muted, X + 160, Row - 2, GEngine->GetSmallFont(), .8f);
            DrawText(Readout.Mid(1), Ivory, X + 165, Row - 2, GEngine->GetSmallFont(), .8f);
        }
    }

    // Footer: hairline + throttle coupling + speed modifier.
    DrawRect(FLinearColor(.6f, .7f, .65f, .10f), X + 14, Y + 126, W - 28, 1);
    DrawText(FString::Printf(TEXT("THR %s    SPEED %3.0f%%"),
             Bird.IsThrottleCoupled() ? TEXT("COUPLED") : TEXT("INDEP"),
             Bird.GetSpeedModifier() * 100),
             Muted, X + 14, Y + 130, GEngine->GetSmallFont(), .72f);
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
    for (int32 I = 0; I < 5; ++I)
        DrawText(Rc.GetMapping(I), Cream, X + Width * .52, Y + 223 + I * 37, GEngine->GetSmallFont(), 1.1f);
    Text(Rc.GetButtons(), 420, Muted);
    Text(Rc.Notice, 450, Accent);
    Text(TEXT("TAB Geraet   C Kalibrieren   ENTER Weiter   X Abbrechen   G RC/Tastatur   F3 Schliessen"), 493);
    Text(TEXT("Kanaele werden pro Geraet gespeichert. Nach Verbindungsabbruch Gas kurz auf Minimum."), 525, Muted);
}