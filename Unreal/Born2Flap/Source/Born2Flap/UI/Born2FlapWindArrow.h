// Born2FlapWindArrow.h — a tiny self-drawn HUD arrow (UMG custom widget).
//
// Replaces the cockpit's old ASCII wind glyphs (the ChakraPetch font cannot
// render U+2196–U+2199) with a small vector arrow painted directly in Slate.
// The arrow rotates in screen space: 0° = straight up (ahead of the pilot),
// positive = clockwise — so the pilot reads the *relative* wind source
// direction at a glance, exactly like a compass needle. No texture asset, no
// world-space actor: the wind indicator lives in the cockpit panel.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Born2FlapWindArrow.generated.h"

class SBorn2FlapWindArrow : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBorn2FlapWindArrow) {}
        SLATE_ARGUMENT(float, Angle)
        SLATE_ARGUMENT(FLinearColor, Color)
        SLATE_ARGUMENT(float, ArrowSize)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Angle = InArgs._Angle;
        Color = InArgs._Color;
        ArrowSize = InArgs._ArrowSize;
    }

    void SetAngle(float InAngle)
    {
        if (FMath::IsNearlyEqual(Angle, InAngle)) return;
        Angle = InAngle;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetColor(const FLinearColor& InColor)
    {
        if (Color == InColor) return;
        Color = InColor;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetArrowSize(float InSize)
    {
        if (FMath::IsNearlyEqual(ArrowSize, InSize)) return;
        ArrowSize = InSize;
        Invalidate(EInvalidateWidgetReason::Layout);
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(ArrowSize, ArrowSize);
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        const FVector2D C = Size * 0.5f;
        const float S = FMath::Max(1.f, FMath::Min(Size.X, Size.Y));
        const float Half = S * 0.5f * 0.78f;   // shaft half-length

        // 0° = up (screen), positive = clockwise (screen Y grows downward).
        const float Rad = FMath::DegreesToRadians(Angle);
        const FVector2D Dir(FMath::Sin(Rad), -FMath::Cos(Rad));
        const FVector2D Perp(-Dir.Y, Dir.X);

        const FVector2D Tip  = C + Dir * Half;
        const FVector2D Tail = C - Dir * Half;
        const float Thick = FMath::Max(2.f, S * 0.10f);
        const float HeadLen = S * 0.34f;
        const float HeadWing = S * 0.20f;

        // Shaft.
        TArray<FVector2D> Shaft;
        Shaft.Add(Tail);
        Shaft.Add(Tip);
        FSlateDrawElement::MakeLines(OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(), Shaft,
            ESlateDrawEffect::None, Color, true, Thick);

        // Head chevron.
        const FVector2D HeadBase = Tip - Dir * HeadLen;
        TArray<FVector2D> Head;
        Head.Add(HeadBase + Perp * HeadWing);
        Head.Add(Tip);
        Head.Add(HeadBase - Perp * HeadWing);
        FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1,
            AllottedGeometry.ToPaintGeometry(), Head,
            ESlateDrawEffect::None, Color, true, Thick);

        return LayerId + 1;
    }

private:
    float Angle = 0.f;
    FLinearColor Color = FLinearColor::White;
    float ArrowSize = 28.f;
};

UCLASS()
class BORN2FLAP_API UBorn2FlapWindArrow : public UWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
    float Angle = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
    FLinearColor Color = FLinearColor(0.561f, 0.714f, 0.812f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
    float ArrowSize = 28.f;

    void SetAngle(float InAngle)
    {
        Angle = InAngle;
        if (MyArrow.IsValid()) MyArrow->SetAngle(InAngle);
    }

    void SetColor(const FLinearColor& InColor)
    {
        Color = InColor;
        if (MyArrow.IsValid()) MyArrow->SetColor(InColor);
    }

    void SetArrowSize(float InSize)
    {
        ArrowSize = InSize;
        if (MyArrow.IsValid()) MyArrow->SetArrowSize(InSize);
    }

    virtual void SynchronizeProperties() override
    {
        Super::SynchronizeProperties();
        if (MyArrow.IsValid())
        {
            MyArrow->SetAngle(Angle);
            MyArrow->SetColor(Color);
            MyArrow->SetArrowSize(ArrowSize);
        }
    }

    virtual void ReleaseSlateResources(bool bReleaseChildren) override
    {
        Super::ReleaseSlateResources(bReleaseChildren);
        MyArrow.Reset();
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
        MyArrow = SNew(SBorn2FlapWindArrow)
            .Angle(Angle)
            .Color(Color)
            .ArrowSize(ArrowSize);
        return MyArrow.ToSharedRef();
    }

    TSharedPtr<SBorn2FlapWindArrow> MyArrow;
};
