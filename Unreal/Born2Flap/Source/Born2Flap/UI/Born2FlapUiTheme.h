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
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

namespace born2flap::ui::theme {

// ---- palette — the website design tokens (web/assets/css/style.css :root).
// Dark scheme: #0a0b10 background, #e9e6dc text, #e0483a accent red,
// #8fb6cf cool blue, #c9a15a gold. Keeps the glassmorphism alpha on BG.
FORCEINLINE FLinearColor BG()       { return FLinearColor(0.039f, 0.043f, 0.063f, 0.72f); }
FORCEINLINE FLinearColor BG_SOLID() { return FLinearColor(0.063f, 0.075f, 0.106f, 0.96f); }
FORCEINLINE FLinearColor FG()       { return FLinearColor(0.914f, 0.902f, 0.863f, 1.0f); }
FORCEINLINE FLinearColor FG_DIM()   { return FLinearColor(0.663f, 0.651f, 0.608f, 1.0f); }
FORCEINLINE FLinearColor ACCENT()   { return FLinearColor(0.878f, 0.282f, 0.227f, 1.0f); }
FORCEINLINE FLinearColor GOOD()     { return FLinearColor(0.360f, 0.820f, 0.480f, 1.0f); }
FORCEINLINE FLinearColor WARN()     { return FLinearColor(0.788f, 0.631f, 0.353f, 1.0f); }
FORCEINLINE FLinearColor DANGER()   { return FLinearColor(0.761f, 0.216f, 0.173f, 1.0f); }
FORCEINLINE FLinearColor INFO()     { return FLinearColor(0.561f, 0.714f, 0.812f, 1.0f); }

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
    if (Tone == TEXT("ghost"))        return FG();
    if (Tone == TEXT("ghost-active")) return ACCENT();
    return Default;
}

// The same tone, but with its alpha scaled (for tinted backgrounds/fills).
FORCEINLINE FLinearColor ToneAt(const FString& ToneName, FLinearColor Default, float Alpha)
{
    FLinearColor C = Tone(ToneName, Default);
    C.A = Alpha;
    return C;
}

// ---- UI scale (the mobile "UI scale" setting) -------------------------------
// A single global multiplier applied to every design token: font size, spacing,
// element size and corner radius all move together. 1.0 = the design-token
// defaults. The settings menu writes it (persisted to FlightPreferences.ini);
// the GameMode re-applies it before any UI is built so the splash/menu/cockpit
// all share one scale. The inline function's function-local static is a single
// object per program (ODR), so every translation unit shares one scale.
namespace detail {
    FORCEINLINE float& UIScaleRef() { static float S = 1.0f; return S; }
}
FORCEINLINE float UIScale() { return detail::UIScaleRef(); }
FORCEINLINE void SetUIScale(float Scale) { detail::UIScaleRef() = FMath::Clamp(Scale, 0.5f, 2.0f); }
FORCEINLINE float Scale(float V) { return V * detail::UIScaleRef(); }

// ---- typography scale ------------------------------------------------------
FORCEINLINE float SizePx(const FString& Size, float Default)
{
    if (Size == TEXT("xs"))  return Scale(12.f);
    if (Size == TEXT("s"))   return Scale(14.f);
    if (Size == TEXT("m"))   return Scale(17.f);
    if (Size == TEXT("l"))   return Scale(20.f);
    if (Size == TEXT("xl"))  return Scale(28.f);
    if (Size == TEXT("xxl")) return Scale(40.f);
    return Scale(Default);
}

// ---- spacing scale (for gap/padding tokens) --------------------------------
FORCEINLINE float SpacePx(const FString& Size, float Default)
{
    if (Size == TEXT("xs")) return Scale(4.f);
    if (Size == TEXT("s"))  return Scale(6.f);
    if (Size == TEXT("m"))  return Scale(8.f);
    if (Size == TEXT("l"))  return Scale(12.f);
    if (Size == TEXT("xl")) return Scale(18.f);
    return Scale(Default);
}

// ---- fonts -----------------------------------------------------------------
FORCEINLINE FSlateFontInfo FontPx(float Px, bool bBold = false)
{
    // Website UI face (Chakra Petch, matching web/assets/css/style.css). Falls
    // back to the engine default until the import step has produced the faces.
    // Hold strong refs for the program lifetime: LoadObject returns a
    // non-rooted pointer, and FSlateFontInfo copies it by value — a raw
    // static would go stale after GC (same disease as the Slate-brush crash).
    static TStrongObjectPtr<UFontFace> Regular = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/ChakraPetchRegular.ChakraPetchRegular")));
    static TStrongObjectPtr<UFontFace> Bold = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/ChakraPetchBold.ChakraPetchBold")));

    // Fallback faces for the scripts Chakra Petch does not cover: Droid Sans
    // Fallback carries CJK (ja/ko/zh) + Cyrillic (ru) + Hangul/Kana, Roboto
    // covers Latin-ext / Greek / Cyrillic. Slate walks the typeface's ordered
    // font list until a glyph is found, so translated text renders instead of
    // tofu. Loaded once and held strongly for the program lifetime.
    static TStrongObjectPtr<UFontFace> FallbackCJK = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Engine/EngineFonts/Faces/DroidSansFallback.DroidSansFallback")));
    static TStrongObjectPtr<UFontFace> FallbackLatin = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Engine/EngineFonts/Faces/RobotoRegular.RobotoRegular")));
    static TStrongObjectPtr<UFontFace> FallbackLatinBold = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Engine/EngineFonts/Faces/RobotoBold.RobotoBold")));

    const UFontFace* Face = bBold ? Bold.Get() : Regular.Get();
    if (Face)
    {
        // A UFontFace implements IFontFaceInterface, NOT IFontProviderInterface,
        // so it cannot serve as FSlateFontInfo::FontObject — Slate's
        // GetCompositeFont() would fall through to the LastResort font and render
        // tofu. Wrap the face in a FCompositeFont whose default typeface carries
        // the face asset via FFontData instead. Append the fallback faces so
        // non-Latin scripts resolve through the same composite.
        TSharedPtr<FCompositeFont> CompFont = MakeShared<FCompositeFont>();

        auto AddFace = [&CompFont](const UFontFace* F)
        {
            if (!F) return;
            FTypefaceEntry& E = CompFont->DefaultTypeface.Fonts.AddDefaulted_GetRef();
            E.Name = NAME_None;
            E.Font = FFontData(F);
        };
        AddFace(Face);
        AddFace(FallbackCJK.Get());
        AddFace(bBold ? FallbackLatinBold.Get() : FallbackLatin.Get());

        FSlateFontInfo Info;
        Info.CompositeFont = CompFont;
        Info.Size = FMath::RoundToInt(Px);
        return Info;
    }
    return FCoreStyle::GetDefaultFontStyle(bBold ? FName("Bold") : FName("Regular"), FMath::RoundToInt(Px));
}

// size token → font (DefaultPx is used when the token is unknown/empty).
FORCEINLINE FSlateFontInfo Font(const FString& Size, float DefaultPx, bool bBold = false)
{
    return FontPx(SizePx(Size, DefaultPx), bBold);
}

// corner radius for rounded-box brushes (uniform).
FORCEINLINE FVector4 Radius(float R = 8.f) { const float Rs = Scale(R); return FVector4(Rs, Rs, Rs, Rs); }

}  // namespace born2flap::ui::theme