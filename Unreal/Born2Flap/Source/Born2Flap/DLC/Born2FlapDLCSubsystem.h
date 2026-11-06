#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Born2FlapDLCSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDLCDownloadComplete, FName, WorldName, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDLCManifestLoaded, bool, bSuccess);

/**
 * Downloads and mounts per-world DLC at runtime, driven by a version manifest.
 *
 * Each world is cooked into its own pak (chunk 1..4) and released individually
 * (e.g. as a GitHub release asset) as a classic .pak file carrying the world's
 * whole /Game/<World> directory, including its /Audio radio soundtrack. The
 * base game ships chunk 0 (Shiomori + shared content).
 *
 * The authoritative catalogue is Config/DLCManifest.json (served over HTTPS,
 * URL configured by ManifestURL in DefaultDLC.ini). Each entry maps a world to
 * its download URL, release version, byte size and sha1 checksum. The game
 * fetches the manifest once, then only re-downloads a world when its local
 * copy is missing or its version differs from the manifest. The world-select UI
 * calls RequestWorldDownload() and waits for OnDownloadComplete before travel.
 *
 * Chunk IDs must match Tools/create_dlc_chunks.py, the $dlcChunks table in
 * Tools/build-android.ps1, and the chunk_id field in DLCManifest.json.
 */
UCLASS()
class BORN2FLAP_API UBorn2FlapDLCSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** True if the world is in the base game, or its DLC pak is already downloaded (and version-current once the manifest is loaded). */
    UFUNCTION(BlueprintPure, Category = "DLC")
    bool IsWorldAvailable(FName WorldName) const;

    /** True if a download for this world is currently in flight. */
    UFUNCTION(BlueprintPure, Category = "DLC")
    bool IsWorldDownloading(FName WorldName) const;

    /** True once the release manifest has been fetched and parsed. */
    UFUNCTION(BlueprintPure, Category = "DLC")
    bool IsManifestLoaded() const { return bManifestLoaded; }

    /** Release version string for a world from the manifest (empty if unknown/unloaded). */
    UFUNCTION(BlueprintPure, Category = "DLC")
    FString GetWorldVersion(FName WorldName) const;

    /** Begin downloading (and mounting) the world's DLC pak. Broadcasts OnDownloadComplete. */
    UFUNCTION(BlueprintCallable, Category = "DLC")
    void RequestWorldDownload(FName WorldName);

    /** Fired on the game thread when a RequestWorldDownload finishes. */
    UPROPERTY(BlueprintAssignable, Category = "DLC")
    FOnDLCDownloadComplete OnDownloadComplete;

    /** Fired on the game thread after the release manifest fetch resolves (success or failure). */
    UPROPERTY(BlueprintAssignable, Category = "DLC")
    FOnDLCManifestLoaded OnManifestLoaded;

private:
    struct FWorldChunk { const TCHAR* World; int32 ChunkId; };

    /** One entry of DLCManifest.json. */
    struct FWorldManifestEntry
    {
        FString Name;
        int32 ChunkId = 0;
        FString WorldVersion;
        FString DownloadURL;
        int64 SizeBytes = 0;
        FString Sha1; // lowercase hex; empty = skip integrity check
    };

    static const FWorldChunk WorldChunks[];

    void MountAllDownloadedChunks();
    int32 FindChunkId(FName WorldName) const;
    FString GetDLCFileName(FName WorldName) const;
    FString GetDLCVersionFile(FName WorldName) const;
    FString GetManifestURL() const;
    FString GetBaseURL() const;

    void FetchManifest();
    void OnManifestFetched(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
    void ParseManifest(const FString& JsonString);

    void StartDownload(FName WorldName);
    void OnDownloadFinished(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FName WorldName);

    bool IsVersionCurrent(FName WorldName, const FWorldManifestEntry& Entry) const;
    void WriteVersionFile(FName WorldName, const FString& Version) const;
    bool VerifyDownload(const TArray<uint8>& Data, const FWorldManifestEntry* Entry, FString& OutError) const;

    TSet<FName> Downloading;
    TArray<FName> PendingRequests;
    TMap<FName, FWorldManifestEntry> Manifest;
    bool bManifestLoaded = false;
    bool bManifestLoading = false;
};
