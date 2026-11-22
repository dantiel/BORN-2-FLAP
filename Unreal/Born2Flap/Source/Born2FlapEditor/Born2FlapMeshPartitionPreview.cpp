#include "Born2FlapMeshPartitionPreview.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

#include "MeshPartition.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionStaticMeshComponent.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"

void UBorn2FlapMeshPartitionPreview::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FEditorDelegates::OnMapOpened.AddUObject(this, &UBorn2FlapMeshPartitionPreview::OnMapOpened);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &UBorn2FlapMeshPartitionPreview::OnWorldCleanup);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBorn2FlapMeshPartitionPreview::Tick));
}

void UBorn2FlapMeshPartitionPreview::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	FEditorDelegates::OnMapOpened.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);

	Super::Deinitialize();
}

void UBorn2FlapMeshPartitionPreview::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
	if (bAsTemplate || !GEditor)
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World && World->WorldType == EWorldType::Editor)
	{
		PendingWorld = World;
		SpawnedProxyCount = 0;
		PendingStartTime = 0.0;
		LastAttemptTime = 0.0;
		PendingAttempts = 0;
	}
}

void UBorn2FlapMeshPartitionPreview::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (PendingWorld.Get() == World)
	{
		PendingWorld = nullptr;
	}
}

bool UBorn2FlapMeshPartitionPreview::Tick(float DeltaTime)
{
	if (UWorld* World = PendingWorld.Get())
	{
		const double Now = FPlatformTime::Seconds();
		if (PendingStartTime <= 0.0)
		{
			PendingStartTime = Now;
			LastAttemptTime = 0.0;
		}
		// Throttle: attempt at most every MinAttemptIntervalSeconds so the
		// World Partition streaming and the asset registry can settle.
		if (Now - LastAttemptTime < MinAttemptIntervalSeconds && PendingAttempts > 0)
		{
			return true;
		}
		LastAttemptTime = Now;

		const int32 Spawned = ScanAndSpawnProxies(World, /*bRequireMeshPartitionActor=*/true);
		PendingAttempts++;
		if (Spawned > 0)
		{
			PendingWorld = nullptr;
			PendingStartTime = 0.0;
			PendingAttempts = 0;
		}
		else if (Now - PendingStartTime >= MaxPendingSeconds)
		{
			// The MeshPartition actor never streamed in or the packages were
			// not discoverable yet; retry once without the gate as a best effort.
			UE_LOG(LogTemp, Warning, TEXT("B2FMPPreview: no proxies after %d attempts over %.1fs; retrying without MeshPartition-actor gate"),
				PendingAttempts, Now - PendingStartTime);
			const int32 Ungated = ScanAndSpawnProxies(World, /*bRequireMeshPartitionActor=*/false);
			UE_LOG(LogTemp, Warning, TEXT("B2FMPPreview: ungated retry spawned %d proxy actor(s)"), Ungated);
			PendingWorld = nullptr;
			PendingStartTime = 0.0;
			PendingAttempts = 0;
		}
		else if (PendingAttempts % 5 == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("B2FMPPreview: waiting for MeshPartition actor / asset registry (attempt %d, %.1fs)"),
				PendingAttempts, Now - PendingStartTime);
		}
	}
	return true;
}

int32 UBorn2FlapMeshPartitionPreview::ForceScanForPreview(UWorld* World)
{
	if (!World)
	{
		return 0;
	}
	return ScanAndSpawnProxies(World, /*bRequireMeshPartitionActor=*/false);
}

int32 UBorn2FlapMeshPartitionPreview::ScanAndSpawnProxies(UWorld* World, bool bRequireMeshPartitionActor)
{
	// Only maps that actually carry a MeshPartition actor (the headless
	// diagnostic hook skips this gate because -nullrhi never streams it).
	bool bHasMeshPartition = false;
	{
		TActorIterator<UE::MeshPartition::AMeshPartition> It(World);
		bHasMeshPartition = static_cast<bool>(It);
	}
	if (bRequireMeshPartitionActor && !bHasMeshPartition)
	{
		return 0;
	}

	const UPackage* WorldPackage = World->GetPackage();
	const FString MapFolder = WorldPackage->GetName(); // e.g. /Game/Yunsha/Maps/YUNSHA
	const FString MapName = FPaths::GetBaseFilename(MapFolder); // e.g. YUNSHA

	// The compiled-section actor packages live either directly in the map
	// folder or (standard World Partition layout) under
	// /Game/__ExternalActors__/<MapName>/Maps/<Map>/<hash>/. Their object
	// paths carry 'CompiledSection'.
	const TArray<FString> FolderCandidates =
	{
		MapFolder,
		TEXT("/Game/__ExternalActors__/") + MapName
	};

	TArray<FString> PackageNames;
	{
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		for (const FString& Folder : FolderCandidates)
		{
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByPath(FName(*Folder), Assets, /*bRecursive=*/true, /*bIncludeOnlyOnDiskAssets=*/true);
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.GetObjectPathString().Contains(TEXT("CompiledSection")))
				{
					PackageNames.AddUnique(Asset.PackageName.ToString());
				}
			}
		}
	}

	// Fallback: disk scan of both candidate folders, filtered through the
	// asset registry per package (avoids loading unrelated actor packages).
	if (PackageNames.IsEmpty())
	{
		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		for (const FString& Folder : FolderCandidates)
		{
			const FString FolderOnDisk = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Folder));
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *FolderOnDisk, TEXT("*.uasset"), /*Files=*/true, /*Directories=*/false);
			for (const FString& File : Files)
			{
				FString PackageName = FPackageName::FilenameToLongPackageName(File);
				if (PackageName == MapFolder)
				{
					continue;
				}
				TArray<FAssetData> PackageAssets;
				AssetRegistry.GetAssetsByPackageName(FName(*PackageName), PackageAssets);
				for (const FAssetData& PackageAsset : PackageAssets)
				{
					if (PackageAsset.GetObjectPathString().Contains(TEXT("CompiledSection")))
					{
						PackageNames.AddUnique(PackageName);
						break;
					}
				}
			}
		}
	}

	int32 SectionsSpawned = 0;
	TSet<FString> HandledPackages;
	for (const FString& PackageName : PackageNames)
	{
		if (HandledPackages.Contains(PackageName))
		{
			continue;
		}
		HandledPackages.Add(PackageName);

		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		if (!Package)
		{
			continue;
		}

		TArray<UObject*> Objects;
		GetObjectsWithOuter(Package, Objects, EGetObjectsFlags::IncludeNestedObjects);
		for (UObject* Object : Objects)
		{
			if (UE::MeshPartition::ACompiledSection* Section = Cast<UE::MeshPartition::ACompiledSection>(Object))
			{
				SpawnProxyForSection(Section, World);
				SectionsSpawned++;
				break;
			}
		}
	}

	if (SectionsSpawned > 0)
	{
		int32 ValidMeshComponents = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->GetActorLabel().StartsWith(TEXT("MPPreview_")))
			{
				continue;
			}
			TArray<UStaticMeshComponent*> Components;
			It->GetComponents<UStaticMeshComponent>(Components);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (Component->GetStaticMesh() && Component->GetStaticMesh()->GetRenderData() != nullptr)
				{
					ValidMeshComponents++;
				}
			}
		}
		UE_LOG(LogTemp, Log, TEXT("B2FMPPreview: spawned %d editor preview actor(s) with %d valid mesh component(s) for MeshPartition terrain (%s)"),
			SectionsSpawned, ValidMeshComponents, *MapFolder);
	}
	SpawnedProxyCount = SectionsSpawned;
	return SectionsSpawned;
}

UMaterialInterface* UBorn2FlapMeshPartitionPreview::GetCachedTerrainMaterial()
{
	if (!CachedTerrainMaterial.IsValid())
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Yunsha/Materials/M_WindRippleSand.M_WindRippleSand"));
		CachedTerrainMaterial = Material;
		if (!Material)
		{
			UE_LOG(LogTemp, Warning, TEXT("B2FMPPreview: replacement terrain material not found at /Game/Yunsha/Materials/M_WindRippleSand"));
		}
	}
	return CachedTerrainMaterial.Get();
}

void UBorn2FlapMeshPartitionPreview::SpawnProxyForSection(UE::MeshPartition::ACompiledSection* Section, UWorld* World)
{
	if (!Section || !World)
	{
		return;
	}

	AActor* Proxy = World->SpawnActor<AActor>(AActor::StaticClass(), Section->GetActorTransform());
	if (!Proxy)
	{
		return;
	}
	Proxy->SetFlags(RF_Transient);
	Proxy->SetActorHiddenInGame(true);
	Proxy->SetActorLabel(*FString::Printf(TEXT("MPPreview_%s"), *Section->GetName()));

	USceneComponent* Root = NewObject<USceneComponent>(Proxy, USceneComponent::StaticClass(), TEXT("Root"));
	Root->SetFlags(RF_Transient);
	Root->SetMobility(EComponentMobility::Static);
	Proxy->SetRootComponent(Root);
	Root->RegisterComponent();

	for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& Source : Section->GetMeshComponents())
	{
		if (!Source)
		{
			continue;
		}

		UStaticMesh* Mesh = Source->GetStaticMesh();
		if (!Mesh)
		{
			continue;
		}
		if (Mesh->GetRenderData() == nullptr)
		{
			Mesh->Build(true);
		}

		UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(Proxy, UStaticMeshComponent::StaticClass(), Source->GetFName());
		Copy->SetFlags(RF_Transient);
		Copy->SetMobility(EComponentMobility::Static);
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->SetStaticMesh(Mesh);
		Copy->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;
		const int32 NumMaterials = Source->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			UMaterialInterface* Material = Source->GetMaterial(MaterialIndex);
			// The engine default channel material renders weightmap colors or
			// nothing outside the editor MaterialCache; use the real terrain
			// material so the viewport shows actual terrain.
			if (const UMaterialInstance* MI = Cast<UMaterialInstance>(Material))
			{
				if (MI->Parent && MI->Parent->GetName().Contains(TEXT("M_MeshPartition_Default")))
				{
					Material = GetCachedTerrainMaterial();
				}
			}
			else if (Material && Material->GetName().Contains(TEXT("M_MeshPartition_Default")))
			{
				Material = GetCachedTerrainMaterial();
			}
			Copy->SetMaterial(MaterialIndex, Material);
		}
		Copy->SetRelativeTransform(Source->GetRelativeTransform());
		Copy->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		Copy->RegisterComponent();
	}
}