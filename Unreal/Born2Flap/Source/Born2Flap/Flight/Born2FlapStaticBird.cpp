#include "Flight/Born2FlapStaticBird.h"
#include "Flight/Born2FlapRavenMesh.h"
#include "Components/SceneComponent.h"

ABorn2FlapStaticBird::ABorn2FlapStaticBird()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ABorn2FlapStaticBird::BuildKestrel()
{
    // Use the same authored body, membrane and tail construction as flight.
    if (GetRootComponent()->GetNumChildrenComponents()) return;
    TObjectPtr<USceneComponent> Left, Right, Tail;
    Born2FlapRaven::Build(this, GetRootComponent(), Left, Right, Tail, 2);
}
