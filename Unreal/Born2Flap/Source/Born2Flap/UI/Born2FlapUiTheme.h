#pragma once
// Born2FlapUiTheme.h — the semantic design system: design tokens + resolvers.
//
// The Ruby Brain speaks *semantically* (tone: :danger, size: :xl, spacing: 8,
// align: :center). This header turns those tokens into concrete slate values,
// so the Brain never has to think in pixels, colors or fonts. Styling lives
// here — the Brain stays behaviour.

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"

namespace born2flap::ui::theme {

// ---- palette (dark glassmorphism, tuned for the night forest/valley) -------
FORCEINLINE FLinearColor BG()       { return FLinearColor(0.035f, 0.045f, 0.065f, 0.66f); }
FORCEINLINE FLinearColor BG_SOLID() { return FLinearColor(0.060f, 0.070f, 0.100f, 0.94f); }
FORCEINLINE FLinearColor FG()       { return FLinearColor(0.920f, 0.940f, 0.970f, 1.0f); }
FORCEINLINE FLinearColor FG_DIM()   { return FLinearColor(0.600f, 0.640f, 0.700f, 1.0f); }
FORCEINLINE FLinearColor ACCENT()   { return FLinearColor(0.980f, 0.720f, 0.280f, 1.0f); }
FORCEINLINE FLinearColor GOOD()     { return FLinearColor(0.360f, 0.820f, 0.480f, 1.0f); }
FORCEINLINE FLinearColor WARN()     { return FLinearColor(0.980f, 0.640f, 0.240f, 1.0f); }
FORCEINLINE FLinearColor DANGER()   { return FLinearColor(0.900f, 0.300f, 0.300f, 1.0f); }
FORCEINLINE FLinearColor INFO()     { return FLinearColor(0.350f, 0.620f, 0.950f, 1.0f); }

// tone token → foreground color (falls back to `Default` on unknown/empty).
FORCEINLINE FLinearColor Tone(const FString& Tone, FLinearColor Default)
{
    if (Tone.IsEmpty() || Tone == TEXT("normal")) return Default;
    if (Tone == TEXT("dim"))    return FG_DIM();
    if (Tone == TEXT("accent")) return ACCENT();
    if (Tone == TEXT("good"))   return GOOD();
    if (Tone == TEXT("warn"))   return WARN();
    if (Tone == TEXT("danger")) return DANGER();
    if (Tone == TEXT("info"))   return INFO();
    return Default;
}

// The same tone, but with its alpha scaled (for tinted backgrounds/fills).
FORCEINLINE FLinearColor ToneAt(const FString& ToneName, FLinearColor Default, float Alpha)
{
    FLinearColor C = Tone(ToneName, Default);
    C.A = Alpha;
    return C;
}

// ---- typography scale ------------------------------------------------------
FORCEINLINE float SizePx(const FString& Size, float Default)
{
    if (Size == TEXT("xs"))  return 10.f;
    if (Size == TEXT("s"))   return 12.f;
    if (Size == TEXT("m"))   return 15.f;
    if (Size == TEXT("l"))   return 20.f;
    if (Size == TEXT("xl"))  return 28.f;
    if (Size == TEXT("xxl")) return 40.f;
    return Default;
}

// ---- spacing scale (for gap/padding tokens) --------------------------------
FORCEINLINE float SpacePx(const FString& Size, float Default)
{
    if (Size == TEXT("xs")) return 4.f;
    if (Size == TEXT("s"))  return 6.f;
    if (Size == TEXT("m"))  return 8.f;
    if (Size == TEXT("l"))  return 12.f;
    if (Size == TEXT("xl")) return 18.f;
    return Default;
}

// ---- fonts -----------------------------------------------------------------
FORCEINLINE FSlateFontInfo FontPx(float Px, bool bBold = false)
{
    return FCoreStyle::GetDefaultFontStyle(bBold ? FName("Bold") : FName("Regular"), Px);
}

// size token → font (DefaultPx is used when the token is unknown/empty).
FORCEINLINE FSlateFontInfo Font(const FString& Size, float DefaultPx, bool bBold = false)
{
    return FontPx(SizePx(Size, DefaultPx), bBold);
}

// corner radius for rounded-box brushes (uniform).
FORCEINLINE FVector4 Radius(float R = 8.f) { return FVector4(R, R, R, R); }

}  // namespace born2flap::ui::theme