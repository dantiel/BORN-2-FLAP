#include "World/Born2FlapWeather.h"
#include "World/Born2FlapWind.h"
#include "World/Born2FlapBayTerrain.h"
#include "World/Born2FlapValley.h"
#include "World/Born2FlapYunsha.h"
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
const TArray<FBorn2FlapSeason>& Seasons()
{
    // One row per season. These are the hidden "stage-art" numbers: the player
    // only ever picks the season id; everything below is hermetically derived.
    //   SunAzimuth/Elevation bend the sun path, SunLux + LightTint bend its
    //   spectrum (cool winter, warm summer), FogBias/ExposureBias shift the air.
    //   InsectFactor/LeafFactor/LeafShed reshape the living world. NativeWeather
    //   names the weather the season drifts toward; SnowBias deepens winter snow.
    //   RadioCue is the (future) seasonal soundtrack playlist hint.
    static const TArray<FBorn2FlapSeason> Values={
        // Id, Label,  SunAz, SunElev, SunLux, LightTint,             SkyInt, FogBias, ExpBias, InsectF, LeafF, LeafShed, LeafTint,                NativeWeather, SnowBias, RadioCue
        { TEXT("spring"), TEXT("Spring"), -12.f,  4.f, 1.05f, FLinearColor(1.00f,.97f,.88f), 1.10f, -.002f,  .15f, 1.35f,  .85f,  .55f, FLinearColor(.55f,.85f,.32f), TEXT("mist"),   0.00f, TEXT("spring") },
        { TEXT("summer"), TEXT("Summer"),   8.f, 12.f, 1.20f, FLinearColor(1.00f,.91f,.76f), 1.20f,  .000f,  .30f, 1.65f, 1.00f,  .40f, FLinearColor(.18f,.62f,.22f), TEXT("sunny"),  0.00f, TEXT("summer") },
        { TEXT("autumn"), TEXT("Autumn"),  15.f, -8.f,  .85f, FLinearColor(1.00f,.76f,.52f),  .95f,  .006f, -.10f,  .80f, 1.85f, 1.60f, FLinearColor(.78f,.45f,.15f), TEXT("mist"),   0.00f, TEXT("autumn") },
        { TEXT("winter"), TEXT("Winter"), -20.f,-16.f,  .60f, FLinearColor(.72f,.83f,1.00f),  .80f,  .004f, -.25f,  .22f,  .18f,  .10f, FLinearColor(.62f,.60f,.58f), TEXT("snow"),   1.00f, TEXT("winter") },
        { TEXT("dry"), TEXT("Dry"), 0.f, 6.f, 1.15f, FLinearColor(1.f,.91f,.76f), .9f, .001f, .1f, 0.f, 0.f, 0.f, FLinearColor(.7f,.6f,.3f), TEXT("sunny"), 0.f, TEXT("dry") },
        { TEXT("monsoon"), TEXT("Monsoon"), 0.f, -3.f, .6f, FLinearColor(.72f,.79f,1.f), .8f, .009f, -.2f, .1f, .1f, 0.f, FLinearColor(.4f,.6f,.2f), TEXT("rain"), 0.f, TEXT("monsoon") },
        { TEXT("cool"), TEXT("Cool"), -12.f, -12.f, .75f, FLinearColor(.83f,.9f,1.f), 1.f, -.001f, 0.f, 0.f, 0.f, 0.f, FLinearColor(.5f,.6f,.3f), TEXT("sunny"), 0.f, TEXT("cool") },
    };
    return Values;
}
TArray<FString> WeatherOptions(const FString& Level)
{
    if(Level==TEXT("Yunsha")) return {TEXT("sunny"),TEXT("cloudy"),TEXT("rain"),TEXT("mist")};
    if(Level==TEXT("Training")) return {TEXT("sunny"),TEXT("cloudy"),TEXT("rain")};
        if(Level==TEXT("Shiomori")) return {TEXT("sunny"),TEXT("cloudy"),TEXT("rain"),TEXT("mist"),TEXT("snow"),TEXT("parhelion")};
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
// Seasons are "stage art": the neutral Training gymnasium is hermetically fixed
// to a single season (it has no weather to speak of), while the two open worlds
// offer the full four-season cycle.
TArray<FString> SeasonOptions(const FString& Level)
{
    if(Level==TEXT("Yunsha")) return {TEXT("dry"),TEXT("monsoon"),TEXT("cool")};
    if(Level==TEXT("Training")) return {TEXT("spring")};
    return {TEXT("spring"),TEXT("summer"),TEXT("autumn"),TEXT("winter")};
}
FString DefaultSeason(const FString& Level)
{
    if(Level==TEXT("Yunsha")) return TEXT("dry");
    if(Level==TEXT("Shiomori")) return TEXT("summer");
    if(Level==TEXT("Training")) return TEXT("spring");
    return TEXT("autumn"); // Ravenstonefield â€” its register is "quiet autumn nature"
}
FBorn2FlapConditions Defaults(const FString& Level)
{
    FBorn2FlapConditions C;
    C.Weather = Level==TEXT("Shiomori") ? TEXT("parhelion") : TEXT("sunny");
    C.Time = Level==TEXT("Training") ? TEXT("noon") : TEXT("morning");
    C.Season = DefaultSeason(Level);
    return C;
}
FBorn2FlapConditions Validate(const FString& Level,const FBorn2FlapConditions& C)
{
    const auto D=Defaults(Level);
    const FString Weather=WeatherOptions(Level).Contains(C.Weather) ? C.Weather : D.Weather;
    FBorn2FlapConditions R=C;   // preserve the wind overrides
    R.Weather=Weather;
    R.Time=TimeOptions(Level,Weather).Contains(C.Time) ? C.Time : D.Time;
    R.Season=SeasonOptions(Level).Contains(C.Season) ? C.Season : D.Season;
    // Clamp explicit wind overrides to sane ranges; keep negative as "auto".
    if(R.WindDir>=0.f)     R.WindDir=FMath::Fmod(R.WindDir+360.f,360.f);
    if(R.WindSpeed>=0.f)   R.WindSpeed=FMath::Clamp(R.WindSpeed,0.f,5.f);
    if(R.WindGust>=0.f)    R.WindGust=FMath::Clamp(R.WindGust,0.f,8.f);
    if(R.WindRandom>=0.f)  R.WindRandom=FMath::Clamp(R.WindRandom,0.f,8.f);
    if(R.WindThermal>=0.f) R.WindThermal=FMath::Clamp(R.WindThermal,0.f,5.f);
    return R;
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
const FBorn2FlapSeason& Season(const FString& Id)
{
    for(const auto& P:Seasons()) if(P.Id==Id) return P;
    return Seasons()[0];
}
FBorn2FlapConditions Load(const FString& Level)
{
    auto C=Defaults(Level);
    const FString Section=TEXT("Born2Flap.Weather.")+Level;
    GConfig->GetString(*Section,TEXT("Weather"),C.Weather,GGameUserSettingsIni);
    GConfig->GetString(*Section,TEXT("Time"),C.Time,GGameUserSettingsIni);
    GConfig->GetString(*Section,TEXT("Season"),C.Season,GGameUserSettingsIni);
    GConfig->GetFloat(*Section,TEXT("WindDir"),C.WindDir,GGameUserSettingsIni);
    GConfig->GetFloat(*Section,TEXT("WindSpeed"),C.WindSpeed,GGameUserSettingsIni);
    GConfig->GetFloat(*Section,TEXT("WindGust"),C.WindGust,GGameUserSettingsIni);
    GConfig->GetFloat(*Section,TEXT("WindRandom"),C.WindRandom,GGameUserSettingsIni);
    GConfig->GetFloat(*Section,TEXT("WindThermal"),C.WindThermal,GGameUserSettingsIni);
    return Validate(Level,C);
}
void Save(const FString& Level,const FBorn2FlapConditions& C)
{
    const auto Valid=Validate(Level,C);
    const FString Section=TEXT("Born2Flap.Weather.")+Level;
    GConfig->SetString(*Section,TEXT("Weather"),*Valid.Weather,GGameUserSettingsIni);
    GConfig->SetString(*Section,TEXT("Time"),*Valid.Time,GGameUserSettingsIni);
    GConfig->SetString(*Section,TEXT("Season"),*Valid.Season,GGameUserSettingsIni);
    GConfig->SetFloat(*Section,TEXT("WindDir"),Valid.WindDir,GGameUserSettingsIni);
    GConfig->SetFloat(*Section,TEXT("WindSpeed"),Valid.WindSpeed,GGameUserSettingsIni);
    GConfig->SetFloat(*Section,TEXT("WindGust"),Valid.WindGust,GGameUserSettingsIni);
    GConfig->SetFloat(*Section,TEXT("WindRandom"),Valid.WindRandom,GGameUserSettingsIni);
    GConfig->SetFloat(*Section,TEXT("WindThermal"),Valid.WindThermal,GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
}
FString TravelOptions(const FString& Level,const FBorn2FlapConditions& C)
{
    const auto Valid=Validate(Level,C);
    // Unreal options are delimited by '?', not web-style '&'.
    FString Options=FString::Printf(TEXT("Level=%s?SkipMenu=1?Weather=%s?DayTime=%s?Season=%s"),*Level,*Valid.Weather,*Valid.Time,*Valid.Season);
    auto Add=[&](const TCHAR* Key,float Value){ if(Value>=0.f) Options+=FString::Printf(TEXT("?%s=%s"),Key,*FString::SanitizeFloat(Value)); };
    Add(TEXT("WindDir"),Valid.WindDir);
    Add(TEXT("WindSpeed"),Valid.WindSpeed);
    Add(TEXT("WindGust"),Valid.WindGust);
    Add(TEXT("WindRandom"),Valid.WindRandom);
    Add(TEXT("WindThermal"),Valid.WindThermal);
    return Options;
}
float Exposure(const FBorn2FlapConditions& C)
{
    // Season exposure bias rides the same EV curve as time-of-day and sunlight,
    // so a low winter sun darkens the whole scene rather than just the disc.
    return DayTime(C.Time).Exposure + FMath::Log2(FMath::Max(.25f,Profile(C.Weather).Sunlight))*.5f + Season(C.Season).ExposureBias;
}
float DefaultWindDirection(const FString& Level)
{
    return Level==TEXT("Shiomori") ? -85.f : (Level==TEXT("Training") ? 15.f : 35.f);
}

FVector Wind(const FString& Level,const FBorn2FlapConditions& C,const FVector& P,double Time)
{
    const auto& W=Profile(C.Weather);
    const auto& D=DayTime(C.Time);
    // Explicit (>=0) overrides layer on top of the weather/time defaults;
    // negative values mean "auto" (follow the profile).
    const double SpeedScale   = C.WindSpeed>=0.   ? (double)C.WindSpeed   : 1.;
    const double GustScale    = C.WindGust>=0.    ? (double)C.WindGust    : 1.;
    const double RandomScale  = C.WindRandom>=0.  ? (double)C.WindRandom  : 1.;
    const double ThermalScale = C.WindThermal>=0. ? (double)C.WindThermal : 1.;
    const double Strength=W.WindSpeed*(Level==TEXT("Training") ? .5 : 1.)*(D.Night ? .65 : 1.)*SpeedScale;
    const double HeadingDeg = C.WindDir>=0. ? (double)C.WindDir : (double)DefaultWindDirection(Level);
    const double Heading=FMath::DegreesToRadians(HeadingDeg);
    // Directional wander: a slow deterministic swing plus a perlin drift, both
    // scaled by the randomness override.
    const double Drift=.12*RandomScale*FMath::Sin(Time*.018)+.06*RandomScale*FMath::PerlinNoise1D((float)(Time*.05));
    const double GX=FMath::PerlinNoise3D(FVector(P.X*.0008,P.Y*.0008,Time*.18));
    const double GY=FMath::PerlinNoise3D(FVector(P.X*.0008+71,P.Y*.0008-24,Time*.15+18));
    FVector Result(FMath::Cos(Heading+Drift)*Strength+GX*W.Gust*GustScale,
                   FMath::Sin(Heading+Drift)*Strength+GY*W.Gust*GustScale,0);
    if(Level==TEXT("Shiomori"))
    {
        Result*=Born2FlapBay::WindExposure(P,Result);
        // Wind piles against the sea wall, headlands and hinterland slopes: the
        // upwash is derived from actual obstacle height (direction-aware).
        Result.Z=Born2FlapWind::WindwardLift(P,Result,&Born2FlapBay::TopHeight);
    }
    else if(Level==TEXT("Yunsha"))
    {
        Result=ABorn2FlapYunsha::SoaringWind(P,Time,C,Result);
    }
    else if(Level!=TEXT("Training"))
    {
        Result.Z=Born2FlapWind::Sample(P,Time).Z*W.Thermals*ThermalScale*(D.Night ? .02 : (D.Id==TEXT("noon") ? 1. : .45));
        // Terrain, buildings and the treeline add real windward lift.
        Result.Z+=Born2FlapWind::WindwardLift(P,Result,&ABorn2FlapValley::TopHeight);
    }
    return Result;
}
}