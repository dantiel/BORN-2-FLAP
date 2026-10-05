// Born2FlapPoi.h — points of interest per level.
//
// A POI is a named reset/launch point. Each level (Ravenstonefield, Shiomori,
// Training) contributes its own list: a launch point plus named landmarks. The
// level-selection menu lists them (Born2FlapPoi::Catalog) and passes the chosen
// destination as ?PoiKey= / ?PoiNum=, so the bird spawns there as its reset
// start point. POIs are no longer visualised or cycled inside the world.
//
// Key is a semantic i18n key ("poi.raven_watch"); Position is the absolute
// world spawn location in centimetres (already at ground + clearance, so the
// existing hand-launch mechanic works unchanged).

#pragma once

#include "CoreMinimal.h"

struct FBorn2FlapPoi
{
    FString Key;      // semantic i18n key, e.g. "poi.raven_watch"
    FVector Position; // absolute spawn location, cm (ground + clearance baked in)
    float Yaw = 0.f;  // spawn heading, degrees
    int32 Number = 0; // optional 1-based suffix ("GATE 3"), 0 = none
};

namespace Born2FlapPoi
{
    // Ordered travel destinations per level, used by the level-selection menu to
    // list a world's points of interest without loading its map. Positions are
    // resolved at runtime by ABorn2FlapGameMode::PopulatePOIs (which keeps the
    // same keys/numbers); this catalog carries only the identity (i18n key plus
    // an optional 1-based suffix for gates).
    inline TArray<FBorn2FlapPoi> Catalog(const FString& Level)
    {
        TArray<FBorn2FlapPoi> Out;
        if (Level.Equals(TEXT("Shiomori"), ESearchCase::IgnoreCase))
        {
            for (const TCHAR* K : { TEXT("poi.shiomori_beach"), TEXT("poi.bay_overlook"),
                                    TEXT("poi.tidewalk"), TEXT("poi.basalt_cove"), TEXT("poi.shoreline"),
                                    TEXT("poi.east_vegetation"), TEXT("poi.west_vegetation") })
            {
                FBorn2FlapPoi P; P.Key = K; Out.Add(P);
            }
        }
        else if (Level.Equals(TEXT("Training"), ESearchCase::IgnoreCase))
        {
            { FBorn2FlapPoi P; P.Key = TEXT("poi.start_line"); Out.Add(P); }
            for (int32 I = 1; I <= 6; ++I)
            {
                FBorn2FlapPoi P; P.Key = TEXT("poi.gate"); P.Number = I; Out.Add(P);
            }
        }
        else // Ravenstonefield
        {
            for (const TCHAR* K : { TEXT("poi.launch_meadow"), TEXT("poi.raven_watch"),
                                    TEXT("poi.crows_acre"), TEXT("poi.last_scrap"), TEXT("poi.underpass"),
                                    TEXT("poi.old_barns") })
            {
                FBorn2FlapPoi P; P.Key = K; Out.Add(P);
            }
        }
        return Out;
    }
}