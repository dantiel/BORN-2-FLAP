#include "Game/Born2FlapGameMode.h"
#include "Flight/Born2FlapFlightPawn.h"

ABorn2FlapGameMode::ABorn2FlapGameMode()
{
    DefaultPawnClass = ABorn2FlapFlightPawn::StaticClass();
}
