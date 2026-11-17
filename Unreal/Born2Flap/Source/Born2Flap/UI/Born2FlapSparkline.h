// Born2FlapSparkline.h — a tiny self-drawn line chart (UMG custom widget).
//
// Draws a polyline through a list of scalar samples, evenly spaced along the
// X axis and auto-normalised to the widget height. Used by the flight desk's
// SERVO tab to show a servo's voltage-response curve (no-load speed vs pack
// voltage) without pulling in a charting dependency. No texture, no world
// actor: it lives inside the semantic panel and is driven by telemetry.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Brushes/SlateColorBrush.h"
#include "Born2FlapSparkline.generated.h"

class SBorn2FlapSparkline : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBorn2FlapSparkline) {}
        SLATE_ARGUMENT(TArray<float>, Values)
        SLATE_ARGUMENT(FLinearColor, Color)
        SLATE_ARGUMENT(FVector2D, Size)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Values = InArgs._Values;
        Color = InArgs._Color;
        Size = InArgs._Size;
    }

    void SetValues(const TArray<float>& InValues)
    {
        Values = InValues;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetColor(const FLinearColor& InColor)
    {
        Color = InColor;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void SetSize(const FVector2D& InSize)
    {
        Size = InSize;
        Invalidate(EInvalidateWidgetReason::Layout);
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return Size;
    }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
    {
        const FVector2D G = AllottedGeometry.GetLocalSize();
        if (G.X < 2.f || G.Y < 2.f)
            return LayerId;

        // Background panel (translucent dark).
        {
            FSlateColorBrush Bg(FLinearColor(0.01f, 0.03f, 0.04f, 0.55f));
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
                AllottedGeometry.ToPaintGeometry(), &Bg,
                ESlateDrawEffect::None, FLinearColor::White);
        }

        if (Values.Num() >= 2)
        {
            float Min = TNumericLimits<float>::Max();
            float Max = -TNumericLimits<float>::Max();
            for (const float V : Values) { Min = FMath::Min(Min, V); Max = FMath::Max(Max, V); }
            const float Span = FMath::Max(1e-6f, Max - Min);

            auto ToPoint = [&](int32 I) -> FVector2D
            {
                const float X = G.X * (Values.Num() == 1 ? 0.5f : float(I) / float(Values.Num() - 1));
                const float Y = G.Y * (1.f - (Values[I] - Min) / Span);
                return FVector2D(X, FMath::Clamp(Y, 0.f, G.Y));
            };

            // Filled area under the curve (subtle).
            {
                TArray<FSlateVertex> Verts;
                TArray<SlateIndex> Idx;
                const float BaseY = G.Y;
                const FSlateRenderTransform RT = AllottedGeometry.ToPaintGeometry().GetAccumulatedRenderTransform();
                for (int32 I = 0; I < Values.Num(); ++I)
                {
                    const FVector2D P = ToPoint(I);
                    Verts.Add(FSlateVertex::Make(
                        RT, FVector2f(P), FVector2f(1.0f, 1.0f), 0.0f, FVector4f(0.0f, 0.0f, 0.0f, 0.0f),
                        Color.CopyWithNewOpacity(0.22f).ToFColor(true)));
                }
                Verts.Add(FSlateVertex::Make(
                    RT, FVector2f(G.X, BaseY), FVector2f(1.0f, 1.0f), 0.0f, FVector4f(0.0f, 0.0f, 0.0f, 0.0f),
                    Color.CopyWithNewOpacity(0.0f).ToFColor(true)));
                Verts.Add(FSlateVertex::Make(
                    RT, FVector2f(0, BaseY), FVector2f(1.0f, 1.0f), 0.0f, FVector4f(0.0f, 0.0f, 0.0f, 0.0f),
                    Color.CopyWithNewOpacity(0.0f).ToFColor(true)));
                for (int32 I = 0; I < Values.Num() - 1; ++I)
                {
                    Idx.Add(I); Idx.Add(I + 1); Idx.Add(Values.Num());
                }
                Idx.Add(Values.Num()); Idx.Add(Values.Num() + 1);
                Idx.Add(Values.Num());
                FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId + 1,
                    FSlateResourceHandle(), Verts, Idx, nullptr, 0, 0,
                    ESlateDrawEffect::None);
            }

            // Polyline.
            {
                TArray<FVector2D> Line;
                for (int32 I = 0; I < Values.Num(); ++I)
                    Line.Add(ToPoint(I));
                FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2,
                    AllottedGeometry.ToPaintGeometry(), Line,
                    ESlateDrawEffect::None, Color, true, 2.f);
            }

            // Endpoint dots (first = min operating point, last = max).
            {
                const float R = 3.f;
                const FVector2D P0 = ToPoint(0), P1 = ToPoint(Values.Num() - 1);
                FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
                    AllottedGeometry.ToPaintGeometry(FVector2D(R * 2.f, R * 2.f),
                        FSlateLayoutTransform(P0 - FVector2D(R, R))),
                    nullptr, ESlateDrawEffect::None, Color);
                FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
                    AllottedGeometry.ToPaintGeometry(FVector2D(R * 2.f, R * 2.f),
                        FSlateLayoutTransform(P1 - FVector2D(R, R))),
                    nullptr, ESlateDrawEffect::None, Color);
            }
        }

        return LayerId + 4;
    }

private:
    TArray<float> Values;
    FLinearColor Color = FLinearColor::White;
    FVector2D Size = FVector2D(320.f, 72.f);
};

UCLASS()
class BORN2FLAP_API UBorn2FlapSparkline : public UWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sparkline")
    TArray<float> Values;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sparkline")
    FLinearColor Color = FLinearColor(0.561f, 0.714f, 0.812f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sparkline")
    FVector2D Size = FVector2D(320.f, 72.f);

    void SetValues(const TArray<float>& InValues)
    {
        Values = InValues;
        if (MySpark.IsValid()) MySpark->SetValues(InValues);
    }

    void SetColor(const FLinearColor& InColor)
    {
        Color = InColor;
        if (MySpark.IsValid()) MySpark->SetColor(InColor);
    }

    void SetSize(const FVector2D& InSize)
    {
        Size = InSize;
        if (MySpark.IsValid()) MySpark->SetSize(InSize);
    }

    virtual void SynchronizeProperties() override
    {
        Super::SynchronizeProperties();
        if (MySpark.IsValid())
        {
            MySpark->SetValues(Values);
            MySpark->SetColor(Color);
            MySpark->SetSize(Size);
        }
    }

    virtual void ReleaseSlateResources(bool bReleaseChildren) override
    {
        Super::ReleaseSlateResources(bReleaseChildren);
        MySpark.Reset();
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
        MySpark = SNew(SBorn2FlapSparkline)
            .Values(Values)
            .Color(Color)
            .Size(Size);
        return MySpark.ToSharedRef();
    }

    TSharedPtr<SBorn2FlapSparkline> MySpark;
};