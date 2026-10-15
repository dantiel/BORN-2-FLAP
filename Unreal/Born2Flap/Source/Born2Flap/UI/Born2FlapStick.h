// Born2FlapStick.h — a self-drawn RC transmitter gimbal (UMG custom widget).
//
// Replaces the RC panel's old flat axis readouts with a real stick: a dark
// bezel, crosshair, a shaft that leans toward the current deflection and a
// knob at the tip — the way a physical transmitter gimbal reads at a glance.
// X/Y are normalized to [-1,1]; +Y deflects the knob *up* on screen (push
// forward = knob up), matching the pilot's mental model. No texture asset, no
// world actor: it lives inside the RC panel and is driven purely by telemetry.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Born2FlapStick.generated.h"

class SBorn2FlapStick : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBorn2FlapStick) {}
        SLATE_ARGUMENT(float, X)
        SLATE_ARGUMENT(float, Y)
        SLATE_ARGUMENT(FLinearColor, Color)
        SLATE_ARGUMENT(float, StickSize)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        X = InArgs._X;
        Y = InArgs._Y;
        Color = InArgs._Color;
        StickSize = InArgs._StickSize;
    }

    void SetX(float InX)
    {
        if (FMath::IsNearlyEqual(X, InX)) return;
        X = InX;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetY(float InY)
    {
        if (FMath::IsNearlyEqual(Y, InY)) return;
        Y = InY;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetColor(const FLinearColor& InColor)
    {
        if (Color == InColor) return;
        Color = InColor;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetStickSize(float InSize)
    {
        if (FMath::IsNearlyEqual(StickSize, InSize)) return;
        StickSize = InSize;
        Invalidate(EInvalidateWidgetReason::Layout);
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(StickSize, StickSize);
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        const float S = FMath::Max(1.f, FMath::Min(Size.X, Size.Y));
        const FVector2D Center = Size * 0.5f;
        const float Half = S * 0.5f;
        const float CornerRadius = S * 0.14f;
        const FVector2D BezelPos(Center.X - Half, Center.Y - Half);
        const FVector2D BezelSize(S, S);

        // 1. Bezel (dark rounded square).
        {
            FSlateBrush Bezel = FSlateRoundedBoxBrush(HousingColor, CornerRadius);
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
                AllottedGeometry.ToPaintGeometry(BezelSize, FSlateLayoutTransform(BezelPos)),
                &Bezel, ESlateDrawEffect::None, FLinearColor::White);
        }

        // 2. Bezel outline.
        {
            FSlateBrush Outline = FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.f),
                CornerRadius, OutlineColor, 1.5f);
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
                AllottedGeometry.ToPaintGeometry(BezelSize, FSlateLayoutTransform(BezelPos)),
                &Outline, ESlateDrawEffect::None, FLinearColor::White);
        }

        // 3. Crosshair (dim centre guide).
        {
            const float Reach = Half * 0.90f;
            const float HairThick = FMath::Max(1.f, S * 0.008f);
            TArray<FVector2D> H;
            H.Add(FVector2D(Center.X - Reach, Center.Y));
            H.Add(FVector2D(Center.X + Reach, Center.Y));
            TArray<FVector2D> V;
            V.Add(FVector2D(Center.X, Center.Y - Reach));
            V.Add(FVector2D(Center.X, Center.Y + Reach));
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2,
                AllottedGeometry.ToPaintGeometry(), H,
                ESlateDrawEffect::None, CrosshairColor, true, HairThick);
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2,
                AllottedGeometry.ToPaintGeometry(), V,
                ESlateDrawEffect::None, CrosshairColor, true, HairThick);
        }

        // 4. Deflection → knob position (screen Y grows downward; +Y goes up).
        const float Range = Half * 0.66f;
        const FVector2D Knob = Center + FVector2D(
            FMath::Clamp(X, -1.f, 1.f) * Range,
            -FMath::Clamp(Y, -1.f, 1.f) * Range);

        // 5. Shaft from centre to knob.
        {
            const float Thick = FMath::Max(2.f, S * 0.045f);
            TArray<FVector2D> Shaft;
            Shaft.Add(Center);
            Shaft.Add(Knob);
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3,
                AllottedGeometry.ToPaintGeometry(), Shaft,
                ESlateDrawEffect::None, Color, true, Thick);
        }

        // 6. Knob (filled disc).
        {
            const float KnobR = S * 0.105f;
            FSlateBrush KnobBrush = FSlateRoundedBoxBrush(Color, KnobR);
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
                AllottedGeometry.ToPaintGeometry(FVector2D(KnobR * 2.f, KnobR * 2.f),
                    FSlateLayoutTransform(Knob - FVector2D(KnobR, KnobR))),
                &KnobBrush, ESlateDrawEffect::None, FLinearColor::White);
        }

        return LayerId + 5;
    }

private:
    float X = 0.f;
    float Y = 0.f;
    FLinearColor Color = FLinearColor(0.561f, 0.714f, 0.812f, 1.0f);
    FLinearColor HousingColor = FLinearColor(0.03f, 0.045f, 0.06f, 0.9f);
    FLinearColor OutlineColor = FLinearColor(0.36f, 0.44f, 0.48f, 0.55f);
    FLinearColor CrosshairColor = FLinearColor(0.20f, 0.26f, 0.30f, 0.9f);
    float StickSize = 160.f;
};

UCLASS()
class BORN2FLAP_API UBorn2FlapStick : public UWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stick")
    float X = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stick")
    float Y = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stick")
    FLinearColor Color = FLinearColor(0.561f, 0.714f, 0.812f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stick")
    float StickSize = 160.f;

    void SetX(float InX)
    {
        X = InX;
        if (MyStick.IsValid()) MyStick->SetX(InX);
    }

    void SetY(float InY)
    {
        Y = InY;
        if (MyStick.IsValid()) MyStick->SetY(InY);
    }

    void SetColor(const FLinearColor& InColor)
    {
        Color = InColor;
        if (MyStick.IsValid()) MyStick->SetColor(InColor);
    }

    void SetStickSize(float InSize)
    {
        StickSize = InSize;
        if (MyStick.IsValid()) MyStick->SetStickSize(InSize);
    }

    virtual void SynchronizeProperties() override
    {
        Super::SynchronizeProperties();
        if (MyStick.IsValid())
        {
            MyStick->SetX(X);
            MyStick->SetY(Y);
            MyStick->SetColor(Color);
            MyStick->SetStickSize(StickSize);
        }
    }

    virtual void ReleaseSlateResources(bool bReleaseChildren) override
    {
        Super::ReleaseSlateResources(bReleaseChildren);
        MyStick.Reset();
    }

#if WITH_EDITOR
    virtual const FText GetPaletteCategory() override
    {
        return NSLOCTEXT("Born2Flap", "Palette", "Born2Flap");
    }
#endif

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override
    {
        MyStick = SNew(SBorn2FlapStick)
            .X(X)
            .Y(Y)
            .Color(Color)
            .StickSize(StickSize);
        return MyStick.ToSharedRef();
    }

    TSharedPtr<SBorn2FlapStick> MyStick;
};