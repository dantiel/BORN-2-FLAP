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

// ---- typography scale ------------------------------------------------------
FORCEINLINE float SizePx(const FString& Size, float Default)
{
    if (Size == TEXT("xs"))  return 12.f;
    if (Size == TEXT("s"))   return 14.f;
    if (Size == TEXT("m"))   return 17.f;
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
    // Website UI face (Chakra Petch, matching web/assets/css/style.css). Falls
    // back to the engine default until the import step has produced the faces.
    // Hold strong refs for the program lifetime: LoadObject returns a
    // non-rooted pointer, and FSlateFontInfo copies it by value — a raw
    // static would go stale after GC (same disease as the Slate-brush crash).
    static TStrongObjectPtr<UFontFace> Regular = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/ChakraPetchRegular.ChakraPetchRegular")));
    static TStrongObjectPtr<UFontFace> Bold = TStrongObjectPtr<UFontFace>(
        LoadObject<UFontFace>(nullptr, TEXT("/Game/UI/ChakraPetchBold.ChakraPetchBold")));
    const UFontFace* Face = bBold ? Bold.Get() : Regular.Get();
    if (Face)
    {
        // A UFontFace implements IFontFaceInterface, NOT IFontProviderInterface,
        // so it cannot serve as FSlateFontInfo::FontObject — Slate's
        // GetCompositeFont() would fall through to the LastResort font and render
        // tofu. Wrap the face in a FCompositeFont whose default typeface carries
        // the face asset via FFontData instead.
        FSlateFontInfo Info;
        TSharedPtr<FCompositeFont> CompFont = MakeShared<FCompositeFont>();
        FTypefaceEntry& Entry = CompFont->DefaultTypeface.Fonts.AddDefaulted_GetRef();
        Entry.Name = NAME_None;
        Entry.Font = FFontData(Face);
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
FORCEINLINE FVector4 Radius(float R = 8.f) { return FVector4(R, R, R, R); }

}  // namespace born2flap::ui::theme