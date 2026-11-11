#pragma once
#include "CoreMinimal.h"
#include "World/Born2FlapYunsha.h"
#include "World/Born2FlapWeather.h"

// Offline reduced-order aeolian model. Solve div(k grad(phi))=0 on the
// terrain, then suppress saltation in upwind horizon shadows and slip faces.
// phi is an unwrapped wind coordinate, so ridges remain continuous at tiles.
// UV1=(phi in metres, transport mask); no simulation or texture load at runtime.
struct FYunshaRippleSolver
{
    static constexpr int N=301;
    static constexpr double Step=2000., X0=-300000., Y0=-180000.;
    TArray<double> H,K,Phi,Mask;
    FVector2D Wind;
    static double Smooth(double A,double B,double V)
    {const double T=FMath::Clamp((V-A)/(B-A),0.,1.);return T*T*(3-2*T);}
    double Sample(const TArray<double>& A,double X,double Y) const
    {
        const double GX=FMath::Clamp((X-X0)/Step,0.,double(N-1));
        const double GY=FMath::Clamp((Y-Y0)/Step,0.,double(N-1));
        const int I=FMath::Min(int(GX),N-2),J=FMath::Min(int(GY),N-2);
        return FMath::Lerp(FMath::Lerp(A[J*N+I],A[J*N+I+1],GX-I),
            FMath::Lerp(A[(J+1)*N+I],A[(J+1)*N+I+1],GX-I),GY-J);
    }
    FYunshaRippleSolver()
    {
        const double Angle=FMath::DegreesToRadians(double(Born2FlapWeather::DefaultWindDirection(TEXT("Yunsha")))
            +Born2FlapWeather::Season(TEXT("dry")).WindDirBias);
        Wind=FVector2D(FMath::Cos(Angle),FMath::Sin(Angle));
        H.SetNum(N*N);K.SetNum(N*N);Phi.SetNum(N*N);Mask.SetNum(N*N);
        for(int J=0;J<N;J++) for(int I=0;I<N;I++)
        {
            const int Q=J*N+I;const double X=X0+I*Step,Y=Y0+J*Step;
            H[Q]=ABorn2FlapYunsha::GroundHeight(X,Y);
            K[Q]=FMath::Exp(-FMath::Clamp(H[Q],-2000.,14000.)/9000.);
            Phi[Q]=X*Wind.X+Y*Wind.Y;
        }
        double Residual=0;
        for(int Iter=0;Iter<600;Iter++)
        {
            Residual=0;
            for(int Colour=0;Colour<2;Colour++) for(int J=1;J<N-1;J++) for(int I=1;I<N-1;I++)
            {
                if((I+J)%2!=Colour) continue;
                const int Q=J*N+I;double Sum=0,Weight=0;
                for(int D:{-1,1,-N,N})
                {const double W=2*K[Q]*K[Q+D]/(K[Q]+K[Q+D]);Sum+=W*Phi[Q+D];Weight+=W;}
                const double Delta=Sum/Weight-Phi[Q];
                Residual=FMath::Max(Residual,FMath::Abs(Delta));Phi[Q]+=1.7*Delta;
            }
            if(Residual<.02) break;
        }
        double Low=1,High=0;int FlatOpen=0,FlatRipple=0;
        for(int J=0;J<N;J++) for(int I=0;I<N;I++)
        {
            const int Q=J*N+I;const double X=X0+I*Step,Y=Y0+J*Step;
            const double DX=(Sample(H,X+Step,Y)-Sample(H,X-Step,Y))/(2*Step);
            const double DY=(Sample(H,X,Y+Step)-Sample(H,X,Y-Step))/(2*Step);
            FVector2D Flow((Sample(Phi,X+Step,Y)-Sample(Phi,X-Step,Y))/(2*Step),
                (Sample(Phi,X,Y+Step)-Sample(Phi,X,Y-Step))/(2*Step));
            Flow.Normalize();double Horizon=0;
            for(double D:{2000.,4000.,8000.,16000.,32000.,64000.})
                Horizon=FMath::Max(Horizon,(Sample(H,X-Flow.X*D,Y-Flow.Y*D)-H[Q])/D);
            const double Lee=DX*Flow.X+DY*Flow.Y;
            const double Oasis=FMath::Sqrt(FMath::Square((X-2500)/27000.)+FMath::Square((Y+51000)/21500.));
            Mask[Q]=(1-Smooth(.025,.15,Horizon))*(1-Smooth(.25,.55,FMath::Sqrt(DX*DX+DY*DY)))
                *Smooth(-.40,-.12,Lee)*Smooth(.65,.90,Oasis);
            if(ABorn2FlapYunsha::SpringContains(X,Y)) Mask[Q]=0;
            if(DX*DX+DY*DY<.01 && Horizon<.025 && Oasis>.95 && !ABorn2FlapYunsha::SpringContains(X,Y))
            {++FlatOpen;if(Mask[Q]>.7) ++FlatRipple;}
            Low=FMath::Min(Low,Mask[Q]);High=FMath::Max(High,Mask[Q]);
            check(FMath::IsFinite(Phi[Q]) && FMath::IsFinite(Mask[Q]));
        }
        check(FlatOpen>0 && FlatRipple==FlatOpen);
        UE_LOG(LogTemp,Display,TEXT("YunshaRippleCoverage open_flat=%d active=%d"),FlatOpen,FlatRipple);
        UE_LOG(LogTemp,Display,TEXT("YunshaRippleSolver grid=%d residual_cm=%.4f exposure=%.3f..%.3f heading=%.2f"),N,Residual,Low,High,FMath::RadiansToDegrees(Angle));
    }
    FVector2f At(double X,double Y) const
    {return FVector2f(Sample(Phi,X,Y)/100.,Sample(Mask,X,Y));}
};
