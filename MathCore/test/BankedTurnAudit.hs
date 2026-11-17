module Main where
import Control.Monad (forM_)
import Text.Printf
import Born2Flap.Math.Types
import Born2Flap.Math.Vehicle
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.Model
import Born2Flap.Math.Airframe
import System.Environment (getArgs)
import WakeReference
import Born2Flap.Math.Planform (wsSpanM, shapeStationsAt, spanChord)
import Born2Flap.Math.Section (zeroLiftAngle)

rot a (Vec3 x y z)=Vec3 x (cos a*y-sin a*z) (sin a*y+cos a*z)
sumV=foldr addVec zeroVec
settle input side angle = go 120 initialStrips where
  go 0 s=s
  go n s=go (n-1) (snd(stepCoupledWing False side angle 0 0 0 input kestrelModel 0 s))

main=do
  args<-getArgs
  let incidence=case args of a:_->read a; _-> -2.3
      exportPath=case args of _:p:_->Just p; _->Nothing
  case exportPath of
    Just p -> writeFile p "plane,common,relative,sink,side,px,py,pz,cx,cy,cz,sx,sy,sz,nx,ny,nz,u,v,w,chord,alpha0,lx,ly,lz\n"
    Nothing -> pure ()
  putStrLn "plane,common,relative,sink,tailInc,tailAlpha,tailYaw,wingYaw,tailLoad,wingLoad,rollAccel,planeYawAccel,flags,wakeAlpha,wakeTailYaw,refWingRoll,modelCirculationRoll,refTailYaw"
  forM_ [0,80] $ \plane -> forM_ [-25,0,25] $ \common -> forM_ [-15,0,15] $ \relative -> forM_ [0,2] $ \sink -> do
    let phi=radians(plane+relative); dt=1/240
        flow=rot (-phi) (Vec3 8 0 (-sink)); up=rot (-phi) (Vec3 0 0 1)
        ql=radians(common+relative); qr=radians(common-relative)
        inp=VehicleInput dt flow zeroVec 0 0 0 0 1000 up
        left=settle inp (-1) ql; right=settle inp 1 qr
        (ix,iy,iz)=airframeInertia kestrelModel 0.05 0
        params=defaultFirmwareParams {fwProfile=(fwProfile defaultFirmwareParams)
             {profGlideAngleDeg= -common,profTailElevatorAngleDeg= incidence,profAileronScale=100}}
        pilot=pilotToRc(PilotInput 0 (-relative/glideAileronMaxDeg) 0 0 0.5 True)
        initial=defaultFirmwareVehicleState {fvServoLeft=ServoState(common+relative) 0,
           fvServoRight=ServoState(common-relative) 0,fvLeftStrips=left,fvRightStrips=right,fvMassFrame=Just(0,iy,iz)}
        (o,next)=stepFirmwareVehicleWithMassInertia 0.05 ix True pilot params defaultServoSpec defaultBatterySpec kestrelModel dt flow zeroVec 1000 up initial
        tail=kestrelTailLoads incidence flow zeroVec
        lsys=prepareWingSystem False (-1) ql 0 inp kestrelModel 0 0 left
        rsys=prepareWingSystem False 1 qr 0 inp kestrelModel 0 0 right
        lr=wingTrialMotion lsys zeroVec zeroVec 0 0 (wingInitialRates lsys)
        rr=wingTrialMotion rsys zeroVec zeroVec 0 0 (wingInitialRates rsys)
        dr=wsSpanM(wmWing kestrelModel)/fromIntegral stripCount
        wakes=[addVec (wakeAt (-1) dr lr (tfPosition f)) (wakeAt 1 dr rr (tfPosition f)) | f<-tail]
        wakeTail=[kestrelTailLoads incidence (addVec flow (scaleVec (-1) w)) zeroVec !! i | (i,w)<-zip [0..] wakes]
        wakeAlpha=sum [incidence+atan2 (-vz(tfVelocity f)) (vx(tfVelocity f))*180/pi | f<-wakeTail]/18
        wakeMoment=planeVector(sumV(map tfMoment wakeTail))
        chords=map spanChord(shapeStationsAt (wmWing kestrelModel) stripCount)
        sections rs=zipWith (\r c -> (r,c,zeroLiftAngle(stripCamber(resultState r)))) rs chords
        ordered=reverse(sections lr)++sections rr
        (refForces,refWake)=liftingLineReference flow ordered
        refWingM=sumV [crossVec p f| ((r,_,_),f)<-zip ordered refForces,let (p,_,_,_,_,_)=resultFrame r]
        modelCircM=sumV [crossVec (stripPosition s)(stripLift s)|r<-lr++rr,let s=resultState r]
        refTail=[kestrelTailLoads incidence (addVec flow(scaleVec (-1)(refWake(tfPosition f)))) zeroVec !! i |(i,f)<-zip [0..] tail]
        refTailM=planeVector(sumV(map tfMoment refTail))
        planeVector=rot (radians relative)
        tailM=planeVector(sumV(map tfMoment tail))
        tailF=planeVector(sumV [sumV [tfLift f,tfPressure f,tfDrag f] | f<-tail])
        wingM=planeVector(sumV(map stripMoment (left++right)))
        wingF=planeVector(sumV [sumV [stripLift f,stripPressure f,stripDrag f] | f<-left++right])
        accel=planeVector(scaleVec (1/dt)(fvSolvedBodyRates next))
        alpha=incidence+atan2 (-vz flow) (vx flow)*180/pi
    case exportPath of
      Just path -> forM_ (zip [-1,1] [lr,rr]) $ \(side,rs) ->
        forM_ (sections rs) $ \(r,c,a0) -> do
          let (p,ch,sp,n,v,_)=resultFrame r
              l=stripLift(resultState r)
              xyz (Vec3 x y z)=[x,y,z]
              values=[plane,common,relative,sink,side]++concatMap xyz [p,ch,sp,n,v]++[c,a0]++xyz l
          appendFile path (foldr1 (\a b -> a++","++b) (map show values)++"\n")
      Nothing -> pure ()
    printf "%.0f,%.0f,%.0f,%.0f,%.2f,%.4f,%.8f,%.8f,%.8f,%.8f,%.6f,%.6f,%d,%.4f,%.8f,%.8f,%.8f,%.8f\n"
      (plane::Double) common relative sink incidence alpha (vz tailM) (vz wingM) (vz tailF) (vz wingF) (vx accel) (vz accel) (outputFlags o) wakeAlpha (vz wakeMoment) (vx refWingM) (vx modelCircM) (vz refTailM)