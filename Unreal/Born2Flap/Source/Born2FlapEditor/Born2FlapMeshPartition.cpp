#include "Born2FlapMeshPartition.h"

#include "MeshPartition.h"
#include "MeshPartitionDefinition.h"
#include "MeshPartitionChannel.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionStaticMeshComponent.h"
#include "MeshPartitionDataLayerContainer.h"
#include "MeshPartitionEditorComponent.h"
#include "Modifiers/MeshPartitionBooleanModifier.h"
#include "Modifiers/MeshPartitionRemeshModifier.h"
#include "Modifiers/MeshPartitionMeshBasedModifierBase.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartitionMeshPartitionBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "RawIndexBuffer.h"

DEFINE_LOG_CATEGORY_STATIC(LogB2FMeshPartition, Log, All);

namespace
{
bool StaticMeshRenderDataToDynamicMesh(UStaticMesh* InSM, FDynamicMesh3& Out, int32 InMaterialID)
{
	Out.Clear();
	Out.EnableAttributes();
	Out.Attributes()->EnableMaterialID();

	const FStaticMeshRenderData* RenderData = InSM ? InSM->GetRenderData() : nullptr;
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
	const FRawStaticIndexBuffer& IdxBuffer = LOD.IndexBuffer;
	const uint32 NumVerts = PosBuffer.GetNumVertices();
	const uint32 NumIndices = IdxBuffer.GetNumIndices();
	if (NumVerts == 0 || NumIndices == 0)
	{
		return false;
	}

	for (uint32 V = 0; V < NumVerts; ++V)
	{
		Out.AppendVertex(FVector3d(PosBuffer.VertexPosition(V)));
	}

	for (uint32 I = 0; I + 3 <= NumIndices; I += 3)
	{
		const int32 A = static_cast<int32>(IdxBuffer.GetIndex(I));
		const int32 B = static_cast<int32>(IdxBuffer.GetIndex(I + 1));
		const int32 C = static_cast<int32>(IdxBuffer.GetIndex(I + 2));
		if (A < 0 || B < 0 || C < 0 ||
			A >= static_cast<int32>(NumVerts) || B >= static_cast<int32>(NumVerts) || C >= static_cast<int32>(NumVerts))
		{
			continue;
		}
		const int32 NewTriangle = Out.AppendTriangle(A, B, C);
		if (NewTriangle >= 0)
		{
			Out.Attributes()->GetMaterialID()->SetValue(NewTriangle, InMaterialID);
		}
	}

	Out.EnableVertexNormals(FVector3f::ZeroVector);
	UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Out);
	UE_LOG(LogB2FMeshPartition, Log, TEXT("StaticMeshRenderDataToDynamicMesh: %s -> %d verts, %d tris (render-data fallback)"),
		InSM ? *InSM->GetName() : TEXT("null"), Out.VertexCount(), Out.TriangleCount());
	return Out.TriangleCount() > 0;
}

bool StaticMeshToDynamicMesh(UStaticMesh* InSM, FDynamicMesh3& Out, int32 InMaterialID)
{
	const FMeshDescription* MD = InSM ? InSM->GetMeshDescription(0) : nullptr;
	if (!MD || MD->Triangles().Num() == 0)
	{
		UE_LOG(LogB2FMeshPartition, Warning, TEXT("StaticMeshToDynamicMesh: %s has no usable source model; extracting render data"),
			InSM ? *InSM->GetName() : TEXT("null"));
		return StaticMeshRenderDataToDynamicMesh(InSM, Out, InMaterialID);
	}

	FStaticMeshConstAttributes Attrs(*MD);
	const auto& Positions = Attrs.GetVertexPositions();

	TMap<FVertexInstanceID, int32> InstanceToVertex;
	InstanceToVertex.Reserve(MD->VertexInstances().Num());

	Out.Clear();
	Out.EnableAttributes();
	Out.Attributes()->EnableMaterialID();

	for (const FTriangleID TriangleID : MD->Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> Instances = MD->GetTriangleVertexInstances(TriangleID);
		int32 Corners[3];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVertexInstanceID Instance = Instances[Corner];
			if (const int32* Existing = InstanceToVertex.Find(Instance))
			{
				Corners[Corner] = *Existing;
				continue;
			}
			const FVertexID VertexID = MD->GetVertexInstanceVertex(Instance);
			const int32 NewVertex = Out.AppendVertex(FVector3d(Positions.Get(VertexID)));
			InstanceToVertex.Add(Instance, NewVertex);
			Corners[Corner] = NewVertex;
		}
		const int32 NewTriangle = Out.AppendTriangle(Corners[0], Corners[1], Corners[2]);
		// Material ID = channel slot index used by the definition's channel map.
		Out.Attributes()->GetMaterialID()->SetValue(NewTriangle, InMaterialID);
	}

	Out.EnableVertexNormals(FVector3f::ZeroVector);
	UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Out);
	return Out.TriangleCount() > 0;
}

template <typename TModifier>
AActor* SpawnModifierActor(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
	const FTransform& InTransform, const TCHAR* InLabel, TModifier** OutModifier)
{
	UWorld* World = InEditorComponent->GetWorld();
	UE::MeshPartition::AMeshPartition* Owner = InEditorComponent->GetOwner<UE::MeshPartition::AMeshPartition>();
	if (!World || !Owner)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.bCreateActorPackage = true;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), InTransform, Params);
	if (!Actor)
	{
		return nullptr;
	}
	Actor->SetActorLabel(InLabel);
	Actor->bIsEditorOnlyActor = true;
	Actor->SetIsSpatiallyLoaded(true);

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("SceneComponent"), RF_Transactional);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();

	TModifier* Modifier = NewObject<TModifier>(Actor, TModifier::StaticClass(), NAME_None, RF_Transactional);
	if (!Modifier)
	{
		World->DestroyActor(Actor);
		return nullptr;
	}

	Modifier->SetAffectedMeshPartition(Owner);
	Actor->AddInstanceComponent(Modifier);
	Modifier->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
	Modifier->RegisterComponent();

	*OutModifier = Modifier;
	return Actor;
}
} // namespace

bool UBorn2FlapMeshPartition::IsAvailable()
{
	return true;
}

UE::MeshPartition::UMeshPartitionDefinition* UBorn2FlapMeshPartition::CreateDefinition(
	const FString& InPackagePath, const FString& InAssetName,
	const FString& InTemplatePath, const TArray<FName>& InChannels, float InChannelTexelSize)
{
	const FString PackageName = InPackagePath + TEXT("/") + InAssetName;

	// Reclaim the package name when a previous generation is still in memory.
	if (UPackage* StalePackage = FindPackage(nullptr, *PackageName))
	{
		StalePackage->Rename(*FString::Printf(TEXT("%s_STALE_%s"), *PackageName,
			*FGuid::NewGuid().ToString()), nullptr, REN_ForceNoResetLoaders | REN_NonTransactional);
	}

	UPackage* Package = CreatePackage(*PackageName);
	UE::MeshPartition::UMeshPartitionDefinition* Definition =
		NewObject<UE::MeshPartition::UMeshPartitionDefinition>(Package, *InAssetName, RF_Public | RF_Standalone);
	if (!Definition)
	{
		return nullptr;
	}

	// Deep-copy the template (engine default definition): material, build variants,
	// transformer pipelines, per-platform runtime settings, physics channels...
	if (!InTemplatePath.IsEmpty())
	{
		UE::MeshPartition::UMeshPartitionDefinition* Template =
			LoadObject<UE::MeshPartition::UMeshPartitionDefinition>(nullptr, *InTemplatePath);
		if (Template)
		{
			UEngine::CopyPropertiesForUnrelatedObjects(Template, Definition);
		}
		else
		{
			UE_LOG(LogB2FMeshPartition, Warning, TEXT("CreateDefinition: template not found (%s)"), *InTemplatePath);
		}
	}

	if (!ConfigureDefinition(Definition, InChannels, InChannelTexelSize))
	{
		return nullptr;
	}

	Package->MarkPackageDirty();
	if (!UEditorLoadingAndSavingUtils::SavePackages({ Package }, /*bOnlyDirty=*/false))
	{
		UE_LOG(LogB2FMeshPartition, Error, TEXT("CreateDefinition: failed to save %s"), *PackageName);
		return nullptr;
	}
	return Definition;
}

bool UBorn2FlapMeshPartition::EnsureWorldPartition(UWorld* InWorld)
{
	if (!InWorld || !InWorld->PersistentLevel)
	{
		return false;
	}

	UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	if (!WorldPartition)
	{
		WorldPartition = UWorldPartition::CreateOrRepairWorldPartition(InWorld->GetWorldSettings());
	}

	if (WorldPartition)
	{
		// Mirrors WorldPartitionConvertCommandlet (SetUseExternalActors(true) +
		// bIsPartitioned = true): without these flags UWorld::SpawnActor silently
		// ignores bCreateActorPackage, modifier actors serialize inline and the
		// WorldPartitionMeshPartitionBuilder finds zero modifier descriptors.
		InWorld->PersistentLevel->SetUseExternalActors(true);
		InWorld->PersistentLevel->bIsPartitioned = true;
	}

	UE_LOG(LogB2FMeshPartition, Log, TEXT("EnsureWorldPartition: %s (partitioned=%d, external_actors=%d)"),
		WorldPartition ? TEXT("enabled") : TEXT("FAILED"),
		InWorld->PersistentLevel->bIsPartitioned ? 1 : 0,
		InWorld->PersistentLevel->IsUsingExternalActors() ? 1 : 0);
	return WorldPartition != nullptr;
}

bool UBorn2FlapMeshPartition::ConfigureDefinition(UE::MeshPartition::UMeshPartitionDefinition* InDefinition,
	const TArray<FName>& InChannels, float InChannelTexelSize)
{
	if (!InDefinition)
	{
		return false;
	}

	UE::MeshPartition::FChannelMap& ChannelMap = InDefinition->GetChannelMap();

	// FChannelMap::ChannelDescs is a private UPROPERTY; mutate through reflection.
	FArrayProperty* ChannelDescsProp = CastField<FArrayProperty>(
		UE::MeshPartition::FChannelMap::StaticStruct()->FindPropertyByName(TEXT("ChannelDescs")));
	if (!ChannelDescsProp)
	{
		UE_LOG(LogB2FMeshPartition, Error, TEXT("ConfigureDefinition: ChannelDescs property not found"));
		return false;
	}

	FScriptArrayHelper Helper(ChannelDescsProp, ChannelDescsProp->ContainerPtrToValuePtr<void>(&ChannelMap));
	Helper.EmptyValues();

	const FStructProperty* InnerProp = CastField<FStructProperty>(ChannelDescsProp->Inner);
	FProperty* NameProp = InnerProp ? InnerProp->Struct->FindPropertyByName(TEXT("Name")) : nullptr;

	for (const FName& Channel : InChannels)
	{
		const int32 Index = Helper.AddValue();
		if (NameProp)
		{
			*NameProp->ContainerPtrToValuePtr<FName>(Helper.GetRawPtr(Index)) = Channel;
		}
	}

	if (FFloatProperty* TexelSizeProp = CastField<FFloatProperty>(
		UE::MeshPartition::UMeshPartitionDefinition::StaticClass()->FindPropertyByName(TEXT("ChannelTexelSize"))))
	{
		TexelSizeProp->SetPropertyValue_InContainer(InDefinition, InChannelTexelSize);
	}

	InDefinition->MarkPackageDirty();
	UE_LOG(LogB2FMeshPartition, Log, TEXT("ConfigureDefinition: %d channel(s) installed"), InChannels.Num());
	return true;
}

UE::MeshPartition::AMeshPartition* UBorn2FlapMeshPartition::CreateMeshPartition(UWorld* InWorld,
	UE::MeshPartition::UMeshPartitionDefinition* InDefinition, const FString& InLabel, const FTransform& InTransform)
{
	if (!InWorld)
	{
		return nullptr;
	}

	// Mirrors UConvertTool::Shutdown: identity spawn, then assign transform.
	UE::MeshPartition::AMeshPartition* MeshPartition =
		InWorld->SpawnActor<UE::MeshPartition::AMeshPartition>(UE::MeshPartition::AMeshPartition::StaticClass(), FTransform::Identity);
	if (!MeshPartition)
	{
		return nullptr;
	}

	UE::MeshPartition::UMeshPartitionEditorComponent* EditorComponent =
		NewObject<UE::MeshPartition::UMeshPartitionEditorComponent>(MeshPartition,
			UE::MeshPartition::UMeshPartitionEditorComponent::StaticClass(), TEXT("MegaMeshEditorComponent"), RF_Transactional);
	MeshPartition->SetMeshPartitionComponent(EditorComponent);

	if (InDefinition)
	{
		MeshPartition->SetMeshPartitionDefinition(InDefinition);
	}
	MeshPartition->SetActorTransform(InTransform);
	MeshPartition->SetActorLabel(*InLabel);
	return MeshPartition;
}

UE::MeshPartition::UMeshPartitionEditorComponent* UBorn2FlapMeshPartition::GetEditorComponent(
	UE::MeshPartition::AMeshPartition* InMeshPartition)
{
	return InMeshPartition ? Cast<UE::MeshPartition::UMeshPartitionEditorComponent>(
		InMeshPartition->GetMeshPartitionComponent()) : nullptr;
}

AActor* UBorn2FlapMeshPartition::AddBaseModifierFromStaticMesh(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
	UStaticMesh* InMesh, const FTransform& InTransform, const TArray<UMaterialInterface*>& InMaterials,
	int32 InMaterialID)
{
	if (!InEditorComponent)
	{
		return nullptr;
	}

	FDynamicMesh3 DynamicMesh;
	if (!StaticMeshToDynamicMesh(InMesh, DynamicMesh, InMaterialID))
	{
		UE_LOG(LogB2FMeshPartition, Warning, TEXT("AddBaseModifierFromStaticMesh: no source mesh data for %s"),
			InMesh ? *InMesh->GetName() : TEXT("null"));
		return nullptr;
	}

	const int32 TriCount = DynamicMesh.TriangleCount();
	AActor* BaseActor = InEditorComponent->SpawnBaseModifier(MoveTemp(DynamicMesh), InMaterials, InTransform);
	UE_LOG(LogB2FMeshPartition, Log, TEXT("Base modifier '%s': %d tris"),
		BaseActor ? *BaseActor->GetActorLabel() : TEXT("<failed>"), TriCount);
	return BaseActor;
}

AActor* UBorn2FlapMeshPartition::AddBooleanModifier(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
	UStaticMesh* InToolMesh, const FTransform& InTransform, int32 Operation, FName InChannelToWrite, float InWeight)
{
	if (!InEditorComponent || !InToolMesh)
	{
		return nullptr;
	}

	UE::MeshPartition::UBooleanModifier* Modifier = nullptr;
	const TCHAR* ActorLabel = Operation == 2 ? TEXT("MeshPartitionBooleanTrim")
		: (Operation == 1 ? TEXT("MeshPartitionBooleanCarve") : TEXT("MeshPartitionBooleanUnion"));
	AActor* Actor = SpawnModifierActor<UE::MeshPartition::UBooleanModifier>(InEditorComponent, InTransform,
		ActorLabel, &Modifier);
	if (!Actor)
	{
		return nullptr;
	}

	Modifier->SetMeshSourceMode(UE::MeshPartition::EModifierMeshSourceMode::StaticMesh);
	Modifier->SetStaticMesh(InToolMesh);

	// BooleanOp / ToolMeshEmbedding / WeightChannels are private UPROPERTYs;
	// author them through reflection (stable: they are EditAnywhere metadata).
	auto SetEnum = [](UObject* InObject, const TCHAR* InName, int64 InValue)
	{
		FProperty* Property = InObject->GetClass()->FindPropertyByName(InName);
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			if (FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty())
			{
				Underlying->SetIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(InObject), InValue);
				return true;
			}
		}
		return false;
	};

	const int64 BooleanOp = Operation == 2 ? (int64)UE::MeshPartition::EBooleanOperation::Trim
		: (Operation == 1 ? (int64)UE::MeshPartition::EBooleanOperation::Subtract
		                  : (int64)UE::MeshPartition::EBooleanOperation::Union);
	SetEnum(Modifier, TEXT("BooleanOp"), BooleanOp);
	SetEnum(Modifier, TEXT("ToolMeshEmbedding"),
		(int64)UE::MeshPartition::EBooleanToolMeshEmbedding::Intersecting);

	if (InChannelToWrite != NAME_None)
	{
		UE::MeshPartition::FBooleanModifierWeightEntry Entry;
		Entry.ChannelName = InChannelToWrite;
		Entry.SourceMode = UE::MeshPartition::EBooleanModifierChannelSourceMode::Constant;
		Entry.WriteValue = InWeight;
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(
			Modifier->GetClass()->FindPropertyByName(TEXT("WeightChannels"))))
		{
			if (FStructProperty* StructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
			{
				FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Modifier));
				const int32 Index = Helper.AddValue();
				StructProperty->CopySingleValue(Helper.GetRawPtr(Index), &Entry);
			}
		}
	}

	return Actor;
}

AActor* UBorn2FlapMeshPartition::AddRemeshModifier(UE::MeshPartition::UMeshPartitionEditorComponent* InEditorComponent,
	const FVector& InWorldLocation, const FVector& InUnscaledCoverage, float InTargetEdgeLength)
{
	if (!InEditorComponent)
	{
		return nullptr;
	}

	UE::MeshPartition::URemeshModifier* Modifier = nullptr;
	AActor* Actor = SpawnModifierActor<UE::MeshPartition::URemeshModifier>(InEditorComponent,
		FTransform(InWorldLocation), TEXT("MeshPartitionRemesh"), &Modifier);
	if (!Actor)
	{
		return nullptr;
	}

	Modifier->SetWorldLocation(InWorldLocation);
	Modifier->SetUnscaledCoverage(FVector3d(InUnscaledCoverage));
	Modifier->SetTargetEdgeLength(InTargetEdgeLength);
	return Actor;
}

bool UBorn2FlapMeshPartition::LoadWorldPartitionRegion(UWorld* InWorld, const FVector& InExtent)
{
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		return false;
	}
	UWorldPartition* WP = InWorld->GetWorldPartition();
	const FBox Box(-InExtent, InExtent);

	TSet<FGuid> Intersecting;
	if (WP->EditorHash)
	{
		WP->EditorHash->ForEachIntersectingActor(Box, [&Intersecting](FWorldPartitionActorDescInstance* Desc)
		{
			Intersecting.Add(Desc->GetGuid());
		});
	}
	UE_LOG(LogB2FMeshPartition, Log, TEXT("LoadWorldPartitionRegion: initialized=%d hash=%s intersecting=%d"),
		WP->IsInitialized() ? 1 : 0,
		WP->EditorHash ? *WP->EditorHash->GetClass()->GetName() : TEXT("null"),
		Intersecting.Num());

	UWorldPartitionEditorLoaderAdapter* Adapter =
		WP->CreateEditorLoaderAdapter<FLoaderAdapterShape>(
			InWorld, Box, TEXT("B2F Probe Loader"));
	if (!Adapter || !Adapter->GetLoaderAdapter())
	{
		return false;
	}
	Adapter->GetLoaderAdapter()->SetUserCreated(true);
	Adapter->GetLoaderAdapter()->Load();

	int32 LoadedActors = 0;
	for (TActorIterator<AActor> It(InWorld); It; ++It)
	{
		++LoadedActors;
	}
	UE_LOG(LogB2FMeshPartition, Log, TEXT("LoadWorldPartitionRegion: total actors after load=%d"), LoadedActors);
	return true;
}

int32 UBorn2FlapMeshPartition::CountCompiledSections(UWorld* InWorld)
{
	if (!InWorld)
	{
		return 0;
	}
	int32 Count = 0;
	for (TActorIterator<UE::MeshPartition::ACompiledSection> It(InWorld); It; ++It)
	{
		++Count;
	}
	return Count;
}

TArray<FString> UBorn2FlapMeshPartition::AuditSectionWinding(UWorld* InWorld, const TArray<FVector>& InProbePoints)
{
	TArray<FString> Result;
	if (!InWorld)
	{
		return Result;
	}

	for (TActorIterator<UE::MeshPartition::ACompiledSection> It(InWorld); It; ++It)
	{
		for (const TObjectPtr<UE::MeshPartition::UMeshPartitionStaticMeshComponent>& SMC : It->GetMeshComponents())
		{
			const UStaticMesh* SM = SMC ? SMC->GetStaticMesh() : nullptr;
			const FStaticMeshRenderData* RenderData = SM ? SM->GetRenderData() : nullptr;
			if (!RenderData || RenderData->LODResources.Num() == 0)
			{
				Result.Add(FString::Printf(TEXT("%s: no render mesh"), *It->GetName()));
				continue;
			}

			const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
			const FPositionVertexBuffer& Verts = LOD.VertexBuffers.PositionVertexBuffer;
			const uint32 NumTris = uint32(LOD.IndexBuffer.GetNumIndices()) / 3u;
			if (NumTris == 0 || Verts.GetNumVertices() == 0)
			{
				Result.Add(FString::Printf(TEXT("%s: empty render mesh"), *It->GetName()));
				continue;
			}

			for (const FVector& Probe : InProbePoints)
			{
				double BestDist = TNumericLimits<double>::Max();
				double BestZ = 0.0;
				FVector BestNormal(0, 0, 0);
				const FVector2D Probe2D(Probe.X, Probe.Y);
				for (uint32 T = 0; T < NumTris; ++T)
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
				if (BestDist < TNumericLimits<double>::Max())
				{
					BestNormal.Normalize();
					Result.Add(FString::Printf(TEXT("%s %s probe=(%.0f,%.0f) triCenterZ=%.0f normalZ=%.3f %s"),
						*It->GetName(), SM ? *SM->GetName() : TEXT("-"),
						Probe.X, Probe.Y, BestZ, BestNormal.Z,
						BestNormal.Z > 0.0 ? TEXT("FACES_UP") : TEXT("FACES_DOWN")));
				}
			}
		}
	}
	return Result;
}

int32 UBorn2FlapMeshPartition::ActivateAllDataLayers(UWorld* InWorld)
{
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		return -1;
	}
	UDataLayerManager* DataLayerManager = InWorld->GetWorldPartition()->GetDataLayerManager();
	if (!DataLayerManager)
	{
		return -1;
	}
	int32 Count = 0;
	for (UDataLayerInstance* Instance : DataLayerManager->GetDataLayerInstances())
	{
		if (Instance && !Instance->IsRuntime())
		{
			DataLayerManager->SetDataLayerInstanceRuntimeState(Instance, EDataLayerRuntimeState::Activated, /*bIsRecursive=*/true);
			UE_LOG(LogB2FMeshPartition, Log, TEXT("ActivateAllDataLayers: %s -> Activated"), *Instance->GetDataLayerShortName());
			++Count;
		}
	}
	return Count;
}

int32 UBorn2FlapMeshPartition::SetVariantDataLayersInitialRuntimeState(UWorld* InWorld, bool bLoaded)
{
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		return -1;
	}
	UDataLayerManager* DataLayerManager = InWorld->GetWorldPartition()->GetDataLayerManager();
	if (!DataLayerManager)
	{
		return -1;
	}
	// Platform policy: the desktop game loads the common gameplay variant plus
	// ONE render variant. Loading all three variants simultaneously stacks
	// three coplanar terrain copies (z-fighting, "broken" look) and triples
	// the runtime collision-cook cost (the spawn fall-through delay).
	const EDataLayerRuntimeState TargetState = bLoaded ? EDataLayerRuntimeState::Loaded : EDataLayerRuntimeState::Unloaded;
	int32 Count = 0;
	for (UDataLayerInstance* Instance : DataLayerManager->GetDataLayerInstances())
	{
		if (!Instance || !Instance->IsRuntime())
		{
			continue;
		}
		const UDataLayerAsset* Asset = Instance->GetAsset();
		if (!Asset)
		{
			continue;
		}
		// Only touch MeshPartition-owned variant data layers (outer = the
		// AMeshPartitionDataLayerContainer living in the map package).
		const UE::MeshPartition::AMeshPartitionDataLayerContainer* Container =
			Asset->GetTypedOuter<UE::MeshPartition::AMeshPartitionDataLayerContainer>();
		if (!Container)
		{
			continue;
		}
		const FString ShortName = Instance->GetDataLayerShortName();
		EDataLayerRuntimeState LayerTarget = TargetState;
		if (bLoaded && ShortName.Contains(TEXT("LowEnd")))
		{
			LayerTarget = EDataLayerRuntimeState::Unloaded;   // desktop renders the high-end variant
		}
		if (Instance->GetInitialRuntimeState() != LayerTarget)
		{
			Instance->SetInitialRuntimeState(LayerTarget);
			UE_LOG(LogB2FMeshPartition, Log, TEXT("SetVariantDataLayersInitialRuntimeState: %s -> %s"),
				*ShortName,
				LayerTarget == EDataLayerRuntimeState::Loaded ? TEXT("Loaded") : TEXT("Unloaded"));
			++Count;
		}
	}
	if (Count > 0 && InWorld->GetWorldDataLayers())
	{
		InWorld->GetWorldDataLayers()->MarkPackageDirty();
	}
	return Count;
}

int32 UBorn2FlapMeshPartition::SaveExternalActorPackages(UWorld* InWorld)
{
	// Mirrors FEditorFileUtils::AutosaveMapEx: UEditorLoadingAndSavingUtils::SaveMap
	// saves only the map package itself; external actor packages (Mesh Partition
	// modifier actors, compiled sections) must be saved explicitly or the
	// WorldPartitionBuilderCommandlet sees "0 modifiers" for every partition.
	if (!InWorld || !InWorld->PersistentLevel)
	{
		return -1;
	}

	TArray<UPackage*> PackagesToSave;
	for (UPackage* ExternalPackage : InWorld->PersistentLevel->GetLoadedExternalObjectPackages())
	{
		if (ExternalPackage->IsDirty()
			&& FPackageName::IsValidLongPackageName(ExternalPackage->GetName(), false)
			&& IsValid(ExternalPackage->FindAssetInPackage()))
		{
			PackagesToSave.Add(ExternalPackage);
		}
	}

	if (PackagesToSave.Num() > 0)
	{
		UE_LOG(LogB2FMeshPartition, Log, TEXT("Saving %d external actor package(s)"), PackagesToSave.Num());
		FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
		for (UPackage* ExternalPackage : PackagesToSave)
		{
			UE_LOG(LogB2FMeshPartition, Log, TEXT("  saved external actor package '%s'"), *ExternalPackage->GetName());
		}
	}
	return PackagesToSave.Num();
}

TArray<FString> UBorn2FlapMeshPartition::DebugListActors(UWorld* InWorld)
{
	TArray<FString> Result;
	if (!InWorld)
	{
		return Result;
	}
	for (TActorIterator<AActor> It(InWorld); It; ++It)
	{
		Result.Add(FString::Printf(TEXT("%s %s"), *It->GetClass()->GetName(), *It->GetName()));
	}
	return Result;
}

void UBorn2FlapMeshPartition::LogDefinitionInfo(UE::MeshPartition::UMeshPartitionDefinition* InDefinition)
{
	if (!InDefinition)
	{
		UE_LOG(LogB2FMeshPartition, Warning, TEXT("LogDefinitionInfo: null definition"));
		return;
	}
	FString ChannelList;
	for (const FName& Channel : InDefinition->GetChannelMap().GetChannels())
	{
		ChannelList += Channel.ToString() + TEXT(",");
	}
	UE_LOG(LogB2FMeshPartition, Log, TEXT("Definition %s material=%s texelSize=%.1f channels=%s"),
		*InDefinition->GetName(),
		InDefinition->GetMaterial() ? *InDefinition->GetMaterial()->GetName() : TEXT("<none>"),
		InDefinition->GetChannelTexelSize(),
		*ChannelList);
	for (const UE::MeshPartition::FCompiledSectionBuildVariant& Variant : InDefinition->GetCompiledSectionBuildVariants())
	{
		UE_LOG(LogB2FMeshPartition, Log, TEXT("  variant '%s' splitToRuntimeGrid=%d pipeline=%s"),
			*Variant.Name.ToString(), Variant.bSplitSectionsToMatchWorldPartitionRuntimeGrid ? 1 : 0,
			Variant.TransformerPipeline ? *Variant.TransformerPipeline->GetName() : TEXT("<none>"));
	}
}

// ---------------------------------------------------------------------------
// World Partition descriptor diagnostics (persistence verification)
// ---------------------------------------------------------------------------
int32 UBorn2FlapMeshPartition::CountActorDescs(UWorld* InWorld)
{
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		return -1;
	}
	UActorDescContainerInstance* ContainerInstance = InWorld->GetWorldPartition()->GetActorDescContainerInstance();
	const UActorDescContainer* Container = ContainerInstance ? ContainerInstance->GetContainer() : nullptr;
	return Container ? Container->GetActorDescCount() : -1;
}

TArray<FString> UBorn2FlapMeshPartition::DumpActorDescState(UWorld* InWorld)
{
	TArray<FString> Result;
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		Result.Add(TEXT("no world partition"));
		return Result;
	}
	UActorDescContainerInstance* ContainerInstance = InWorld->GetWorldPartition()->GetActorDescContainerInstance();
	const UActorDescContainer* Container = ContainerInstance ? ContainerInstance->GetContainer() : nullptr;
	if (!Container)
	{
		Result.Add(TEXT("no actor desc container"));
		return Result;
	}
	Result.Add(FString::Printf(TEXT("container=%s descs=%d wp_initialized=%d external_actors=%d"),
		*Container->GetName(), Container->GetActorDescCount(),
		InWorld->GetWorldPartition()->IsInitialized() ? 1 : 0,
		InWorld->PersistentLevel->IsUsingExternalActors() ? 1 : 0));
	for (const auto& Pair : Container->GetActorsByGuid())
	{
		const FWorldPartitionActorDesc* Desc = Pair.Value ? Pair.Value->Get() : nullptr;
		if (!Desc)
		{
			continue;
		}
		Result.Add(FString::Printf(TEXT("  %s %s guid=%s pkg=%s"),
			*Desc->GetBaseClass().ToString(),
			*Desc->GetDisplayClassName().ToString(),
			*Desc->GetGuid().ToString(),
			*Desc->GetActorPackage().ToString()));
	}
	return Result;
}

// ---------------------------------------------------------------------------
// Template-based World Partition map creation
// ---------------------------------------------------------------------------
UWorld* UBorn2FlapMeshPartition::NewMapFromTemplateCpp(const FString& InTemplatePath)
{
	// Loads the template into an untitled package (bLoadAsTemplate=true) so the
	// template file itself is never overwritten; the world keeps the template's
	// World Partition setup (runtime hash, grids, data layers).
	return UEditorLoadingAndSavingUtils::NewMapFromTemplate(InTemplatePath, false);
}

// ---------------------------------------------------------------------------
// Interactive-editor-compatible save order
// ---------------------------------------------------------------------------
bool UBorn2FlapMeshPartition::SaveWorldExternalFirst(UWorld* InWorld, const FString& InMapPath)
{
	if (!InWorld || !InWorld->PersistentLevel)
	{
		return false;
	}

	// 1) External actor packages first: saving them triggers the descriptor
	//    container's OnObjectPreSave, which registers each actor descriptor in
	//    the map package's container. If the map were saved first, a first-time
	//    headless save would serialize an empty descriptor container and the
	//    world would reload with none of its external actors.
	TArray<UPackage*> ExternalPackages;
	for (UPackage* ExternalPackage : InWorld->PersistentLevel->GetLoadedExternalObjectPackages())
	{
		if (!ExternalPackage || !FPackageName::IsValidLongPackageName(ExternalPackage->GetName(), false))
		{
			continue;
		}
		// A destroyed authoring actor (StripAuthoringActors) is pending kill but
		// its external package lingers in the loaded list; PromptForCheckoutAndSave
		// then dereferences it and access-violates. Skip packages with no live actor.
		bool bHasLiveActor = false;
		ForEachObjectWithOuter(ExternalPackage, [&bHasLiveActor](UObject* Object)
		{
			if (const AActor* Actor = Cast<AActor>(Object))
			{
				if (IsValid(Actor))
				{
					bHasLiveActor = true;
				}
			}
		});
		if (!bHasLiveActor)
		{
			continue;
		}
		ExternalPackages.Add(ExternalPackage);
	}
	if (ExternalPackages.Num() > 0)
	{
		const FEditorFileUtils::EPromptReturnCode PromptResult =
			FEditorFileUtils::PromptForCheckoutAndSave(ExternalPackages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
		if (PromptResult == FEditorFileUtils::PR_Failure)
		{
			UE_LOG(LogB2FMeshPartition, Error, TEXT("SaveWorldExternalFirst: external actor package save failed"));
			return false;
		}
		UE_LOG(LogB2FMeshPartition, Log, TEXT("SaveWorldExternalFirst: saved %d external actor package(s)"), ExternalPackages.Num());
	}

	// 2) The map package (serializes the descriptor container).
	return UEditorLoadingAndSavingUtils::SaveMap(InWorld, InMapPath);
}

// ---------------------------------------------------------------------------
// In-process compiled section build (no separate commandlet process)
// ---------------------------------------------------------------------------
bool UBorn2FlapMeshPartition::BuildMeshPartitionInProcess(UWorld* InWorld, bool bReuseByModifierHash)
{
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		UE_LOG(LogB2FMeshPartition, Error, TEXT("BuildMeshPartitionInProcess: world is not partitioned"));
		return false;
	}

	UE::MeshPartition::UWorldPartitionMeshPartitionBuilder* Builder =
		NewObject<UE::MeshPartition::UWorldPartitionMeshPartitionBuilder>(
			GetTransientPackage(), UE::MeshPartition::UWorldPartitionMeshPartitionBuilder::StaticClass());
	if (!Builder)
	{
		return false;
	}

	// ReuseMode is a private UPROPERTY(meta=(CommandLineArgument)); author it
	// through reflection (the commandlet normally reads it from the command line).
	if (FEnumProperty* ReuseModeProperty = CastField<FEnumProperty>(
		Builder->GetClass()->FindPropertyByName(TEXT("ReuseMode"))))
	{
		const int64 ReuseModeValue = bReuseByModifierHash
			? static_cast<int64>(UE::MeshPartition::EMeshPartitionReuseMode::ByModifierHash)
			: static_cast<int64>(UE::MeshPartition::EMeshPartitionReuseMode::ForceRebuild);
		if (FNumericProperty* Underlying = ReuseModeProperty->GetUnderlyingProperty())
		{
			Underlying->SetIntPropertyValue(ReuseModeProperty->ContainerPtrToValuePtr<void>(Builder), ReuseModeValue);
		}
	}

	const bool bOk = Builder->RunBuilder(InWorld);
	UE_LOG(LogB2FMeshPartition, Log, TEXT("BuildMeshPartitionInProcess: RunBuilder %s"), bOk ? TEXT("OK") : TEXT("FAILED"));
	Builder->MarkAsGarbage();
	return bOk;
}

// ---------------------------------------------------------------------------
// Data layer / actor desc diagnostics + compiled-section pin loading
// ---------------------------------------------------------------------------
namespace
{
// Pinned references keep compiled-section actors alive after LoadAllCompiledSections
// returns, so the Python probe can collect mesh statistics on them.
TArray<FWorldPartitionReference> PinnedCompiledSectionReferences;
}

TArray<FString> UBorn2FlapMeshPartition::DumpDataLayerStates(UWorld* InWorld)
{
	TArray<FString> Result;
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		Result.Add(TEXT("no world partition"));
		return Result;
	}
	UDataLayerManager* DataLayerManager = InWorld->GetWorldPartition()->GetDataLayerManager();
	if (!DataLayerManager)
	{
		Result.Add(TEXT("no data layer manager"));
		return Result;
	}
	const TArray<UDataLayerInstance*>& Instances = DataLayerManager->GetDataLayerInstances();
	Result.Add(FString::Printf(TEXT("data layer instances: %d"), Instances.Num()));
	for (UDataLayerInstance* Instance : Instances)
	{
		if (!Instance)
		{
			continue;
		}
		Result.Add(FString::Printf(TEXT("  DL %s runtime=%d loadedInEditor=%d initiallyLoaded=%d initialRuntime=%s runtime=%s effectiveLoaded=%d"),
			*Instance->GetDataLayerShortName(),
			Instance->IsRuntime() ? 1 : 0,
			Instance->IsLoadedInEditor() ? 1 : 0,
			Instance->IsInitiallyLoadedInEditor() ? 1 : 0,
			*StaticEnum<EDataLayerRuntimeState>()->GetNameStringByValue(static_cast<int64>(Instance->GetInitialRuntimeState())),
			*StaticEnum<EDataLayerRuntimeState>()->GetNameStringByValue(static_cast<int64>(Instance->GetRuntimeState())),
			Instance->IsEffectiveLoadedInEditor() ? 1 : 0));
	}
	return Result;
}

TArray<FString> UBorn2FlapMeshPartition::DumpActorDescDetails(UWorld* InWorld)
{
	TArray<FString> Result;
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		Result.Add(TEXT("no world partition"));
		return Result;
	}
	UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	UActorDescContainerInstance* ContainerInstance = WorldPartition->GetActorDescContainerInstance();
	const UActorDescContainer* Container = ContainerInstance ? ContainerInstance->GetContainer() : nullptr;
	if (!Container)
	{
		Result.Add(TEXT("no actor desc container"));
		return Result;
	}
	for (const auto& Pair : Container->GetActorsByGuid())
	{
		const FWorldPartitionActorDesc* Desc = Pair.Value ? Pair.Value->Get() : nullptr;
		if (!Desc)
		{
			continue;
		}
		const FWorldPartitionActorDescInstance* DescInstance =
			ContainerInstance ? ContainerInstance->GetActorDescInstance(Desc->GetGuid()) : nullptr;
		if (!DescInstance)
		{
			Result.Add(FString::Printf(TEXT("  DESC %s %s NO-INSTANCE"),
				*Desc->GetBaseClass().ToString(), *Desc->GetActorLabelOrName().ToString()));
			continue;
		}
		const FBox EditorBounds = DescInstance->GetEditorBounds();
		FString DataLayerNames;
		for (const FName& DataLayer : DescInstance->GetDataLayers())
		{
			DataLayerNames += DataLayer.ToString();
			DataLayerNames += TEXT(",");
		}
		Result.Add(FString::Printf(TEXT("  DESC %s %s spatial=%d runtimeOnly=%d editorRelevant=%d loaded=%d boundsValid=%d center=%s extent=%s dataLayers=%s"),
			*Desc->GetBaseClass().ToString(), *DescInstance->GetActorLabelOrName().ToString(),
			DescInstance->GetIsSpatiallyLoaded() ? 1 : 0,
			DescInstance->GetActorIsRuntimeOnly() ? 1 : 0,
			DescInstance->IsEditorRelevant() ? 1 : 0,
			DescInstance->IsLoaded() ? 1 : 0,
			EditorBounds.IsValid ? 1 : 0,
			*EditorBounds.GetCenter().ToString(), *EditorBounds.GetExtent().ToString(), *DataLayerNames));
	}
	return Result;
}

int32 UBorn2FlapMeshPartition::LoadAllCompiledSections(UWorld* InWorld)
{
	PinnedCompiledSectionReferences.Reset();
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		return -1;
	}
	UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	UActorDescContainerInstance* ContainerInstance = WorldPartition->GetActorDescContainerInstance();
	const UActorDescContainer* Container = ContainerInstance ? ContainerInstance->GetContainer() : nullptr;
	if (!Container)
	{
		return -1;
	}
	int32 Loaded = 0;
	for (const auto& Pair : Container->GetActorsByGuid())
	{
		const FWorldPartitionActorDesc* Desc = Pair.Value ? Pair.Value->Get() : nullptr;
		// Compiled-section descs are namespaced (UE::MeshPartition::ACompiledSection):
		// GetBaseClass() resolves to an empty path for them, while GetDisplayClassName()
		// returns "CompiledSection". Match on both so LoadAllCompiledSections actually
		// finds them (the Nov 13 "invisible terrain" probe false-negative root cause).
		if (!Desc
			|| (!Desc->GetBaseClass().ToString().Contains(TEXT("CompiledSection"))
				&& !Desc->GetDisplayClassName().ToString().Contains(TEXT("CompiledSection"))))
		{
			continue;
		}
		FWorldPartitionReference Reference(WorldPartition, Desc->GetGuid());
		if (AActor* Actor = Reference.GetActor())
		{
			UE_LOG(LogB2FMeshPartition, Log, TEXT("LoadAllCompiledSections: loaded %s (%s)"),
				*Actor->GetName(), *Desc->GetActorPackage().ToString());
			++Loaded;
		}
		else
		{
			UE_LOG(LogB2FMeshPartition, Warning, TEXT("LoadAllCompiledSections: FAILED to load %s pkg=%s"),
				*Desc->GetGuid().ToString(), *Desc->GetActorPackage().ToString());
		}
		// Pin the reference regardless: successful loads must stay alive for the
		// probe; failed ones stay pinned to make the failure visible in GC.
		PinnedCompiledSectionReferences.Add(Reference);
	}
	return Loaded;
}

int32 UBorn2FlapMeshPartition::StripAuthoringActors(UWorld* InWorld)
{
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		return 0;
	}
	UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	UActorDescContainerInstance* ContainerInstance = WorldPartition->GetActorDescContainerInstance();
	const UActorDescContainer* Container = ContainerInstance ? ContainerInstance->GetContainer() : nullptr;
	if (!Container)
	{
		return 0;
	}

	// Editor-module actor classes of the Mesh Partition authoring stack
	// (plus brushes: editor-only template leftovers that must not ship in
	// the game-loadable map).
	const TSet<FName> AuthoringClasses = {
		TEXT("ModifierActor"),
		TEXT("InteractiveSection"),
		TEXT("PreviewSection"),
		TEXT("Brush"),
	};

	// Pass 1: force-load every authoring actor by descriptor (spatially loaded
	// actors outside the boot region are not visible to TActorIterator) and
	// collect them. Pinned references keep the actors alive.
	TArray<AActor*> ToDestroy;
	TArray<FWorldPartitionReference> Pinned;
	Pinned.Reserve(16);
	for (const auto& Pair : Container->GetActorsByGuid())
	{
		const FWorldPartitionActorDesc* Desc = Pair.Value ? Pair.Value->Get() : nullptr;
		if (!Desc)
		{
			continue;
		}
		FName ClassName = NAME_None;
		if (const UClass* NativeClass = Desc->GetActorNativeClass())
		{
			ClassName = NativeClass->GetFName();
		}
		else
		{
			ClassName = *Desc->GetBaseClass().GetAssetName().ToString();
		}
		bool bAuthoring = AuthoringClasses.Contains(ClassName);

		// Criterion 2: any component desc whose native class lives in the
		// editor-only MeshPartitionEditor module. Our generator spawns the
		// modifier authoring actors as PLAIN AActors carrying modifier
		// components (/Script/MeshPartitionEditor.*), so the actor-class
		// name check alone cannot see them; their component descs are what
		// asserts in an uncooked -game load (WorldPartitionActorDesc.cpp:944).
		if (!bAuthoring)
		{
			for (const TUniquePtr<FWorldPartitionComponentDesc>& CompDesc : Desc->GetComponentDescs())
			{
				if (CompDesc && CompDesc->GetNativeClass().ToString().StartsWith(TEXT("/Script/MeshPartitionEditor.")))
				{
					bAuthoring = true;
					break;
				}
			}
		}
		if (!bAuthoring)
		{
			continue;
		}
		FWorldPartitionReference Reference(WorldPartition, Desc->GetGuid());
		if (AActor* Actor = Reference.GetActor())
		{
			UE_LOG(LogB2FMeshPartition, Log, TEXT("StripAuthoringActors: staging %s (%s) pkg=%s"),
				*Actor->GetName(), *ClassName.ToString(), *Desc->GetActorPackage().ToString());
			ToDestroy.Add(Actor);
		}
		else
		{
			UE_LOG(LogB2FMeshPartition, Warning, TEXT("StripAuthoringActors: FAILED to load %s (%s)"),
				*Desc->GetGuid().ToString(), *ClassName.ToString());
		}
		Pinned.Add(Reference);
	}

	// Pass 2: destroy (desc removal mutates the container map, so never
	// during the iteration above).
	int32 Destroyed = 0;
	for (AActor* Actor : ToDestroy)
	{
		UE_LOG(LogB2FMeshPartition, Log, TEXT("StripAuthoringActors: destroying %s (%s)"),
			*Actor->GetName(), *Actor->GetClass()->GetName());
		InWorld->DestroyActor(Actor);
		++Destroyed;
	}

	// Pass 3: loaded actors without descriptors (non-WP-managed, e.g. the
	// builder brush saved from a map template) matched by class name.
	for (TActorIterator<AActor> It(InWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && AuthoringClasses.Contains(Actor->GetClass()->GetFName()))
		{
			UE_LOG(LogB2FMeshPartition, Log, TEXT("StripAuthoringActors: destroying %s (%s)"),
				*Actor->GetName(), *Actor->GetClass()->GetName());
			InWorld->DestroyActor(Actor);
			++Destroyed;
		}
	}

	UE_LOG(LogB2FMeshPartition, Log, TEXT("StripAuthoringActors: destroyed %d authoring actors"), Destroyed);
	return Destroyed;
}

TArray<FString> UBorn2FlapMeshPartition::DumpDescComponents(UWorld* InWorld)
{
	TArray<FString> Out;
	if (!InWorld || !InWorld->GetWorldPartition())
	{
		Out.Add(TEXT("no world partition"));
		return Out;
	}
	UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
	UActorDescContainerInstance* ContainerInstance = WorldPartition->GetActorDescContainerInstance();
	const UActorDescContainer* Container = ContainerInstance ? ContainerInstance->GetContainer() : nullptr;
	if (!Container)
	{
		Out.Add(TEXT("no container"));
		return Out;
	}
	for (const auto& Pair : Container->GetActorsByGuid())
	{
		const FWorldPartitionActorDesc* Desc = Pair.Value ? Pair.Value->Get() : nullptr;
		if (!Desc)
		{
			continue;
		}
		const UClass* ActorClass = Desc->GetActorNativeClass();
		const FString ActorClassName = ActorClass ? ActorClass->GetName() : Desc->GetBaseClass().GetAssetName().ToString();
		for (const TUniquePtr<FWorldPartitionComponentDesc>& CompDesc : Desc->GetComponentDescs())
		{
			if (CompDesc)
			{
				Out.Add(FString::Printf(TEXT("%s %s"), *ActorClassName, *CompDesc->ToString()));
			}
		}
	}
	return Out;
}