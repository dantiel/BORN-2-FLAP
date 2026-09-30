#include "Flight/Born2FlapWingMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"

void UBorn2FlapWingMesh::InitializeWing(int32 Side, int32 Design)
{
    WingSide=Side; WingDesign=Design;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UV.Reset(); Colours.Reset(); Triangles.Reset(); RestVertices.Reset();
    auto Patch=[&](int32 NS,int32 NC,TFunction<FVector(double,double)> Position,FLinearColor Colour)
    {
        const int32 Base=RestVertices.Num();
        for(int32 S=0;S<=NS;++S) for(int32 C=0;C<=NC;++C)
        {
            const FVector P=Position(double(C)/NC,double(S)/NS);
            RestVertices.Add(FVector(P.X,Side*P.Y,P.Z));
            UV.Add(FVector2D((30-P.X)/95.,P.Y/105.)); Colours.Add(Colour);
            if(S<NS && C<NC)
            {
                const int32 A=Base+S*(NC+1)+C,B=A+1,D=A+NC+1,E=D+1;
                if(Side>0) Triangles.Append({A,B,D,B,E,D}); else Triangles.Append({A,D,B,B,D,E});
            }
        }
    };
    const FLinearColor Ink(.025,.038,.055), Slate(.065,.09,.12), Teal(.015,.27,.30), Ivory(.88,.83,.66);
    if(Design==0)
    {
        // Broad folded arm with seven distinct, parallel slotted pinions.
        Patch(20,12,[](double U,double V){return FVector(FMath::Lerp(17.,12.,V)-U*FMath::Lerp(47.,37.,V),V*54.6,2.8*FMath::Sin(PI*U)*FMath::Sin(PI*V));},Ink);
        for(int32 I=0;I<7;++I)
        {
            const double X=13-I*6.,Tip=(63+10*FMath::Sin((I+1)*PI/9.))*1.4;
            Patch(24,6,[X,Tip](double U,double V){return FVector(X+2.5-3.7*V-U*FMath::Lerp(5.6,1.4,V),FMath::Lerp(40.6,Tip,V),1.2*FMath::Sin(PI*U));},I%3==0?Slate:Ink);
        }
    }
    else if(Design==1)
    {
        auto Ellipse=[&](FVector Centre,double RX,double RY,double Angle,FLinearColor Colour)
        {
            Patch(24,12,[=](double U,double V){
                const double Y=(2*V-1)*RY, X=(1-2*U)*RX*FMath::Sqrt(FMath::Max(.0001,1-FMath::Square(2*V-1)));
                const double A=FMath::DegreesToRadians(Angle);
                return Centre+FVector(X*FMath::Cos(A)-Y*FMath::Sin(A),X*FMath::Sin(A)+Y*FMath::Cos(A),1.5*FMath::Sin(PI*U)*FMath::Sin(PI*V));
            },Colour);
        };
        Ellipse(FVector(-4,38,0),22.5,42.5,8,Teal);
        for(int32 I=0;I<5;++I) Ellipse(FVector(-16-I*3,49+I*9,-1),( .43-I*.035)*50,10,15+I*7,I%2?Ivory:Teal);
    }
    else
        Patch(32,16,[](double U,double V){return FVector(17-14*V-10*V*V*V-U*(47-36*V*V*V),100*V,0);},FLinearColor(.09,.24,.29));
    ApplyShape(nullptr,FTransform::Identity);
    CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,Colours,Tangents,false);
    SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,WingDesign==2 ? TEXT("/Game/Birds/M_FalconWing") : TEXT("/Game/Birds/M_WingMembrane")));
}

void UBorn2FlapWingMesh::ApplyShape(const B2F_WingSection* Shape,const FTransform& BodyTransform)
{
    if(Shape) { bReceivedShape=true; for(uint32 I=0;I<B2F_WING_STATIONS;++I) PeakBend=FMath::Max(PeakBend,FMath::Abs(Shape[I].bend_m)); }
    Vertices.SetNum(RestVertices.Num()); Normals.Init(FVector::ZeroVector,Vertices.Num()); Tangents.SetNum(Vertices.Num());
    for(int32 I=0;I<Vertices.Num();++I)
    {
        const FVector R=RestVertices[I];
        const double V=FMath::Clamp(FMath::Abs(R.Y)/(WingDesign==1?95.:102.),0.,1.);
        double Camber=FMath::Lerp(.035,.025,V),Twist=FMath::DegreesToRadians(FMath::Lerp(5.,-2.,V)),Bend=0;
        if(Shape)
        {
            const double Station=FMath::Clamp(V*B2F_WING_STATIONS-.5,0.,double(B2F_WING_STATIONS-1));
            const int32 A=FMath::FloorToInt(Station),B=FMath::Min(A+1,int32(B2F_WING_STATIONS)-1); const double T=Station-A;
            Camber=FMath::Lerp(Shape[A].camber,Shape[B].camber,T); Twist=FMath::Lerp(Shape[A].twist_rad,Shape[B].twist_rad,T);
            Bend=FMath::Lerp(Shape[A].bend_m,Shape[B].bend_m,T)*100*FMath::Clamp(V/Shape[0].span_fraction,0.,1.);
        }
        const double Leading=17-14*V-(WingDesign==2?10*V*V*V:0),Chord=WingDesign==2?47-36*V*V*V:47;
        const double U=FMath::Clamp((Leading-R.X)/Chord,0.,1.),Pivot=Leading-.25*Chord,X=R.X-Pivot;
        const double Z=R.Z+4*U*(1-U)*Camber*Chord;
        const FVector Deflection=GetComponentTransform().InverseTransformVectorNoScale(BodyTransform.TransformVectorNoScale(FVector(0,0,Bend)));
        Vertices[I]=FVector(Pivot+X*FMath::Cos(Twist)-Z*FMath::Sin(Twist),R.Y,X*FMath::Sin(Twist)+Z*FMath::Cos(Twist))+Deflection;
    }
    for(int32 I=0;I<Triangles.Num();I+=3)
    {
        const int32 A=Triangles[I],B=Triangles[I+1],C=Triangles[I+2];
        const FVector N=FVector::CrossProduct(Vertices[C]-Vertices[A],Vertices[B]-Vertices[A]);
        Normals[A]+=N; Normals[B]+=N; Normals[C]+=N;
    }
    for(int32 I=0;I<Vertices.Num();++I)
    {
        Normals[I]=Normals[I].GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        Tangents[I]=FProcMeshTangent(FVector::VectorPlaneProject(FVector(-1,0,0),Normals[I]).GetSafeNormal(),WingSide<0);
    }
    if(GetProcMeshSection(0)) UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colours,Tangents);
}
void UBorn2FlapWingMesh::SetPaintTexture(UTexture2D* Texture)
{
    if(auto* M=CreateDynamicMaterialInstance(0)) { M->SetTextureParameterValue(TEXT("WingPaint"),Texture); M->SetScalarParameterValue(TEXT("PaintStrength"),Texture?1.f:0.f); }
}
bool UBorn2FlapWingMesh::HasValidDeformation()
{
    if(!bReceivedShape || PeakBend<.001 || Vertices.Num()<500) return false;
    const auto* Section=GetProcMeshSection(0); if(!Section || Section->ProcVertexBuffer.Num()!=UV.Num()) return false;
    for(int32 I=0;I<Vertices.Num();++I)
        if(Vertices[I].ContainsNaN() || !Normals[I].IsNormalized() || !Section->ProcVertexBuffer[I].UV0.Equals(UV[I],1.e-6)) return false;
    return true;
}