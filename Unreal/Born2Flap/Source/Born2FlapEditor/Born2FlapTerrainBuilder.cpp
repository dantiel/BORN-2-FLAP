#include "Born2FlapTerrainBuilder.h"
#include "World/Born2FlapYunsha.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

void UBorn2FlapTerrainBuilder::SmoothYunshaTerrain(UStaticMesh* Mesh,FVector2D Offset,bool bHorizon)
{
    check(Mesh);
    FMeshDescription* Description=Mesh->GetMeshDescription(0);
    check(Description);
    FStaticMeshAttributes Attributes(*Description);
    const auto Positions=Attributes.GetVertexPositions();
    auto Normals=Attributes.GetVertexInstanceNormals();
    for(const FVertexInstanceID Instance:Description->VertexInstances().GetElementIDs())
    {
        const FVector3f P=Positions[Description->GetVertexInstanceVertex(Instance)];
        const double X=P.X+Offset.X,Y=P.Y+Offset.Y;
        const double DX=bHorizon?0:(ABorn2FlapYunsha::GroundHeight(X+50,Y)-ABorn2FlapYunsha::GroundHeight(X-50,Y))/100;
        const double DY=bHorizon?0:(ABorn2FlapYunsha::GroundHeight(X,Y+50)-ABorn2FlapYunsha::GroundHeight(X,Y-50))/100;
        Normals[Instance]=FVector3f(FVector(-DX,-DY,1).GetSafeNormal());
    }
    auto& Settings=Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals=false;
    Settings.bRecomputeTangents=true;
    Settings.bUseMikkTSpace=true;
    Mesh->CommitMeshDescription(0);
    Mesh->Build(false);
    Mesh->MarkPackageDirty();
}
