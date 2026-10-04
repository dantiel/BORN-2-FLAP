// Born2FlapPoiBeacon.cpp — implementation. See the header for the design note.

#include "Game/Born2FlapPoiBeacon.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"

ABorn2FlapPoiBeacon::ABorn2FlapPoiBeacon()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("PoiRoot"));
    SetRootComponent(Root);

    // A warm amber glow marks the reset point from far away. No external
    // material or mesh is needed, so it survives a clean cook and all levels.
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("PoiLight"));
    Light->SetupAttachment(Root);
    Light->SetRelativeLocation(FVector(0, 0, 400));
    Light->SetIntensity(4000.f);
    Light->SetAttenuationRadius(4000.f);
    Light->SetLightColor(FLinearColor(1.f, 0.62f, 0.24f)); // cinnabar
    Light->bUseInverseSquaredFalloff = false;
    Light->SetLightFalloffExponent(2.5f);
    Light->SetCastShadows(false);
    Light->SetMobility(EComponentMobility::Movable);

    // The floating name. Text render needs no material and matches the existing
    // generated signage (Sign() in Born2FlapRavenLandmarks.cpp).
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PoiLabel"));
    Label->SetupAttachment(Root);
    Label->SetRelativeLocation(FVector(0, 0, 900));
    Label->SetWorldSize(140.f);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetTextRenderColor(FColor(212, 202, 169));
    Label->SetCastShadow(false);
    Label->SetMobility(EComponentMobility::Movable);
    Label->bAlwaysRenderAsText = true;
}

void ABorn2FlapPoiBeacon::Initialize(const FString& InLabel, const FVector& InPosition)
{
    SetActorLocation(InPosition);
    Label->SetText(FText::FromString(InLabel));
    SetSelected(false);
}

void ABorn2FlapPoiBeacon::SetSelected(bool bSelected)
{
    // The active reset point glows white-hot; inactive points stay cinnabar.
    Label->SetTextRenderColor(bSelected ? FColor(255, 244, 214) : FColor(212, 202, 169));
    Light->SetLightColor(bSelected ? FLinearColor(1.f, 0.92f, 0.70f) : FLinearColor(1.f, 0.62f, 0.24f));
    Light->SetIntensity(bSelected ? 9000.f : 4000.f);
}
