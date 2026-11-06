#include "DLC/Born2FlapDLCSubsystem.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IPlatformFilePak.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"

// Chunk-id <-> world mapping. Must match Tools/create_dlc_chunks.py and the
// $dlcChunks table in Tools/build-android.ps1. Shiomori is chunk 0 (base), so
// it is intentionally absent here — it is never downloaded.
const UBorn2FlapDLCSubsystem::FWorldChunk UBorn2FlapDLCSubsystem::WorldChunks[] = {
    { TEXT("Murex"), 1 },
    { TEXT("NeonAbyss"), 2 },
    { TEXT("Ravenstonefield"), 3 },
    { TEXT("Yunsha"), 4 },
};

void UBorn2FlapDLCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MountAllDownloadedChunks();
}

void UBorn2FlapDLCSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

bool UBorn2FlapDLCSubsystem::IsWorldAvailable(FName WorldName) const
{
    if (FindChunkId(WorldName) == 0)
    {
        return true; // base game world
    }
    if (!FPaths::FileExists(GetDLCFileName(WorldName)))
    {
        return false;
    }
    // Have the file. If the manifest is loaded, require the version to match;
    // otherwise the local copy is treated as authoritative.
    if (bManifestLoaded)
    {
        if (const FWorldManifestEntry* Entry = Manifest.Find(WorldName))
        {
            return IsVersionCurrent(WorldName, *Entry);
        }
    }
    return true;
}

bool UBorn2FlapDLCSubsystem::IsWorldDownloading(FName WorldName) const
{
    return Downloading.Contains(WorldName);
}

FString UBorn2FlapDLCSubsystem::GetWorldVersion(FName WorldName) const
{
    if (const FWorldManifestEntry* Entry = Manifest.Find(WorldName))
    {
        return Entry->WorldVersion;
    }
    return FString();
}

void UBorn2FlapDLCSubsystem::RequestWorldDownload(FName WorldName)
{
    if (FindChunkId(WorldName) == 0)
    {
        OnDownloadComplete.Broadcast(WorldName, true);
        return;
    }
    if (Downloading.Contains(WorldName))
    {
        return;
    }
    Downloading.Add(WorldName);

    if (!bManifestLoaded)
    {
        PendingRequests.AddUnique(WorldName);
        FetchManifest();
        return;
    }

    StartDownload(WorldName);
}

void UBorn2FlapDLCSubsystem::FetchManifest()
{
    if (bManifestLoading)
    {
        return;
    }
    bManifestLoading = true;

    const FString URL = GetManifestURL();
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(30.0f);
    Request->OnProcessRequestComplete().BindUObject(this, &UBorn2FlapDLCSubsystem::OnManifestFetched);
    Request->ProcessRequest();

    UE_LOG(LogTemp, Log, TEXT("DLC: fetching manifest from %s"), *URL);
}

void UBorn2FlapDLCSubsystem::OnManifestFetched(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
    bManifestLoading = false;

    const bool bOk = bSucceeded && Response.IsValid() && Response->GetResponseCode() == 200;
    if (bOk)
    {
        ParseManifest(Response->GetContentAsString());
        bManifestLoaded = true;
        UE_LOG(LogTemp, Log, TEXT("DLC: manifest loaded (%d worlds)"), Manifest.Num());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("DLC: manifest fetch failed (code %d, succeeded=%d)"),
            Response.IsValid() ? Response->GetResponseCode() : -1,
            bSucceeded ? 1 : 0);
    }

    OnManifestLoaded.Broadcast(bOk);

    // Resume any requests that were queued while the manifest was loading.
    TArray<FName> Pending = PendingRequests;
    PendingRequests.Reset();
    for (const FName WorldName : Pending)
    {
        if (!bOk)
        {
            Downloading.Remove(WorldName);
            OnDownloadComplete.Broadcast(WorldName, false);
        }
        else
        {
            StartDownload(WorldName);
        }
    }
}

void UBorn2FlapDLCSubsystem::ParseManifest(const FString& JsonString)
{
    // Tolerate a UTF-8 BOM from the host.
    FString Clean = JsonString;
    if (Clean.StartsWith(TEXT("\xFEFF")))
    {
        Clean.RightChopInline(1);
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Clean);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("DLC: failed to parse manifest JSON"));
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Worlds = nullptr;
    if (!Root->TryGetArrayField(TEXT("worlds"), Worlds))
    {
        UE_LOG(LogTemp, Error, TEXT("DLC: manifest has no 'worlds' array"));
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Worlds)
    {
        const TSharedPtr<FJsonObject>* Obj = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj->IsValid())
        {
            continue;
        }

        FWorldManifestEntry Entry;
        (*Obj)->TryGetStringField(TEXT("name"), Entry.Name);
        (*Obj)->TryGetNumberField(TEXT("chunk_id"), Entry.ChunkId);
        (*Obj)->TryGetStringField(TEXT("world_version"), Entry.WorldVersion);
        (*Obj)->TryGetStringField(TEXT("download_url"), Entry.DownloadURL);
        (*Obj)->TryGetNumberField(TEXT("size_bytes"), Entry.SizeBytes);
        (*Obj)->TryGetStringField(TEXT("sha1"), Entry.Sha1);

        if (Entry.Name.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("DLC: skipping manifest entry with no 'name'"));
            continue;
        }
        Manifest.Add(FName(*Entry.Name), Entry);
    }
}

void UBorn2FlapDLCSubsystem::StartDownload(FName WorldName)
{
    const FWorldManifestEntry* Entry = Manifest.Find(WorldName);
    if (!Entry)
    {
        UE_LOG(LogTemp, Error, TEXT("DLC: world '%s' is not listed in the manifest"), *WorldName.ToString());
        Downloading.Remove(WorldName);
        OnDownloadComplete.Broadcast(WorldName, false);
        return;
    }

    if (IsVersionCurrent(WorldName, *Entry))
    {
        // Already have the right version on disk; just ensure it is mounted.
        FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
            FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
        const FString PakPath = GetDLCFileName(WorldName);
        const bool bMounted = PakPlatformFile && PakPlatformFile->Mount(*PakPath, 0);
        Downloading.Remove(WorldName);
        OnDownloadComplete.Broadcast(WorldName, bMounted);
        return;
    }

    FString URL = Entry->DownloadURL;
    if (URL.IsEmpty())
    {
        URL = GetBaseURL() + WorldName.ToString() + TEXT(".b2f-world");
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(URL);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1200.0f);
    // GitHub release assets 302-redirect to a signed storage URL. The curl
    // backend follows redirects by default (CURLOPT_FOLLOWLOCATION is always
    // set), so no explicit call is needed.
    Request->OnProcessRequestComplete().BindUObject(this, &UBorn2FlapDLCSubsystem::OnDownloadFinished, WorldName);
    Request->ProcessRequest();

    UE_LOG(LogTemp, Log, TEXT("DLC: downloading %s from %s"), *WorldName.ToString(), *URL);
}

void UBorn2FlapDLCSubsystem::OnDownloadFinished(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FName WorldName)
{
    bool bSuccess = false;
    const FWorldManifestEntry* Entry = Manifest.Find(WorldName);

    if (bSucceeded && Response.IsValid() && Response->GetResponseCode() == 200)
    {
        const TArray<uint8>& Data = Response->GetContent();
        FString VerifyError;
        if (Data.Num() > 0 && VerifyDownload(Data, Entry, VerifyError))
        {
            const FString PakPath = GetDLCFileName(WorldName);
            if (FFileHelper::SaveArrayToFile(Data, *PakPath))
            {
                FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
                    FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
                if (PakPlatformFile && PakPlatformFile->Mount(*PakPath, 0))
                {
                    if (Entry)
                    {
                        WriteVersionFile(WorldName, Entry->WorldVersion);
                    }
                    bSuccess = true;
                    UE_LOG(LogTemp, Log, TEXT("DLC: mounted %s"), *PakPath);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("DLC: failed to mount %s"), *PakPath);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("DLC: failed to write %s"), *PakPath);
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("DLC: download verification failed for %s: %s"), *WorldName.ToString(), *VerifyError);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("DLC: download failed for %s (code %d, succeeded=%d)"),
            *WorldName.ToString(),
            Response.IsValid() ? Response->GetResponseCode() : -1,
            bSucceeded ? 1 : 0);
    }

    Downloading.Remove(WorldName);
    OnDownloadComplete.Broadcast(WorldName, bSuccess);
}

void UBorn2FlapDLCSubsystem::MountAllDownloadedChunks()
{
    FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
        FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
    if (!PakPlatformFile)
    {
        return;
    }

    for (const FWorldChunk& WC : WorldChunks)
    {
        const FString PakPath = GetDLCFileName(FName(WC.World));
        if (FPaths::FileExists(PakPath) && PakPlatformFile->Mount(*PakPath, 0))
        {
            UE_LOG(LogTemp, Log, TEXT("DLC: auto-mounted %s"), *PakPath);
        }
    }
}

int32 UBorn2FlapDLCSubsystem::FindChunkId(FName WorldName) const
{
    for (const FWorldChunk& WC : WorldChunks)
    {
        if (WorldName == FName(WC.World))
        {
            return WC.ChunkId;
        }
    }
    return 0; // unknown worlds are treated as base (no DLC)
}

FString UBorn2FlapDLCSubsystem::GetDLCFileName(FName WorldName) const
{
    return FPaths::ProjectSavedDir() / TEXT("DLC") / (WorldName.ToString() + TEXT(".b2f-world"));
}

FString UBorn2FlapDLCSubsystem::GetDLCVersionFile(FName WorldName) const
{
    return FPaths::ProjectSavedDir() / TEXT("DLC") / (WorldName.ToString() + TEXT(".b2f-world.version"));
}

FString UBorn2FlapDLCSubsystem::GetManifestURL() const
{
    FString URL;
    GConfig->GetString(TEXT("Born2FlapDLC"), TEXT("ManifestURL"), URL, GGameIni);
    if (URL.IsEmpty())
    {
        URL = TEXT("https://raw.githubusercontent.com/OWNER/born2flap-worlds/main/Config/DLCManifest.json");
    }
    return URL;
}

FString UBorn2FlapDLCSubsystem::GetBaseURL() const
{
    FString BaseURL;
    GConfig->GetString(TEXT("Born2FlapDLC"), TEXT("BaseURL"), BaseURL, GGameIni);
    if (BaseURL.IsEmpty())
    {
        BaseURL = TEXT("https://cdn.example.com/born2flap/dlc/");
    }
    if (!BaseURL.EndsWith(TEXT("/")))
    {
        BaseURL += TEXT("/");
    }
    return BaseURL;
}

bool UBorn2FlapDLCSubsystem::IsVersionCurrent(FName WorldName, const FWorldManifestEntry& Entry) const
{
    const FString PakPath = GetDLCFileName(WorldName);
    if (!FPaths::FileExists(PakPath))
    {
        return false;
    }
    if (Entry.WorldVersion.IsEmpty())
    {
        return true; // no version to compare against; the file is enough
    }

    FString LocalVersion;
    if (!FFileHelper::LoadFileToString(LocalVersion, *GetDLCVersionFile(WorldName)))
    {
        return false;
    }
    return LocalVersion.TrimStartAndEnd() == Entry.WorldVersion;
}

void UBorn2FlapDLCSubsystem::WriteVersionFile(FName WorldName, const FString& Version) const
{
    FFileHelper::SaveStringToFile(Version, *GetDLCVersionFile(WorldName));
}

bool UBorn2FlapDLCSubsystem::VerifyDownload(const TArray<uint8>& Data, const FWorldManifestEntry* Entry, FString& OutError) const
{
    if (!Entry)
    {
        return true; // no manifest metadata to check against
    }
    if (Entry->SizeBytes > 0 && Data.Num() != Entry->SizeBytes)
    {
        OutError = FString::Printf(TEXT("size mismatch (got %d, expected %lld)"), Data.Num(), (long long)Entry->SizeBytes);
        return false;
    }
    if (!Entry->Sha1.IsEmpty())
    {
        const FSHAHash Actual = FSHA1::HashBuffer(Data.GetData(), Data.Num());
        const FString ActualHex = Actual.ToString().ToLower();
        if (ActualHex != Entry->Sha1.ToLower())
        {
            OutError = FString::Printf(TEXT("sha1 mismatch (got %s, expected %s)"), *ActualHex, *Entry->Sha1);
            return false;
        }
    }
    return true;
}
