#include "World/Born2FlapWeather.h"
#include "World/Born2FlapWind.h"
#include "Misc/ConfigCacheIni.h"

namespace Born2FlapWeather
{
const TArray<FBorn2FlapWeather>& Profiles()
{
    static const TArray<FBorn2FlapWeather> Values={
        {TEXT("sunny"),TEXT("Sunny"),2.2f,.65f,.65f,.08f,.001f,1.f},
        {TEXT("cloudy"),TEXT("Cloudy"),3.4f,1.1f,.20f,.72f,.004f,.45f},
        {TEXT("rain"),TEXT("Gentle rain"),3.8f,1.25f,.05f,.92f,.009f,.28f,true},
        {TEXT("mist"),TEXT("Mist"),.7f,.22f,.02f,.35f,.045f,.45f},
        {TEXT("snow"),TEXT("Light snow"),1.8f,.65f,.02f,.85f,.015f,.40f,false,true},
        {TEXT("parhelion"),TEXT("Parhelion"),3.2f,.3f,.35f,.12f,.0008f,1.f,false,false,true}
    };
    return Values;
}
const TArray<FBorn2FlapDayTime>& DayTimes()
{
    static const TArray<FBorn2FlapDayTime> Values={
        {TEXT("dawn"),TEXT("Dawn"),6.f,-135.f,18000.f,11.2f},
        {TEXT("morning"),TEXT("Morning"),10.f,-90.f,70000.f,13.2f},
        {TEXT("noon"),TEXT("Midday"),52.f,-65.f,100000.f,14.f},
        {TEXT("sunset"),TEXT("Sunset"),5.f,-20.f,14000.f,10.8f},
        {TEXT("night"),TEXT("Moonlit night"),22.f,45.f,80.f,4.5f,true}
    };
    return Values;
}
TArray<FString> WeatherOptions(const FString& Level)
{
    if(Level==TEXT("Training")) return {TEXT("sunny"),TEXT("cloudy"),TEXT("rain")};
    if(Level==TEXT("Shiomori")) return {TEXT("sunny"),TEXT("cloudy"),TEXT("rain"),TEXT("mist"),TEXT("parhelion")};
    // Parhelion is a Shiomori-Bay-only phenomenon (the ice-halo weather). The
    // default nature level (Ravenstonefield) never offers it.
    return {TEXT("sunny"),TEXT("cloudy"),TEXT("rain"),TEXT("mist"),TEXT("snow")};
}
TArray<FString> TimeOptions(const FString& Level,const FString& Weather)
{
    if(Level==TEXT("Training")) return {TEXT("morning"),TEXT("noon"),TEXT("sunset")};
    if(Weather==TEXT("parhelion")) return {TEXT("dawn"),TEXT("morning"),TEXT("noon"),TEXT("sunset")};
    return {TEXT("dawn"),TEXT("morning"),TEXT("noon"),TEXT("sunset"),TEXT("night")};
}
FBorn2FlapConditions Defaults(const FString& Level)
{
    return {Level==TEXT("Shiomori") ? TEXT("parhelion") : TEXT("sunny"),
            Level==TEXT("Training") ? TEXT("noon") : TEXT("morning")};
}
FBorn2FlapConditions Validate(const FString& Level,const FBorn2FlapConditions& C)
{
    const auto D=Defaults(Level);
    const FString Weather=WeatherOptions(Level).Contains(C.Weather) ? C.Weather : D.Weather;
    return {Weather,TimeOptions(Level,Weather).Contains(C.Time) ? C.Time : D.Time};
}
const FBorn2FlapWeather& Profile(const FString& Id)
{
    for(const auto& P:Profiles()) if(P.Id==Id) return P;
    return Profiles()[0];
}
const FBorn2FlapDayTime& DayTime(const FString& Id)
{
    for(const auto& P:DayTimes()) if(P.Id==Id) return P;
    return DayTimes()[1];
}
FBorn2FlapConditions Load(const FString& Level)
{
    auto C=Defaults(Level);
    const FString Section=TEXT("Born2Flap.Weather.")+Level;
    GConfig->GetString(*Section,TEXT("Weather"),C.Weather,GGameUserSettingsIni);
    GConfig->GetString(*Section,TEXT("Time"),C.Time,GGameUserSettingsIni);
    return Validate(Level,C);
}
void Save(const FString& Level,const FBorn2FlapConditions& C)
{
    const auto Valid=Validate(Level,C);
    const FString Section=TEXT("Born2Flap.Weather.")+Level;
    GConfig->SetString(*Section,TEXT("Weather"),*Valid.Weather,GGameUserSettingsIni);
    GConfig->SetString(*Section,TEXT("Time"),*Valid.Time,GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
}
FString TravelOptions(const FString& Level,const FBorn2FlapConditions& C)
{
    const auto Valid=Validate(Level,C);
    // Unreal options are delimited by '?', not web-style '&'.
    return FString::Printf(TEXT("Level=%s?SkipMenu=1?Weather=%s?DayTime=%s"),*Level,*Valid.Weather,*Valid.Time);
}
float Exposure(const FBorn2FlapConditions& C)
{
    return DayTime(C.Time).Exposure + FMath::Log2(FMath::Max(.25f,Profile(C.Weather).Sunlight))*.5f;
}
FVector Wind(const FString& Level,const FBorn2FlapConditions& C,const FVector& P,double Time)
{
    const auto& W=Profile(C.Weather);
    const auto& D=DayTime(C.Time);
    const double Strength=W.WindSpeed*(Level==TEXT("Training") ? .5 : 1.)*(D.Night ? .65 : 1.);
    const double Heading=FMath::DegreesToRadians(Level==TEXT("Shiomori") ? -85. : (Level==TEXT("Training") ? 15. : 35.));
    const double Drift=.12*FMath::Sin(Time*.018);
    const double GX=FMath::PerlinNoise3D(FVector(P.X*.0008,P.Y*.0008,Time*.18));
    const double GY=FMath::PerlinNoise3D(FVector(P.X*.0008+71,P.Y*.0008-24,Time*.15+18));
    FVector Result(FMath::Cos(Heading+Drift)*Strength+GX*W.Gust,
                   FMath::Sin(Heading+Drift)*Strength+GY*W.Gust,0);
    if(Level==TEXT("Shiomori"))
    {
        Result.Z=.85*(Strength/3.2)*FMath::Exp(-FMath::Square((P.Y+1400)/650.))*
            FMath::Exp(-FMath::Max(0.,P.Z-150)/650.)*(1-FMath::SmoothStep(43000.,46000.,FMath::Abs(P.X)));
    }
    else if(Level!=TEXT("Training"))
        Result.Z=Born2FlapWind::Sample(P,Time).Z*W.Thermals*(D.Night ? .02 : (D.Id==TEXT("noon") ? 1. : .45));
    return Result;
}
}