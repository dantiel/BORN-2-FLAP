#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Born2FlapSplash.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class UTexture2D;

// Full-screen startup splash: the born2flap-splash artwork plus a loading bar
// and a live percentage label. Built entirely in code (no Blueprint), so it
// survives a fresh checkout with zero extra assets beyond the imported texture.
UCLASS()
class BORN2FLAP_API UBorn2FlapSplash : public UUserWidget
{
    GENERATED_BODY()
public:
    UBorn2FlapSplash(const FObjectInitializer& ObjectInitializer);

    // Drive the loading bar 0..1 and refresh the percentage label.
    void SetProgress(float Fraction);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY(Transient) TObjectPtr<UImage> BackgroundImage;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> LoadingBar;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY() TObjectPtr<UTexture2D> SplashTexture;
};
