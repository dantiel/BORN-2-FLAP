#include "Probe/Born2FlapTerrainProbe.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/Born2FlapGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Camera/CameraActor.h"
#include "CollisionQueryParams.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionStaticMeshComponent.h"
#include "MeshPartitionCollisionComponent.h"
#include "MeshPartitionMaterialCacheCommon.h"
#include "Materials/MaterialInstance.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "HighResScreenshot.h"

void UBorn2FlapTerrainProbe::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bEnabled = FParse::Param(FCommandLine::Get(), TEXT("B2FTerrainProbe"));
	if (bEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN probe enabled (game runtime terrain diagnostic)"));
	}
}

void UBorn2FlapTerrainProbe::Deinitialize()
{
	Super::Deinitialize();
}

static void TraceOne(UWorld* World, const FVector& Start, const FVector& End,
	const FString& Label, ECollisionChannel Channel, int32& OutHits, int32& OutTraces)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(B2FTerrainProbe), true);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, Channel, Params);
	OutTraces++;
	if (bHit)
	{
		OutHits++;
		UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_TRACE %s ch=%d HIT dist=%.0f actor=%s loc=(%.0f,%.0f,%.0f)"),
			*Label, (int32)Channel, Hit.Distance,
			Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"),
			Hit.Location.X, Hit.Location.Y, Hit.Location.Z);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_TRACE %s ch=%d MISS"), *Label, (int32)Channel);
	}
}

void UBorn2FlapTerrainProbe::Tick(float DeltaSeconds)
{
	Time += DeltaSeconds;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString CurrentMap = World->GetMapName();
	const bool bIsTargetMap = CurrentMap.Contains(TEXT("YUNSHA"));
	if (!bIsTargetMap)
	{
		if (FMath::FloorToInt(Time * 2.f) % 4 == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN t=%.1f waiting for terrain map (current=%s)"), Time, *CurrentMap);
		}
		return;
	}

	// Build the point list on first contact with the terrain map.
	if (Points.Num() == 0)
	{
		MapName = CurrentMap;
		if (CurrentMap.Contains(TEXT("YUNSHA")))
		{
			Points.Add({ TEXT("deep_desert"),      FVector(200000, 300000, 0) });
			Points.Add({ TEXT("lake_basin"),       FVector(13000, -49000, 0) });
			Points.Add({ TEXT("excavation_pit"),   FVector(148000, -11000, 0) });
			Points.Add({ TEXT("escarpment"),       FVector(-80000, -30000, 0) });
			Points.Add({ TEXT("oasis_shore"),      FVector(17500, -52000, 0) });
			Points.Add({ TEXT("spawn_poi"),        FVector(13000, -68500, 0) });
		}
		else
		{
			Points.Add({ TEXT("mountain_summit"),  FVector(0, 0, 0) });
			Points.Add({ TEXT("canyon_floor"),     FVector(0, -8000, 0) });
			Points.Add({ TEXT("arch_crown"),       FVector(24000, 4000, 0) });
			Points.Add({ TEXT("flat_ground"),      FVector(35000, 35000, 0) });
		}
		PointIndex = -1;
		PointWait = 0.f;
		UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN map=%s points=%d"), *CurrentMap, Points.Num());
	}

	if (Time >= NextDump)
	{
		NextDump = Time + 2.f;
		DumpState(World);
	}

	// Teleport the player pawn to the next point, give streaming ~4s, trace.
	if (PointWait <= 0.f)
	{
		PointIndex++;
		if (PointIndex >= Points.Num())
		{
			// All trace points done: physics drop test. Does the bird's
			// rigid body actually collide with the MP terrain (not just
			// line traces)? Teleport it above the deep-desert surface and
			// watch whether it lands or falls through.
			if (!bDropTestDone)
			{
				if (DropWait <= 0.f)
				{
					APlayerController* PC = World->GetFirstPlayerController();
					if (PC && PC->GetPawn())
					{
						PC->GetPawn()->SetActorLocation(FVector(200000, 300000, 1933), false, nullptr, ETeleportType::TeleportPhysics);
						if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(PC->GetPawn()->GetRootComponent()))
						{
							Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
							Root->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
						}
						UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_DROP teleported pawn above deep_desert"));
					}
					DropWait = 3.5f;
				}
				else
				{
					DropWait -= DeltaSeconds;
					if (DropWait <= 0.f)
					{
						APlayerController* PC = World->GetFirstPlayerController();
						const FVector BirdPos = PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
						UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_DROP bird=(%.0f,%.0f,%.0f) expectSurfaceZ=1433"),
							BirdPos.X, BirdPos.Y, BirdPos.Z);
						bDropTestDone = true;
						Finish(World);
						return;
					}
				}
			}
			return;
		}
		PointWait = 6.f;
		bShotRequested = false;
		APlayerController* PC = World->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			const FVector Pos = Points[PointIndex].Value;
			PC->GetPawn()->SetActorLocation(Pos + FVector(0, 0, 3000), false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN point[%d]=%s teleported pawn to (%.0f,%.0f,3000)"),
				PointIndex, *Points[PointIndex].Key, Pos.X, Pos.Y);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN point[%d]=%s NO PLAYER PAWN - tracing without forced streaming"),
				PointIndex, *Points[PointIndex].Key);
			PointWait = 1.f; // nothing to wait for without a streaming source
		}
	}
	else
	{
		PointWait -= DeltaSeconds;

		if (RestoreWait > 0.f)
		{
			RestoreWait -= DeltaSeconds;
			if (RestoreWait <= 0.f)
			{
				APlayerController* PC = World->GetFirstPlayerController();
				if (PC && ProbeOriginalPawn.IsValid())
				{
					PC->SetViewTargetWithBlend(ProbeOriginalPawn.Get(), 0.f);
				}
				ProbeOriginalPawn.Reset();
			}
		}

		if (bShotPending)
		{
			ShotDelay -= DeltaSeconds;
			if (ShotDelay <= 0.f)
			{
				bShotPending = false;
				APlayerController* PC = World->GetFirstPlayerController();
				const FVector CamLoc = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
				const FRotator CamRot = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
				UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_SHOT closeup point[%d]=%s cam=(%.0f,%.0f,%.0f) rot=(%.0f,%.0f,%.0f)"),
					PointIndex, *Points[PointIndex].Key, CamLoc.X, CamLoc.Y, CamLoc.Z, CamRot.Pitch, CamRot.Yaw, CamRot.Roll);
				FScreenshotRequest::RequestScreenshot(*FString::Printf(TEXT("V:/BORN 2 FLAP/_terrain_close_%d"), PointIndex), false, false);
			}
		}
		else if ((PointIndex == 5 || PointIndex == 4 || PointIndex == 2) && PointWait <= 3.f && !bShotRequested)
		{
			bShotRequested = true;
			APlayerController* PC = World->GetFirstPlayerController();
			if (PC)
			{
				ProbeOriginalPawn = PC->GetPawn();
				// Teleport the PAWN itself to the point: MP streaming follows the
				// pawn, so the terrain streams at proper LOD around it. Shoot the
				// player view (the CameraActor path showed only sky — the render
				// mesh never streamed for a far-away camera).
				const FVector GroundPos = Points[PointIndex].Value;
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(B2FTerrainProbe), true);
				World->LineTraceSingleByChannel(Hit, GroundPos + FVector(0.f, 0.f, 20000.f), GroundPos + FVector(0.f, 0.f, -20000.f), ECC_WorldStatic, Params);
				const float GroundZ = Hit.bBlockingHit ? Hit.Location.Z : 300.f;
				if (APawn* Pawn = PC->GetPawn())
				{
					Pawn->SetActorLocation(FVector(GroundPos.X, GroundPos.Y, GroundZ + 800.f), false, nullptr, ETeleportType::TeleportPhysics);
					if (UPawnMovementComponent* Move = Pawn->GetMovementComponent())
					{
						Move->StopMovementImmediately();
						Move->Velocity = FVector::ZeroVector;
					}
				}
				PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
				// CANARY: with the pawn teleported here the MP sections stream in,
				// then force an unlit magenta material onto every section SMC at
				// the LAST shot. If the visible ground stays warm, the sections
				// are not what the camera sees.
				if (PointIndex == 5)
				{
					UMaterialInterface* Probe = LoadObject<UMaterialInterface>(
						nullptr, TEXT("/Game/Yunsha/Materials/M_ProbeMagenta.M_ProbeMagenta"));
					int32 N = 0;
					for (TActorIterator<UE::MeshPartition::ACompiledSection> It(World); It; ++It)
					{
						for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& SMC : It->GetMeshComponents())
						{
							if (SMC)
							{
								UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_CANARY smc=%s oldmat=%s -> M_ProbeMagenta"),
									*SMC->GetName(), SMC->GetMaterial(0) ? *SMC->GetMaterial(0)->GetName() : TEXT("-"));
								SMC->SetMaterial(0, Probe);
								SMC->MarkRenderStateDirty();
								N++;
							}
						}
					}
					UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_CANARY forced probe material on %d SMCs"), N);
				}
				bShotPending = true;
				ShotDelay = 1.8f;
				RestoreWait = 0.f;
			}
		}
		if (PointWait <= 0.f && PointIndex >= 0 && PointIndex < Points.Num())
		{
			const FVector Pos = Points[PointIndex].Value;
			TraceOne(World, Pos + FVector(0, 0, 30000), Pos + FVector(0, 0, -40000),
				Points[PointIndex].Key, ECC_WorldStatic, TotalHits, TotalTraces);
			TraceOne(World, Pos + FVector(0, 0, 30000), Pos + FVector(0, 0, -40000),
				Points[PointIndex].Key, ECC_Visibility, TotalHits, TotalTraces);
			// Compare with the analytic ground height the game logic uses
			// (spawn, safety floor, camera anchor) — a disagreement here
			// means the flight safety system fights the real terrain.
			if (const ABorn2FlapGameMode* GM = Cast<ABorn2FlapGameMode>(World->GetAuthGameMode()))
			{
				UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_GH %s analyticGround=%.0f"),
					*Points[PointIndex].Key, GM->GroundHeight(Pos.X, Pos.Y));
			}
		}
	}
}

void UBorn2FlapTerrainProbe::DumpState(UWorld* World)
{
	int32 MPCount = 0;
	FString DefName = TEXT("-");
	for (TActorIterator<UE::MeshPartition::AMeshPartition> It(World); It; ++It)
	{
		MPCount++;
		if (const UE::MeshPartition::UMeshPartitionDefinition* Def = It->GetMeshPartitionDefinition())
		{
			DefName = Def->GetName();
		}
	}

	int32 SectionCount = 0;
	int32 SectionSMCs = 0;
	int32 SectionCols = 0;
	int32 SectionMeshed = 0;
	int32 SectionPhysicsReady = 0;
	int32 SectionPhysicsTotal = 0;
	for (TObjectIterator<UE::MeshPartition::ACompiledSection> It; It; ++It)
	{
		if (It->GetWorld() != World)
		{
			continue;
		}
		SectionCount++;
		SectionSMCs += It->GetMeshComponents().Num();
		SectionCols += It->GetCollisionComponents().Num();
		UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_SEC %s init=%d regAll=%d inLevelActors=%d loc=(%.0f,%.0f,%.0f)"),
			*It->GetName(),
			It->IsActorInitialized() ? 1 : 0,
			It->HasActorRegisteredAllComponents() ? 1 : 0,
			It->GetLevel() && It->GetLevel()->Actors.Contains(*It) ? 1 : 0,
			It->GetActorLocation().X, It->GetActorLocation().Y, It->GetActorLocation().Z);
		for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& SMC : It->GetMeshComponents())
		{
			if (SMC)
			{
				UStaticMesh* SM = SMC->GetStaticMesh();
				UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_SMC %s mesh=%s registered=%d"),
					*SMC->GetName(),
					SM ? *SM->GetName() : TEXT("-"),
					SMC->IsRegistered() ? 1 : 0);
				UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_SMCR %s pkg=%s renderData=%d verts0=%d tris0=%d mats=%d mat0=%s bounds=(%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)"),
					SM ? *SM->GetName() : TEXT("-"),
					SM ? *SM->GetOutermost()->GetName() : TEXT("-"),
					SM && SM->GetRenderData() ? 1 : 0,
					SM ? SM->GetNumVertices(0) : -1,
					SM ? SM->GetNumTriangles(0) : -1,
					SM ? SM->GetStaticMaterials().Num() : -1,
					SM && SM->GetStaticMaterials().Num() > 0 ? *SM->GetStaticMaterials()[0].MaterialInterface->GetName() : TEXT("-"),
					SM ? SM->GetBoundingBox().Min.X : 0.f, SM ? SM->GetBoundingBox().Min.Y : 0.f, SM ? SM->GetBoundingBox().Min.Z : 0.f,
					SM ? SM->GetBoundingBox().Max.X : 0.f, SM ? SM->GetBoundingBox().Max.Y : 0.f, SM ? SM->GetBoundingBox().Max.Z : 0.f);
				UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_VIS smc=%s visible=%d visFlag=%d bHiddenInGame=%d mainPass=%d depthPass=%d castShadow=%d vtPass=%d matCache=%d worldGame=%d"),
					*SMC->GetName(),
					SMC->IsVisible() ? 1 : 0,
					SMC->GetVisibleFlag() ? 1 : 0,
					SMC->bHiddenInGame ? 1 : 0,
					SMC->bRenderInMainPass ? 1 : 0,
					SMC->bRenderInDepthPass ? 1 : 0,
					SMC->CastShadow ? 1 : 0,
					(int32)SMC->VirtualTextureRenderPassType,
					UE::MeshPartition::IsMaterialCacheEnabled(World) ? 1 : 0,
					World->IsGameWorld() ? 1 : 0);
				if (UMaterialInterface* Mat = SMC->GetMaterial(0))
				{
					UMaterialInstance* MI = Cast<UMaterialInstance>(Mat);
					UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_MAT smc=%s mat=%s parent=%s blend=%d opaque=%d"),
						*SMC->GetName(),
						*Mat->GetName(),
						MI && MI->Parent ? *MI->Parent->GetName() : TEXT("NULL"),
						(int32)Mat->GetBlendMode(),
						Mat->GetBlendMode() == BLEND_Opaque ? 1 : 0);
				}
				else
				{
					UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_MAT smc=%s mat=NULL"), *SMC->GetName());
				}
				// IsNaniteEnabled() is editor-only (Nanite settings are baked
				// editor data); the game target cannot call it, so the flag is
				// only reported when editor data is present.
	#if WITH_EDITORONLY_DATA
				const int32 B2FProbeNaniteEnabled = SM ? (SM->IsNaniteEnabled() ? 1 : 0) : -1;
	#else
				const int32 B2FProbeNaniteEnabled = -1;
	#endif
				UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_NANITE mesh=%s naniteEnabled=%d validNaniteData=%d"),
					SM ? *SM->GetName() : TEXT("-"),
					B2FProbeNaniteEnabled,
					SM && SM->GetRenderData() ? (SM->GetRenderData()->HasValidNaniteData() ? 1 : 0) : -1);
				// Winding audit: find the triangle nearest the spawn POI and report
				// the face normal implied by its index winding (Z sign = up/down).
				if (SM && SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)
				{
					const FStaticMeshLODResources& LOD = SM->GetRenderData()->LODResources[0];
					const FPositionVertexBuffer& Verts = LOD.VertexBuffers.PositionVertexBuffer;
					const int32 NumTris = int32(LOD.IndexBuffer.GetNumIndices() / 3);
					if (NumTris > 0 && Verts.GetNumVertices() > 0)
					{
						const FVector2D Probe2D(13000.0, -68500.0);
						double BestDist = TNumericLimits<double>::Max();
						FVector BestNormal(0, 0, 0);
						double BestZ = 0.0;
						for (int32 T = 0; T < NumTris; ++T)
						{
							const uint32 I0 = LOD.IndexBuffer.GetIndex(T * 3 + 0);
							const uint32 I1 = LOD.IndexBuffer.GetIndex(T * 3 + 1);
							const uint32 I2 = LOD.IndexBuffer.GetIndex(T * 3 + 2);
							if (I0 >= Verts.GetNumVertices() || I1 >= Verts.GetNumVertices() || I2 >= Verts.GetNumVertices())
							{
								continue;
							}
							const FVector3f& P0 = Verts.VertexPosition(I0);
							const FVector3f& P1 = Verts.VertexPosition(I1);
							const FVector3f& P2 = Verts.VertexPosition(I2);
							const FVector2D C2D(
								(double(P0.X) + P1.X + P2.X) / 3.0,
								(double(P0.Y) + P1.Y + P2.Y) / 3.0);
							const double D = FVector2D::DistSquared(C2D, Probe2D);
							if (D < BestDist)
							{
								BestDist = D;
								BestNormal = FVector::CrossProduct(
									FVector(P1) - FVector(P0), FVector(P2) - FVector(P0));
								BestZ = (double(P0.Z) + P1.Z + P2.Z) / 3.0;
							}
						}
						BestNormal.Normalize();
						UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_WINDING mesh=%s spawnTri centerZ=%.0f normalZ=%.3f (normalZ>0 => faces UP, <0 => faces DOWN)"),
							*SM->GetName(), BestZ, BestNormal.Z);
					}
				}
			}
		}
		for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& SMC : It->GetMeshComponents())
		{
			if (SMC && SMC->GetStaticMesh())
			{
				SectionMeshed++;
			}
		}
		for (const TObjectPtr<UE::MeshPartition::UMeshPartitionCollisionComponent>& Col : It->GetCollisionComponents())
		{
			if (!Col)
			{
				continue;
			}
			SectionPhysicsTotal++;
			if (Col->IsPhysicsStateCreated())
			{
				SectionPhysicsReady++;
			}
			UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_COL section=%s col=%s registered=%d physics=%d enabled=%d bodyValid=%d worldPhysics=%d"),
				*It->GetName(), *Col->GetName(),
				Col->IsRegistered() ? 1 : 0,
				Col->IsPhysicsStateCreated() ? 1 : 0,
				(int32)Col->GetCollisionEnabled(),
				Col->GetBodyInstance() && Col->GetBodyInstance()->IsValidBodyInstance() ? 1 : 0,
				World->GetPhysicsScene() != nullptr ? 1 : 0);
		}
	}

	int32 SMActorCount = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		SMActorCount++;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	FVector PawnLoc = PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;

	UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN t=%.1f map=%s mp=%d def=%s sections=%d sectionSMCs=%d sectionCols=%d sectionMeshed=%d physics=%d/%d smActors=%d pawn=(%.0f,%.0f,%.0f)"),
		Time, *World->GetMapName(), MPCount, *DefName, SectionCount, SectionSMCs, SectionCols, SectionMeshed,
		SectionPhysicsReady, SectionPhysicsTotal, SMActorCount,
		PawnLoc.X, PawnLoc.Y, PawnLoc.Z);
}

void UBorn2FlapTerrainProbe::Finish(UWorld* World)
{
	DumpState(World);
	UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_VERDICT map=%s hits=%d/%d"),
		*MapName, TotalHits, TotalTraces);
	UE_LOG(LogTemp, Display, TEXT("B2FTERRAIN_DONE"));
	bDone = true;
	FGenericPlatformMisc::RequestExit(false);
}