#pragma once
// Born2FlapLoading.h — thread-safe loading progress shared between the game
// thread (writer: GameMode BeginPlay, FlightPawn, the module's OnPostLoadMap)
// and the loading-screen widget (reader: SBorn2FlapLoadingPanel::OnPaint,
// which may run on the movie player's slate thread).
//
// Progress is monotonic across a single load: Report() never moves the bar
// backwards, so reporters with no strict ordering (GameMode vs FlightPawn)
// cannot make it flicker. -1 means indeterminate (marquee); 0..1 is a
// determinate fill.

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>

namespace Born2Flap::Loading
{
    struct FState
    {
        // -1 → indeterminate (marquee); 0..1000 → determinate fill (permille).
        std::atomic<int32> ProgressPermille{ -1 };
        std::atomic<bool> bActive{ false };

        mutable FCriticalSection TextLock;
        FString Text = TEXT("LOADING");

        // Start a fresh load: reset the bar to indeterminate and arm the loader.
        void Begin(const FString& InText = TEXT("LOADING"))
        {
            ProgressPermille.store(-1, std::memory_order_relaxed);
            { FScopeLock L(&TextLock); Text = InText; }
            bActive.store(true, std::memory_order_relaxed);
        }

        // Report determinate progress (0..1). Monotonic within a load.
        void Report(float Fraction, const FString& InText)
        {
            const int32 Target = FMath::Clamp(FMath::RoundToInt(Fraction * 1000.f), 0, 1000);
            int32 Cur = ProgressPermille.load(std::memory_order_relaxed);
            if (Cur < 0)
                Cur = 0;
            ProgressPermille.store(FMath::Max(Cur, Target), std::memory_order_relaxed);
            { FScopeLock L(&TextLock); Text = InText; }
        }

        // Switch back to an indeterminate marquee (e.g. a long async phase).
        void Indeterminate(const FString& InText)
        {
            ProgressPermille.store(-1, std::memory_order_relaxed);
            { FScopeLock L(&TextLock); Text = InText; }
        }

        void Finish()
        {
            ProgressPermille.store(1000, std::memory_order_relaxed);
            bActive.store(false, std::memory_order_relaxed);
        }

        bool IsActive() const { return bActive.load(std::memory_order_relaxed); }

        void Get(float& OutFraction, FString& OutText) const
        {
            const int32 P = ProgressPermille.load(std::memory_order_relaxed);
            OutFraction = (P < 0) ? -1.f : (float)P * 0.001f;
            { FScopeLock L(&TextLock); OutText = Text; }
        }
    };

    inline FState& State()
    {
        static FState S;
        return S;
    }

    // Free-function writers, so call sites read naturally
    // (Born2Flap::Loading::Report(...) rather than State().Report(...)).
    inline void Report(float Fraction, const FString& Text) { State().Report(Fraction, Text); }
    inline void Finish() { State().Finish(); }
}