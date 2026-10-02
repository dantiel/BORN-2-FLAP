// BORN 2 FLAP — Shiomori Bay coastal water system.
//
// Central, data-driven ocean parameters and the shared Gerstner height field.
// This is the single source of truth that keeps the base ocean, the shore
// breakers, the foam and the rock interaction all synchronized.
//
// Units: world centimetres (Unreal units) and seconds. Directions are degrees
// in the XY plane (0 = +X, counter-clockwise positive). The Shiomori sea lies
// in +Y; the shore faces -Y, so the default primary wave direction (270 deg)
// travels shoreward.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Born2FlapWater.generated.h"

class UMaterialParameterCollection;

// ---------------------------------------------------------------------------
// Global ocean parameters. Exposed both inline (on the director actor) and
// through UOceanParametersAsset so the same block can be authored as an asset.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FOceanParameters
{
    GENERATED_BODY()

    // Still-water surface height in world Z (cm).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean")
    float OceanLevel = -50.0f;

    // Travel direction of the dominant large-scale swell (degrees, 0 = +X CCW).
    // Shiomori default 270 -> travels -Y, i.e. toward the beach.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean")
    float PrimaryWaveDirection = 270.0f;

    // Direction the wind blows TOWARD (degrees, 0 = +X CCW).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean")
    float WindDirection = 270.0f;

    // Wind speed in m/s (drives chop amplitude and foam density).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0"))
    float WindSpeed = 6.0f;

    // Dominant swell amplitude (cm) and wavelength (cm).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0"))
    float SwellAmplitude = 44.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "1.0"))
    float SwellLength = 4000.0f;

    // Phase speed of the swell (cm/s). Anchors the dispersion of every octave.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0"))
    float SwellSpeed = 530.0f;

    // Beaufort-like overall sea state 0..6 (scales wave amplitude + foam).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0", ClampMax = "6.0"))
    float SeaState = 3.0f;

    // Global multipliers (>= 0). StormAmount cross-fades toward heavy sea.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0"))
    float FoamAmount = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float StormAmount = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean", meta = (ClampMin = "0.0"))
    float ShoreBreakIntensity = 1.0f;

    // --- Derived helpers ---------------------------------------------------
    float SeaStateScale() const { return FMath::Lerp(0.35f, 1.6f, SeaState / 6.0f); }
    FVector2D WaveDirection() const { return DirectionVector(PrimaryWaveDirection); }
    FVector2D WindDir() const { return DirectionVector(WindDirection); }

    static FVector2D DirectionVector(float Degrees)
    {
        const float Rad = FMath::DegreesToRadians(Degrees);
        return FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
    }

    // Height displacement (cm) of the deterministic Gerstner spectrum above
    // OceanLevel at world XY (cm) at time T (s). Fewer than ten components:
    // one swell, three wind waves, four surface-detail waves. Kept in lock-step
    // with the water material's HLSL (see create_shiomori_map.py).
    float SampleWaveHeight(const FVector2D& P, float Time) const;
};

// ---------------------------------------------------------------------------
// Authorable settings asset wrapping the parameter block.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType)
class BORN2FLAP_API UOceanParametersAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean")
    FOceanParameters Parameters;
};

// ---------------------------------------------------------------------------
// Material Parameter Collection parameter names. The director writes these
// every frame; the water/foam/breaker materials read them via CollectionParameter
// nodes, so the strings must match create_shiomori_map.py and any material.
// ---------------------------------------------------------------------------
namespace Born2FlapWater
{
    extern BORN2FLAP_API const FName OceanLevel;
    extern BORN2FLAP_API const FName PrimaryWaveDirection;
    extern BORN2FLAP_API const FName WindDirection;
    extern BORN2FLAP_API const FName WindSpeed;
    extern BORN2FLAP_API const FName SwellAmplitude;
    extern BORN2FLAP_API const FName SwellLength;
    extern BORN2FLAP_API const FName SwellSpeed;
    extern BORN2FLAP_API const FName SeaState;
    extern BORN2FLAP_API const FName FoamAmount;
    extern BORN2FLAP_API const FName StormAmount;
    extern BORN2FLAP_API const FName ShoreBreakIntensity;
    extern BORN2FLAP_API const FName WaveTime;
    extern BORN2FLAP_API const FName QualityLevel;
    extern BORN2FLAP_API const FName WaterLOD;
}
