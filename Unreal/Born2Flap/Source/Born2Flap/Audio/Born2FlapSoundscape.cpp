#include "Audio/Born2FlapSoundscape.h"
#include "Game/Born2FlapGameMode.h"
#include "Water/Born2FlapSurf.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

ABorn2FlapSoundscape::ABorn2FlapSoundscape()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("SoundscapeRoot"));
    Bed=CreateDefaultSubobject<UAudioComponent>(TEXT("WorldBed"));
    Rain=CreateDefaultSubobject<UAudioComponent>(TEXT("Rainfall"));
    Surf=CreateDefaultSubobject<UAudioComponent>(TEXT("SurfWash"));
    Wildlife=CreateDefaultSubobject<UAudioComponent>(TEXT("Wildlife"));
    for(auto* C:{Bed.Get(),Rain.Get(),Surf.Get(),Wildlife.Get()})
    {
        C->SetupAttachment(RootComponent);C->bAutoActivate=false;
        C->bAllowSpatialization=false;C->SetVolumeMultiplier(0);
    }
    Wildlife->bAllowSpatialization=true;
    Wildlife->bOverrideAttenuation=true;
    Wildlife->AttenuationOverrides.bAttenuate=true;
    Wildlife->AttenuationOverrides.bSpatialize=true;
    Wildlife->AttenuationOverrides.AttenuationShapeExtents=FVector(2500);
    Wildlife->AttenuationOverrides.FalloffDistance=16000;
}
void ABorn2FlapSoundscape::BeginPlay()
{
    Super::BeginPlay();
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    if(!GM) return;
    Level=GM->GetLevelId();
    auto Wave=[](const TCHAR* Name){return LoadObject<USoundWave>(nullptr,*(FString(TEXT("/Game/Weather/Audio/"))+Name));};
    Bed->SetSound(Wave(Level==TEXT("Shiomori") ? TEXT("CoastalWind") : (Level==TEXT("Training") ? TEXT("MeadowWind") : TEXT("ForestWind"))));
    Rain->SetSound(Wave(TEXT("Rain")));
    Surf->SetSound(Wave(TEXT("Surf")));
    DayCall=Wave(Level==TEXT("Shiomori") ? TEXT("Seagull") : (Level==TEXT("Training") ? TEXT("Songbird") : TEXT("Raven")));
    NightCall=Wave(TEXT("Crickets"));
    Bed->Play();Rain->Play();
    if(Level==TEXT("Shiomori")) Surf->Play();
    bStarted=true;
}
void ABorn2FlapSoundscape::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* GM=Cast<ABorn2FlapGameMode>(GetWorld()->GetAuthGameMode());
    auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
    if(!GM || !Camera || !bStarted) return;
    const auto C=GM->GetConditions();const auto& W=Born2FlapWeather::Profile(C.Weather);
    const bool Night=Born2FlapWeather::DayTime(C.Time).Night;
    const FVector P=Camera->GetCameraLocation();
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(AmbientShelter),false,this);
    Query.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this,0));
    const bool Covered=GetWorld()->LineTraceSingleByChannel(Hit,P,P+FVector(0,0,20000),ECC_Visibility,Query);
    Shelter=FMath::FInterpTo(Shelter,Covered ? 1.f : 0.f,Dt,3.f);
    const float Wind=GM->WindAt(P,GetWorld()->GetTimeSeconds()).Size2D();
    Bed->SetVolumeMultiplier((.12f+Wind*.025f)*(1-Shelter*.7f)*(W.Snow ? .7f : 1.f));
    Rain->SetVolumeMultiplier(W.Rain ? .42f*(1-Shelter*.82f) : 0.f);
    Rain->SetLowPassFilterEnabled(Covered);Rain->SetLowPassFilterFrequency(1800);
    if(Level==TEXT("Shiomori"))
    {
        const double DY=P.Y-Born2FlapSurf::Shore(P.X);
        const double Distance=FMath::Sqrt(DY*DY+P.Z*P.Z);
        const float Near=FMath::Exp(-Distance/16000.);
        const float Wash=Born2FlapSurf::Wash(P.X,GetWorld()->GetTimeSeconds());
        Surf->SetVolumeMultiplier(Near*(.22f+.38f*Wash)*(1-Shelter*.7f));
    }
    NextCall-=Dt;
    if(NextCall<=0 && !Wildlife->IsPlaying())
    {
        NextCall=Random.FRandRange(13,32)*(W.Rain ? 1.6f : 1.f);
        // Snow stays quiet; nocturnal insects replace daytime birds.
        if(!W.Snow && (Night || !W.Rain || Random.FRand()<.35f))
        {
            Wildlife->SetSound(Night ? NightCall.Get() : DayCall.Get());
            Wildlife->SetWorldLocation(P+FVector(Random.FRandRange(-6500,6500),Random.FRandRange(-4000,5000),Night ? -100 : 1600));
            Wildlife->SetVolumeMultiplier((Night ? .18f : .32f)*(1-Shelter*.8f));
            Wildlife->SetPitchMultiplier(Random.FRandRange(.90f,1.12f));Wildlife->Play();
        }
    }
}
bool ABorn2FlapSoundscape::ValidateAudio() const
{
    return bStarted && Bed->Sound && Rain->Sound && Surf->Sound && DayCall && NightCall && Bed->IsPlaying();
}
