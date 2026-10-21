#pragma once
#include "CoreMinimal.h"

namespace HarvestWatch
{
enum class EState { Perched, Feeding, Hopping, Fleeing, Circling, Returning, Feinting };
inline bool Airborne(EState S)
{return S==EState::Fleeing || S==EState::Circling || S==EState::Returning || S==EState::Feinting;}
struct FRaven
{
    FVector Position=FVector::ZeroVector,Target=FVector::ZeroVector,Velocity=FVector::ZeroVector,HopStart=FVector::ZeroVector;
    EState State=EState::Feeding;
    float Timer=0,Linger=0,Yaw=0,Bank=0,HeadYaw=0,Phase=0,Boldness=0,FlightTime=0,DecisionCooldown=0;
    int32 Plot=0,Escapes=0;
    bool ReturningToPerch=false,Decoy=false;
};
inline FVector Plot(int32 I) { return FVector((I-1)*10000.,-16400,350); }
inline FVector Perch(int32 I) { return Plot(I)+FVector(0,0,215); }
struct FRound
{
    TArray<FRaven> Birds;
    FRandomStream Random{180626};
    double Score=1000,Remaining=180,Time=0;
    float Pressure[3]={0,0,0};
    int32 Scares=0,Returns=0,Hops=0,Feints=0,Alarms=0;
    bool Running=false,Finished=false;
    FVector Crop(int32 I)
    {return FVector(Plot(I).X+Random.FRandRange(-3000,3000),-13300-Random.RandRange(2,8)*620,365);}
    bool PerchOccupied(const FRaven& B) const
    {
        for(const auto& Other:Birds)if(&Other!=&B && Other.Plot==B.Plot
            && (Other.State==EState::Perched || Other.ReturningToPerch))return true;
        return false;
    }
    void Reset()
    {
        *this=FRound();
        for(int32 I=0;I<18;++I)
        {
            FRaven B;B.Plot=I/6;B.Position=I%6==0?Perch(B.Plot):Crop(B.Plot);B.Target=B.Position;
            B.State=I%6==0?EState::Perched:EState::Feeding;
            B.Timer=Random.FRandRange(.6,2.8);B.Yaw=Random.FRandRange(-180,180);
            B.Boldness=Random.FRandRange(.1,.95);B.Phase=Random.FRandRange(0,2*PI);B.Decoy=I%6==1;
            Birds.Add(B);
        }
    }
    // Recent patrol pressure, projected approach, and crowding discourage a
    // whole flock from returning to the same guarded patch. No perfect memory.
    int32 SafestPlot(const FRaven& B,const FVector& Falcon,const FVector& FalconVelocity,bool Threat) const
    {
        int32 Best=B.Plot;double BestValue=-1.e9;
        for(int32 I=0;I<3;++I)
        {
            int32 Crowd=0;for(const auto& Other:Birds)if(Other.Plot==I)++Crowd;
            const double Distance=FMath::Min(22000.,FVector::Dist2D(Plot(I),Falcon+FalconVelocity*.8));
            const double Value=(Threat?Distance:0)-Pressure[I]*4000-Crowd*500+(I!=B.Plot?650:0);
            if(Value>BestValue){BestValue=Value;Best=I;}
        }
        return Best;
    }
    FVector EscapePoint(const FRaven& B,const FVector& Falcon)
    {
        FVector Away=(B.Position-Falcon).GetSafeNormal2D();
        // Escape sideways over the cultivated strip, clear of the warehouses
        // and inland trees. Alternate jinks instead of flying a straight ray.
        if(FMath::Abs(Away.X)<.5)Away.X=B.Phase>PI?-.8:.8;
        Away.Normalize();
        return FVector(FMath::Clamp(B.Position.X+Away.X*4800,-36000.,36000.),
            FMath::Clamp(B.Position.Y+Away.Y*1200,-18000.,-14600.),1900+B.Boldness*450);
    }
    void Flee(FRaven& B,const FVector& Falcon,bool Alarm)
    {
        B.State=EState::Fleeing;B.Target=EscapePoint(B,Falcon);B.ReturningToPerch=false;
        B.Timer=12-5*B.Boldness+Random.FRandRange(0,4);B.FlightTime=0;B.Linger=0;
        ++B.Escapes;++Scares;if(Alarm)++Alarms;
    }
    void BeginReturn(FRaven& B,const FVector& Falcon,const FVector& FalconVelocity,bool Threat)
    {
        B.Plot=SafestPlot(B,Falcon,FalconVelocity,Threat);
        B.ReturningToPerch=!PerchOccupied(B) && Random.FRand()<.3f;
        B.Target=B.ReturningToPerch?Perch(B.Plot):Crop(B.Plot);B.State=EState::Returning;B.FlightTime=0;++Returns;
    }
    // Birds stay alive before launch and after results; only scoring is gated.
    // Caller pauses this method for menus. Fixed 50 ms steps make decisions
    // independent of rendering rate, including animation and escape cooldowns.
    void Step(float Dt,const FVector& Falcon,bool Threat,const FVector& FalconVelocity=FVector::ZeroVector)
    {
        if(Dt<=0)return;
        Time+=Dt;const bool Scoring=Running && !Finished;
        const double ScoreDt=Scoring?FMath::Min(double(Dt),Remaining):0;
        if(Scoring)Remaining=FMath::Max(0.,Remaining-ScoreDt);
        for(int32 I=0;I<3;++I)
        {
            Pressure[I]=FMath::Max(0.f,Pressure[I]-Dt*.035f);
            if(Threat && FVector::Dist2D(Plot(I),Falcon)<6500)Pressure[I]=FMath::Min(1.f,Pressure[I]+Dt*.6f);
        }
        TArray<FVector> Warnings;
        if(Threat)for(const auto& B:Birds)
            if(!Airborne(B.State) && FVector::DistSquared(B.Position,Falcon)<FMath::Square(2400.))Warnings.Add(B.Position);
        for(FRaven& B:Birds)
        {
            B.DecisionCooldown-=Dt;
            const FVector Predicted=Falcon+FalconVelocity*.45;
            const double Distance=FMath::Min(FVector::Dist(B.Position,Falcon),FVector::Dist(B.Position,Predicted));
            const double Radius=2300-700*B.Boldness+FMath::Min(B.Escapes,3)*100;
            bool Alarm=false;
            if(!Airborne(B.State))for(const auto& P:Warnings)
                if(FVector::DistSquared(P,B.Position)<FMath::Square(1700.) && FVector::DistSquared(P,B.Position)>1){Alarm=true;break;}
            if(Threat && B.State!=EState::Fleeing && B.State!=EState::Circling && (Distance<Radius || Alarm))
                Flee(B,Predicted,Alarm && Distance>=Radius);
            if(Threat && B.DecisionCooldown<=0)
            {
                if((B.State==EState::Fleeing || B.State==EState::Circling) && Distance<Radius)
                {
                    B.State=EState::Fleeing;B.Target=EscapePoint(B,Predicted);
                    B.Timer=FMath::Max(B.Timer,2.f);B.DecisionCooldown=.9f;
                }
                else if(B.State==EState::Returning && FVector::Dist2D(B.Target,Predicted)<3200
                    && SafestPlot(B,Falcon,FalconVelocity,true)!=B.Plot)
                {BeginReturn(B,Falcon,FalconVelocity,true);B.DecisionCooldown=2;}
            }
            B.Timer-=Dt;
            // The sentry watches the passing falcon; feeders cock their heads
            // between pecks. Bold birds keep eating until the last moment.
            const float Look=Threat && Distance<6000?FMath::Clamp(float(FMath::FindDeltaAngleDegrees(double(B.Yaw),(Falcon-B.Position).Rotation().Yaw)),-65.f,65.f):FMath::Sin(Time*1.8+B.Phase)*32;
            B.HeadYaw=FMath::FInterpTo(B.HeadYaw,Look,Dt,5);
            if(B.State==EState::Feeding)
            {
                B.Linger+=Dt;Score-=ScoreDt*(1.5+FMath::Min(2.f,B.Linger*.08f));
                if(B.Timer<=0)
                {
                    if(Random.FRand()<.22f)
                    {
                        B.ReturningToPerch=!PerchOccupied(B) && Random.FRand()<.3f;
                        B.Target=B.ReturningToPerch?Perch(B.Plot):Crop(B.Plot);B.State=EState::Returning;B.FlightTime=0;
                    }
                    else
                    {
                        B.HopStart=B.Position;
                        B.Target=B.Position+FVector(Random.FRandRange(-260,260),0,0);
                        B.Target.X=FMath::Clamp(B.Target.X,Plot(B.Plot).X-3100,Plot(B.Plot).X+3100);
                        B.Timer=.55f;B.State=EState::Hopping;++Hops;
                    }
                }
            }
            else if(B.State==EState::Hopping)
            {
                const float T=FMath::Clamp(1-B.Timer/.55f,0.f,1.f);
                B.Position=FMath::Lerp(B.HopStart,B.Target,T)+FVector(0,0,32*FMath::Sin(T*PI));
                if(B.Timer<=0){B.Position=B.Target;B.State=EState::Feeding;B.Timer=Random.FRandRange(.8,2.7);}
            }
            else if(B.State==EState::Perched && B.Timer<=0)
            {B.Target=Crop(B.Plot);B.ReturningToPerch=false;B.State=EState::Returning;B.FlightTime=0;}
            if(Airborne(B.State))
            {
                B.FlightTime+=Dt;
                if(B.State==EState::Fleeing && FVector::DistSquared(B.Position,B.Target)<FMath::Square(650.))
                {B.State=EState::Circling;B.Target=FVector(B.Position.X,-16400,1900+B.Boldness*450);}
                if((B.State==EState::Fleeing || B.State==EState::Circling) && B.Timer<=0)
                {
                    if(B.Decoy && Threat && Distance>3000 && Distance<8000)
                    {B.State=EState::Feinting;B.Timer=2.4f;++Feints;}
                    else BeginReturn(B,Falcon,FalconVelocity,Threat);
                }
                if(B.State==EState::Feinting && B.Timer<=0)BeginReturn(B,Falcon,FalconVelocity,Threat);
                FVector Destination=B.Target;
                if(B.State==EState::Circling)
                {
                    const double Angle=Time*.8+B.Phase;
                    Destination=B.Target+FVector(FMath::Cos(Angle)*1100,FMath::Sin(Angle)*550,FMath::Sin(Angle*1.7)*140);
                }
                else if(B.State==EState::Feinting)
                {
                    FVector Side=FVector::CrossProduct(FalconVelocity.GetSafeNormal2D(),FVector::UpVector);
                    if(Side.IsNearlyZero())Side=FVector(1,0,0);
                    Destination=Falcon+Side*(B.Phase>PI?-2600:2600)+FVector(0,0,650);
                    Destination.X=FMath::Clamp(Destination.X,-36000.,36000.);
                    Destination.Y=FMath::Clamp(Destination.Y,-18000.,-14600.);
                    Destination.Z=FMath::Clamp(Destination.Z,1100.,2600.);
                }
                else if(B.State==EState::Returning)
                {
                    const double D=FVector::Dist2D(B.Position,B.Target);
                    // Smooth descent, no altitude step that traps a returning bird.
                    Destination.Z+=1050*FMath::SmoothStep(250.,1800.,D);
                }
                FVector Delta=Destination-B.Position;
                const double Speed=B.State==EState::Returning?760:1150;
                const FVector Desired=Delta.GetSafeNormal()*FMath::Min(Speed,Delta.Size()*2.5);
                B.Velocity=FMath::VInterpTo(B.Velocity,Desired,Dt,3.5);
                B.Position+=B.Velocity*Dt;
                const float OldYaw=B.Yaw;
                if(B.Velocity.SizeSquared2D()>1)B.Yaw=FMath::FixedTurn(B.Yaw,B.Velocity.Rotation().Yaw,Dt*220);
                B.Bank=FMath::FInterpTo(B.Bank,FMath::Clamp(FMath::FindDeltaAngleDegrees(OldYaw,B.Yaw)*3,-28.f,28.f),Dt,5);
                if(B.State==EState::Returning && FVector::DistSquared(B.Position,B.Target)<FMath::Square(24.))
                {
                    B.State=B.ReturningToPerch?EState::Perched:EState::Feeding;B.Timer=Random.FRandRange(1.5,4.5);
                    B.Position=B.Target;B.Velocity=FVector::ZeroVector;B.Bank=0;B.Linger=0;B.ReturningToPerch=false;
                }
            }
            else
            {
                B.Bank=0;B.Velocity=FVector::ZeroVector;
                if(B.State==EState::Hopping)B.Yaw=FMath::FixedTurn(B.Yaw,(B.Target-B.HopStart).Rotation().Yaw,Dt*240);
            }
        }
        Score=FMath::Max(0.,Score);
        if(Scoring && (Score<=0 || Remaining<=0)){Finished=true;Running=false;}
    }
    int32 Feeding(int32 PlotIndex=-1) const
    {int32 N=0;for(const auto& B:Birds)if(B.State==EState::Feeding && (PlotIndex<0 || PlotIndex==B.Plot))++N;return N;}
};
}
