#include "World/Born2FlapMeshPartitionRuntime.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"
#include "HighResScreenshot.h"
#include "Misc/CommandLine.h"

#include "MeshPartition.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

void UBorn2FlapMeshPartitionRuntime::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LastScanTime = 0.0;
}

void UBorn2FlapMeshPartitionRuntime::Deinitialize()
{
	Super::Deinitialize();
}

bool UBorn2FlapMeshPartitionRuntime::IsTickable() const
{
	return GetWorld() != nullptr;
}

void UBorn2FlapMeshPartitionRuntime::Tick(float DeltaTime)
{
	// Self-screenshot diagnostic (-B2FSelfShot): capture the player's own view
	// from inside the engine ~20s after the world begins play, bypassing any
	// window occlusion (the viewport render target, exactly what the player
	// sees). Two shots: (1) the natural grounded view, then (2) after lifting
	// the pawn +800 and pitching the camera -20 so the terrain fills the frame
	// from above (the geometry the terrain probe sees).
	if ((!bSelfShotDone || !bSelfShotDone2) && FParse::Param(FCommandLine::Get(), TEXT("B2FSelfShot")))
	{
		if (!bSelfShotDone && GetWorld() && GetWorld()->GetTimeSeconds() > 20.0)
		{
			bSelfShotDone = true;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				if (PC->PlayerCameraManager)
				{
					UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: selfshot1 cam=(%.0f,%.0f,%.0f) rot=(%.0f,%.0f)"),
						PC->PlayerCameraManager->GetCameraLocation().X, PC->PlayerCameraManager->GetCameraLocation().Y, PC->PlayerCameraManager->GetCameraLocation().Z,
						PC->PlayerCameraManager->GetCameraRotation().Pitch, PC->PlayerCameraManager->GetCameraRotation().Yaw);
				}
			}
			FScreenshotRequest::RequestScreenshot(TEXT("V:/BORN 2 FLAP/_selfshot"), false, false);
			UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: self-screenshot requested"));
		}
		else if (!bSelfShotDone2 && GetWorld() && GetWorld()->GetTimeSeconds() > 22.0)
		{
			bSelfShotDone2 = true;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					// Stand inside the excavation pit (the bone-archive area):
					// this is the view the user reports as "no terrain".
					Pawn->SetActorLocation(FVector(148000.f, -11000.f, -1400.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
				if (PC->PlayerCameraManager)
				{
					FRotator Rot = PC->PlayerCameraManager->GetCameraRotation();
					Rot.Pitch = 0.f;
					PC->SetControlRotation(Rot);
					UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: selfshot2 cam=(%.0f,%.0f,%.0f) rot=(%.0f,%.0f)"),
						PC->PlayerCameraManager->GetCameraLocation().X, PC->PlayerCameraManager->GetCameraLocation().Y, PC->PlayerCameraManager->GetCameraLocation().Z,
						PC->PlayerCameraManager->GetCameraRotation().Pitch, PC->PlayerCameraManager->GetCameraRotation().Yaw);
				}
			}
			FScreenshotRequest::RequestScreenshot(TEXT("V:/BORN 2 FLAP/_selfshot2"), false, false);
			UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: self-screenshot 2 requested (pit)"));
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastScanTime < 0.5)
	{
		return;
	}
	LastScanTime = Now;
	if (Now - LastPolicyPass > 2.0)
	{
		LastPolicyPass = Now;
		EnforceVariantDataLayerPolicy();
	}
	const int32 Registered = RegisterPendingSections();
	if (Registered > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: registered %d compiled-section actor(s)"), Registered);
	}
	// Periodic safety pass: the engine default channel material is useless at
	// runtime (weightmap look / invisible), so keep the real terrain material
	// applied — also re-covers sections whose meshes were (re)built late
	// (PIE placeholder rebuilds).
	if (Now - LastMaterialPass > 4.0)
	{
		LastMaterialPass = Now;
		ApplyTerrainMaterialFixToAllSections();
	}
}

static bool LooksLikeMeshPartitionDefaultMaterial(const UMaterialInterface* Material)
{
	if (!Material)
	{
		return false;
	}
	if (Material->GetName().Contains(TEXT("M_MeshPartition_Default")))
	{
		return true;
	}
	if (const UMaterialInstance* MI = Cast<UMaterialInstance>(Material))
	{
		return MI->Parent && MI->Parent->GetName().Contains(TEXT("M_MeshPartition_Default"));
	}
	return false;
}

UMaterialInterface* UBorn2FlapMeshPartitionRuntime::GetCachedTerrainMaterial()
{
	if (!CachedTerrainMaterial.IsValid())
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Yunsha/Materials/M_WindRippleSand.M_WindRippleSand"));
		CachedTerrainMaterial = Material;
		if (!Material)
		{
			UE_LOG(LogTemp, Warning, TEXT("B2FMeshPartitionRuntime: replacement terrain material not found at /Game/Yunsha/Materials/M_WindRippleSand"));
		}
	}
	return CachedTerrainMaterial.Get();
}

void UBorn2FlapMeshPartitionRuntime::ApplyTerrainMaterialFix(UE::MeshPartition::ACompiledSection* Section)
{
	if (!Section)
	{
		return;
	}
	UMaterialInterface* Replacement = GetCachedTerrainMaterial();
	for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& SMC : Section->GetMeshComponents())
	{
		if (!SMC)
		{
			continue;
		}
		if (SMC->VirtualTextureRenderPassType != ERuntimeVirtualTextureMainPassType::Always)
		{
			SMC->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;
		}
		if (LooksLikeMeshPartitionDefaultMaterial(SMC->GetMaterial(0)))
		{
			if (Replacement)
			{
				SMC->SetMaterial(0, Replacement);
			}
			SMC->MarkRenderStateDirty();
		}
	}
}

void UBorn2FlapMeshPartitionRuntime::ApplyTerrainMaterialFixToAllSections()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TObjectIterator<UE::MeshPartition::ACompiledSection> It; It; ++It)
	{
		if (It->GetWorld() == World && !It->IsPendingKillPending())
		{
			ApplyTerrainMaterialFix(*It);
		}
	}
}

int32 UBorn2FlapMeshPartitionRuntime::RegisterPendingSections()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	int32 Registered = 0;
	for (TObjectIterator<UE::MeshPartition::ACompiledSection> It; It; ++It)
	{
		if (It->GetWorld() != World)
		{
			continue;
		}
		if (It->IsPendingKillPending() || It->HasActorRegisteredAllComponents())
		{
			continue;
		}
		It->RegisterAllComponents();
		ApplyTerrainMaterialFix(*It);
		// One-shot render-state diagnostic per section (mirrors the terrain
		// probe's dump so plain -game sessions are auditable too).
		for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& SMC : It->GetMeshComponents())
		{
			if (!SMC)
			{
				continue;
			}
			UStaticMesh* SM = SMC->GetStaticMesh();
			UMaterialInterface* Mat = SMC->GetMaterial(0);
			UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: section=%s smc=%s registered=%d hidden=%d mesh=%s renderData=%d verts=%d tris=%d mat0=%s base=%s"),
				*It->GetName(), *SMC->GetName(), SMC->IsRegistered() ? 1 : 0, SMC->bHiddenInGame ? 1 : 0,
				SM ? *SM->GetName() : TEXT("-"),
				SM && SM->GetRenderData() ? 1 : 0,
				SM ? SM->GetNumVertices(0) : -1,
				SM ? SM->GetNumTriangles(0) : -1,
				Mat ? *Mat->GetName() : TEXT("-"),
				Mat ? *Mat->GetBaseMaterial()->GetName() : TEXT("-"));
		}
		Registered++;
	}
	return Registered;
}

bool UBorn2FlapMeshPartitionRuntime::HasMeshPartitionTerrain() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<UE::MeshPartition::AMeshPartition> It(World); It; ++It)
	{
		return true;
	}
	for (TObjectIterator<UE::MeshPartition::ACompiledSection> It; It; ++It)
	{
		if (It->GetWorld() == World && !It->IsPendingKillPending())
		{
			return true;
		}
	}
	return false;
}

bool UBorn2FlapMeshPartitionRuntime::IsTerrainCollisionReadyAt(const FVector& Pos, const AActor* IgnoreActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(B2FMeshPartitionTerrainWait), true);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, Pos + FVector(0, 0, 3000), Pos + FVector(0, 0, -40000),
		ECC_WorldStatic, Params);
}

void UBorn2FlapMeshPartitionRuntime::EnforceVariantDataLayerPolicy()
{
	UWorld* World = GetWorld();
	UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
	UDataLayerManager* DataLayerManager = WorldPartition ? WorldPartition->GetDataLayerManager() : nullptr;
	if (!DataLayerManager)
	{
		return;
	}
	for (UDataLayerInstance* Instance : DataLayerManager->GetDataLayerInstances())
	{
		if (!Instance || !Instance->IsRuntime())
		{
			continue;
		}
		const FString ShortName = Instance->GetDataLayerShortName();
		if (!ShortName.StartsWith(TEXT("MPDL_")))
		{
			continue;
		}
		const EDataLayerRuntimeState Current = Instance->GetRuntimeState();
		if (ShortName.Contains(TEXT("LowEnd")))
		{
			// Desktop renders the high-end variant only; the low-end copy
			// would stack a duplicate terrain (z-fighting) and triple the
			// collision-cook cost at spawn.
			if (Current != EDataLayerRuntimeState::Unloaded)
			{
				DataLayerManager->SetDataLayerInstanceRuntimeState(Instance, EDataLayerRuntimeState::Unloaded, /*bIsRecursive=*/true);
				UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: variant policy %s -> Unloaded"), *ShortName);
			}
		}
		else if (Current == EDataLayerRuntimeState::Unloaded)
		{
			// Common/HighEnd must stream; heal maps finalized with everything
			// unloaded (the pre-fix default).
			DataLayerManager->SetDataLayerInstanceRuntimeState(Instance, EDataLayerRuntimeState::Activated, /*bIsRecursive=*/true);
			UE_LOG(LogTemp, Log, TEXT("B2FMeshPartitionRuntime: variant policy %s -> Activated"), *ShortName);
		}
	}
}