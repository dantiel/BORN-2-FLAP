module Main where
import Control.Monad (unless)
import System.Environment (getArgs)
import Text.Printf (printf)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model
import Born2Flap.Math.Firmware
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.Servo
import Born2Flap.Math.Structure (SparInventory(..))

sumV=foldr addVec zeroVec
partQ side get states=sum [side*(vx(crossVec(stripPosition s)(get s))+wingRootHeight*vy(get s))|s<-states]

-- Prescribed body motion (wind-tunnel constraint), real mixer and elastic
-- wings. The effectively infinite body roll inertia prevents mount recoil.
-- No synthetic input-angle trajectory is imposed on the wings.
main :: IO ()
main = do
  args<-getArgs
  let u=case args of x:_->read x; _->8
      throttle=case args of _:x:_->read x; _->0.8
      yaw=case args of _:_:x:_->read x; _->0
      seconds=case args of _:_:_:x:_->read x; _->5
      twistScale=case args of _:_:_:_:x:_->read x; _->8
      model=kestrelModel {wmSpars=(wmSpars kestrelModel) {siTwistScale=twistScale}}
      dt=1/480
      servo=servoFromNominal "captured servo" 857.142883 0.74000001 20
      battery=defaultBatterySpec {batteryNominalVoltage=8.4,batteryInternalResistanceOhm=0.08,
                  batteryCapacityAh=1.3,batteryCRating=30,batteryBurstCRating=60}
      profile=(fwProfile defaultFirmwareParams) {profStrokeFerocity=68,profReturnFerocity=67,
        profGlideAngleDeg= -4,profTailElevatorAngleDeg= -10,profAileronScale=100,profElevatorScale=100,
        profRudderAmplitudeDiff=100,profStrokeSkew=0,profReturnSkew=0,profFerocityShapeMix=42,
        profThrottleThrustShapeMix=29,profThrottleThrustExpo=100,profAileronSkewMix=100,
        profThrottleSkewRateMix=100,profAileronSkewRateMix=100,profElevatorFerocityMix=100}
      params=defaultFirmwareParams {fwServoSpeedMs=60000/857.142883,fwFlapBaseFreqDh=32,
                   fwFlapCentreDeg= -7,fwProfile=profile}
      rc=pilotToRc(PilotInput throttle 0 0 yaw 1 True)
      advance :: Int -> FirmwareVehicleState -> IO ()
      advance i state
        | fromIntegral i*dt>=seconds=pure ()
        | otherwise=do
          let (mix,_)=computeServoMixer True rc params (fvFirmware state) dt
              (o,next)=stepFirmwareVehicleWithMassInertia 0.05 1e6 True rc params servo battery model dt
                           (Vec3 u 0 (-0.5)) zeroVec 1000 (Vec3 0 0 1) state
              (motorL,motorR)=fvDriveTorques next
              row :: Double -> Double -> Double -> ServoState -> [StripState] -> IO ()
              row side target motor shaft states =
                let lift=sumV(map stripLift states);pressure=sumV(map stripPressure states);drag=sumV(map stripDrag states)
                    rate=servoRateDegPerSec shaft*pi/180
                    ql=partQ side stripLift states;qp=partQ side stripPressure states;qd=partQ side stripDrag states
                    q=sum[side*(vx(stripMoment s)+wingRootHeight*vy(addVec(stripLift s)(addVec(stripPressure s)(stripDrag s))))|s<-states]
                    force=sumV[lift,pressure,drag]
                    rateErr=target-servoAngleDeg shaft
                in printf "%.9f,%.0f,%.6f,%.6f,%.6f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.6f,%.6f,%.9f,%.6f,%.6f,%.6f\n"
                     (fromIntegral i*dt) side target (servoAngleDeg shaft) rate
                     (vz lift) (vz pressure) (vz drag) (vx force) ql qp qd q motor (motor*rate)
                     (stripTwistAero(last states)*180/pi) (fvFlapHz next) rateErr
                     (sum(map stripPreviousAlpha states)*180/pi/fromIntegral(length states))
                     (stripPreviousAlpha(last states)*180/pi)
                     (sum(map stripSeparation states)/fromIntegral(length states))
          unless (outputFlags o==0) $ fail ("solver failure at "++show i++": "++show(outputFlags o))
          row (-1) (mixLeftFlapDevDeg mix) motorL (fvServoLeft next) (fvLeftStrips next)
          row 1 (mixRightFlapDevDeg mix) motorR (fvServoRight next) (fvRightStrips next)
          advance (i+1) next
  putStrLn "t,side,target_deg,angle_deg,rate_rad_s,lift_z,pressure_z,drag_z,force_x,Q_lift,Q_pressure,Q_drag,Q_total,motor_Nm,motor_power_W,tip_twist_deg,hz,position_error_deg,mean_alpha_deg,tip_alpha_deg,mean_separation"
  advance (0::Int) defaultFirmwareVehicleState
