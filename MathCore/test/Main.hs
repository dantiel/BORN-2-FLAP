module Main (main) where

import Born2Flap.Math.Types
import Born2Flap.Math.Wing
import Born2Flap.Math.Control
import Born2Flap.Math.Vehicle
import Born2Flap.Math.LiftField
import Born2Flap.Math.Planform
import Born2Flap.Math.Airframe
import Born2Flap.Math.Section
import Born2Flap.Math.Structure
import Born2Flap.Math.Model
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform
  ( OscillatorState(..), defaultOscillator, advanceOscillator
  , limiarFromFerocities, shapeWave, shapeWaveWithDerivative, wavePeakDerivativeBound )
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.RollDynamics
import qualified Born2Flap.Math.SpatialDynamics as Spatial
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.FerocityPrice
import Born2Flap.Math.PhaseEnvelope
import Born2Flap.Math.ResponsePhase

main :: IO ()
main = do
  -- Spatial added inertia must be positive, symmetric, and carry no weight.
  -- Dry free fall must not actuate either hinge, at any wing position or CG.
  let positive [] = True
      positive matrix =
        let pivot=head(head matrix)
            reduced=zipWith (\row arm -> zipWith (\a b -> a-arm*b/pivot) (tail row) (tail(head matrix)))
                      (tail matrix) (map head (tail matrix))
        in pivot>1e-10 && positive reduced
      spatialCase cg l r =
        let tensor=airframeInertia kestrelModel 0.05 cg
            run rho=Spatial.spatialSystem kestrelModel 0.05 cg tensor rho (l,r) (replicate 8 0) (Vec3 0 0 (-9.81))
            (dry,_,weight)=run 0
            (wet,_,wetWeight)=run airDensity
            falling=Spatial.multiply dry [0,0,-9.81,0,0,0,0,0]
        in positive wet && positive dry
           && and [abs(wet!!i!!j-wet!!j!!i)<1e-12 | i<-[0..7],j<-[0..7]]
           && maximum(map abs(zipWith (-) weight falling))<1e-12
           && maximum(map abs(zipWith (-) weight wetWeight))<1e-12
      (flatMass,_,_)=Spatial.spatialSystem kestrelModel 0.05 0
        (airframeInertia kestrelModel 0.05 0) airDensity (0,0) (replicate 8 0) zeroVec
      (_,dryPitch,_)=airframeInertia kestrelModel 0.05 0
  if and [spatialCase cg l r | cg<-[-0.1,0,0.1],l<-[-1,0,1],r<-[-1,0,1]]
     && abs(flatMass!!0!!0-0.05)<1e-12 && flatMass!!2!!2>0.05 && flatMass!!4!!4>dryPitch
    then pure () else fail "spatial wing inertia must preserve positive energy, dry weight and pitch added mass"
  -- A pressure patch may return stored energy, but never create it. Check
  -- changing surface direction, reversal, and relaxation at several steps.
  let fieldCase dt normal velocity =
        let old = PressureField (Vec3 0.01 (-0.02) 0.03) (Vec3 0 0 1) 1
            stiffness = 7
            (force, next) = advancePressureField dt 0.75 stiffness normal velocity old
            energy s = 0.5*stiffness*dotVector (fieldDisplacement s) (fieldDisplacement s)
        in dt*dotVector force velocity + energy next-energy old <= 1e-12
  if and [fieldCase dt (unitVector (Vec3 0 a 1)) (Vec3 3 b c)
         | dt<-[1/240,1/960,1/3840],a<-[-1,0,1],b<-[-2,0,2],c<-[-5,0,5]]
    then pure () else fail "air-patch work plus stored-energy change must be non-positive"
  let v = Vec3 0.2 (-0.4) 0.7
      transported = transportVector 0.3 (Vec3 1 2 (-3)) v
      restored = transportVector (-0.3) (Vec3 1 2 (-3)) transported
  if abs (magnitude v-magnitude transported)<1e-12 && magnitude (addVec restored (scaleVec (-1) v))<1e-12
    then pure () else fail "body-frame transport must preserve air-field direction and energy"
  -- Structural ringing stores real kinetic/elastic energy, not waveform phase.
  let energy (q,v)=0.5*0.02*v*v+0.5*8*q*q
      ring damping = take 1000 (iterate (\(q,v) -> stepElasticMode (1/240) 0.02 8 damping 0 q v) (0.01,0))
      undamped=map energy (ring 0)
      damped=map energy (ring 0.08)
  if all (\e -> abs(e-head undamped)<1e-12) undamped &&
     and (zipWith (>=) damped (drop 1 damped)) && last damped < head damped*0.001
    then pure () else fail "structural mode must conserve free energy or dissipate with damping"
  let response frequency =
        let tick (q,v,peak) i =
              let force=0.01*sin(2*pi*frequency*fromIntegral i/240)
                  (q1,v1)=stepElasticMode (1/240) 0.02 8 0.04 force q v
              in (q1,v1,if i>1200 then max peak (abs q1) else peak)
            (_,_,peak)=foldl tick (0,0,0) [1..2400::Int]
        in peak
      naturalHz=sqrt(8/0.02)/(2*pi)
  if response naturalHz > 3*max (response (naturalHz/2)) (response (naturalHz*2))
    then pure () else fail "elastic resonance must follow sqrt(stiffness/inertia), not flap phase"
  let coupledEnergy ((b,t),(v,w))=0.5*(0.02*v*v-0.002*v*w+0.001*w*w+8*b*b+0.2*t*t)
      coupledRing=take 1000 (iterate (\(q,v) -> stepCoupledElasticModes (1/240)
                   (0.02,-0.001,0.001) (8,0.2) (0,0) (0,0) q v) ((0.01,0.1),(0,0)))
  if all (\s -> abs(coupledEnergy s-coupledEnergy(head coupledRing))<1e-12) coupledRing
    then pure () else fail "coupled bend/twist inertia must conserve free modal energy"
  let tailPassive (u,w,a)=let (fx,fz)=tailSurface 0.0242 a u w in fx*u+fz*w<=1e-10
      (_,broadsideTail)=tailSurface 0.0242 0 0 3
      expectedPressure=surfacePressure 0.0242 3-0.5*airDensity*0.0242*surfaceSkinCd*9
  if abs (broadsideTail-expectedPressure)<1e-12 &&
     all tailPassive [(u,w,a) | u<-[-8,0,8],w<-[-5,0,5],a<-[-0.5,0,0.5]]
    then pure () else fail "tail must share passive wing pressure and skin constants"
  -- The real fixed fan sees the full local relative flow at every patch.
  let tailForce f=addVec (tfLift f) (addVec (tfPressure f) (tfDrag f))
      tailCase incidence velocity rates = all (\f ->
        abs(dotVector (tfLift f) (tfVelocity f))<1e-10 &&
        dotVector (tfDrag f) (tfVelocity f)<=1e-10 &&
        dotVector (tfPressure f) (tfVelocity f)<=1e-10 &&
        magnitude(addVec (tfVelocity f) (scaleVec (-1) (addVec velocity (crossVec rates (tfPosition f)))))<1e-12 &&
        magnitude(addVec (tfMoment f) (scaleVec (-1) (crossVec (tfPosition f) (tailForce f))))<1e-12)
        (kestrelTailLoads incidence velocity rates)
      fanLoad incidence=foldr (addVec . tailForce) zeroVec (kestrelTailLoads incidence (Vec3 8 0 0) zeroVec)
      fanMoment incidence=foldr (addVec . tfMoment) zeroVec (kestrelTailLoads incidence (Vec3 8 0 0) zeroVec)
      rotateX a (Vec3 x y z)=Vec3 x (cos a*y-sin a*z) (sin a*y+cos a*z)
      banked a=let force=rotateX a (fanLoad (-2.3)); moment=rotateX a (fanMoment (-2.3))
               in a*vy force>0 && a*vz moment<0
      rollDamping p=sum(map (vx.tfMoment) (kestrelTailLoads (-2.3) (Vec3 8 0 0) (Vec3 p 0 0)))
                    -vx(fanMoment (-2.3))
  if and [tailCase a (Vec3 u v w) (Vec3 p 0 r) | a<-[-15,-2.3,0,10],u<-[-8,0,8],v<-[-2,2],w<-[-3,0,3],p<-[-1,1],r<-[-1,1]]
     && vz(fanLoad (-2.3))<0 && vy(fanMoment (-2.3))<0
     && vz(fanLoad 2.3)>0 && vy(fanMoment 2.3)>0
     && all banked [-0.6,0.6] && all (\p -> p*rollDamping p<0) [-1,1]
    then pure () else fail "fixed fan must rotate negative lift into yaw, damp roll, and use passive local airflow"
  let mixTail angle throttle =
        let params=defaultFirmwareParams {fwProfile=(fwProfile defaultFirmwareParams) {profTailElevatorAngleDeg=angle}}
        in fst(computeServoMixer True (pilotToRc (defaultPilotInput {piThrottle=throttle})) params defaultFirmwareState (1/240))
  if and [mixTail (-10) throttle==mixTail 10 throttle | throttle<-[0,0.72]]
    then pure () else fail "tail incidence setting must not move wing stroke targets"
  let (bodyMass,wingMass,tailMass)=airframeMasses 0.05
      (ix,iy,iz)=airframeInertia kestrelModel 0.05 0
      wingDry=shapeFlapInertia wingMass 0 defaultBirdWing
  if abs (bodyMass-0.038)<1e-12 && wingMass==0.005 && tailMass==0.002 &&
     abs (bodyMass+2*wingMass+tailMass-0.05)<1e-12 &&
     ix>2*wingDry && ix<2*wingDry*1.15 && iy>0 && iz>0
    then pure () else fail "component mass budget and narrow-body roll inertia"
  -- Camber -> lift -> camber must have a stable attached-flow equilibrium.
  let camberLoop profile = 2*finiteWingLiftSlope (shapeAspectRatio defaultBirdWing)*spMembraneK profile
  if all ((<0.5) . camberLoop) [defaultBirdArmSection,defaultBirdHandSection]
    then pure () else fail "reference membrane compliance must stay below static aeroelastic divergence"
  let flow=VehicleInput (1/240) (Vec3 8 0 (-1)) (Vec3 0 0 0) 0 0 0 0 5 (Vec3 0 0 1)
      loads rate=wingLoadsWithStroke True 1 0.1 rate 0 flow kestrelModel 0 initialStrips (replicate stripCount 0) 0 0
      summed=foldr (addVec . resultForce) zeroVec
      still=summed (loads 0)
      beating=summed (loads (-3))
  -- Stroke motion tilts the aerodynamic resultant; no independent jet.
  if vx beating > vx still
    then pure () else fail "flap thrust must emerge from stroke motion, not a flag"
  -- Isolate yaw's local-airflow effect from later sideslip, elastic history and
  -- tail reactions. The outside wing must gain speed and cycle-mean lift, with
  -- a rolling moment that raises it. Include bent/twisted surface geometry.
  let yawLoads speed yaw bend stroke rate side =
        let input=flow {bodyVelocityMS=Vec3 speed 0 (-0.5),bodyRatesRadS=Vec3 0 0 yaw,groundClearanceM=1000}
            states=map (\s -> s {stripBendM=bend,stripBendSlope=2*bend,stripTwistAero=2*bend}) initialStrips
        in wingLoadsWithStroke True side stroke rate rate input kestrelModel 0.08 states (replicate stripCount 0) 0 0
      yawCase speed yaw bend phases =
        let pairs=[(yawLoads speed yaw bend a v (-1),yawLoads speed yaw bend a v 1) | (a,v)<-phases]
            liftDifference=sum [sum(map (vz.stripLift.resultState) ls)-sum(map (vz.stripLift.resultState) rs) | (ls,rs)<-pairs]
            liftRoll=sum [sum [vx(crossVec (stripPosition s) (stripLift s)) | r<-ls++rs,let s=resultState r] | (ls,rs)<-pairs]
            velocityError l r=let a=resultState l; b=resultState r in
              abs (vx(stripAirVelocity a)-vx(stripAirVelocity b)-yaw*(vy(stripPosition b)-vy(stripPosition a)))
        in yaw*liftDifference>1e-5 && yaw*liftRoll<(-1e-5)
           && and [velocityError l r<1e-10 | (ls,rs)<-pairs,(l,r)<-zip ls rs]
      cyclePhases=[(0.5*sin a,5*cos a) | i<-[0..31::Int],let a=2*pi*fromIntegral i/32]
  if and [yawCase speed yaw bend phases | speed<-[5,8,12],yaw<-[-0.6,0.6],bend<-[0,0.025],phases<-[[(0,0)],cyclePhases]]
    then pure () else fail "yaw must increase advancing-strip speed/lift and roll that wing upwards in glide and flap"
  -- Coupled inertia: internal motor impulses conserve angular momentum;
  -- kinetic work converges to the continuous constant-torque solution.
  let j=0.002; rotor=0.0016; locked=0.008; dt=1/240
      zero=Vec3 0 0 0
      drive _=Vec3 0 (-1) 1
      solve h old angles loads = case stepRoll h locked j rotor old angles loads of
        Just v -> v
        Nothing -> error "coupled roll solve failed"
      moved=solve dt zero (0,0) drive
      a=1/(j+rotor-2*j*j/locked)
  if abs (rollMomentum locked j moved) < 1e-9
     && abs (y moved + dt*a) < 1e-6 && abs (z moved - dt*a) < 1e-6
    then pure () else fail "free-body motor acceleration must match coupled mass matrix"
  -- The body diagonal includes locked wing inertia even with free hinges.
  let kinetic (Vec3 p l r) = 0.5*((locked-2*j)*p*p + j*((l-p)^2+(r+p)^2) + rotor*(l*l+r*r))
  if all (\v -> abs(rollEnergy locked j rotor v-kinetic v)<1e-12)
       [Vec3 p l r | p<-[-3,0,2], l<-[-2,0,4], r<-[-1,0,3]]
    then pure () else fail "roll mass matrix must equal physical component kinetic energy"
  -- A slowly moving wing retains the load of forward airflow.
  let slow=summed (loads 1e-6)
  if magnitude still > 0.01 && magnitude (addVec slow (scaleVec (-1) still)) < 1e-5
    then pure () else fail "slow hinge motion must retain steady forward-flow loading"
  -- Loaded support is a conservative mass coupling, not a lift-dependent drag.
  let total=0.05; mr=0.0012; root=0.04
      (reduced,support,recoil,_)=supportedRollSystem total 0 locked 0.0004 j rotor mr 0 root (0,0) zero
      supported=support (Vec3 0 0 (total*9.81))
      free=support zero
      recoilUp=recoil (Vec3 0 1 1)
      full=supportMassMatrix total 0 locked 0.0004 j rotor mr 0 root (0,0,0)
      rv=[0.4,-0.3,0.7]
      dotList a b=sum(zipWith (*) a b)
      translation i= -dotList (drop 2 (full!!i)) rv / total
      allV=[translation 0,translation 1]++rv
      fullEnergy=0.5*dotList allV (map (`dotList` allV) full)
      (r0,r1,r2)=reduced
      values (Vec3 a b c)=[a,b,c]
      reducedEnergy=0.5*dotList rv (map ((`dotList` rv) . values) [r0,r1,r2])
  if magnitude free<1e-12 && abs(y supported+mr*9.81)<1e-12
     && abs(z supported+mr*9.81)<1e-12 && abs(z recoilUp+2*mr)<1e-12
     && abs(fullEnergy-reducedEnergy)<1e-12 && fullEnergy>0
    then pure () else fail "supported mass reduction must preserve energy, free fall and linear recoil"
  -- An unforced moving linkage must not create first-order kinetic energy.
  let hstep=1e-5; angles=(0.2,-0.1); initial=Vec3 0.4 (-0.3) 0.7
      (mm,cc,rr,_)=supportedRollSystem total 0 locked 0.0004 j rotor mr 0 root angles initial
      next=case stepRollWithMass mm hstep initial angles (const (cc zero)) of
             Just v -> v
             Nothing -> error "support conservation solve failed"
      accel=scaleVec (1/hstep) (addVec next (scaleVec (-1) initial))
      linear=scaleVec (hstep/total) (rr accel)
      energyAt q vel=let mat=supportMassMatrix total 0 locked 0.0004 j rotor mr 0 root q
                     in 0.5*dotList vel (map (`dotList` vel) mat)
      e0=energyAt (0,fst angles,snd angles) ([0,0]++values initial)
      e1=energyAt (hstep*x next,fst angles+hstep*y next,snd angles+hstep*z next)
                    ([y linear,z linear]++values next)
  if abs(e1-e0)<1e-9 then pure ()
    else fail "moving support linkage must conserve kinetic energy to integration order"
  -- Translational added air is a conservative fluid-inertia coupling. With
  -- added mass and first moment present, the Schur reduction and recoil must
  -- still conserve kinetic energy and transmit the weight through the joints.
  let ma=0.02; mar=0.004; mt=total+2*ma
      (ared,asup,arec,arat)=supportedRollSystem total ma locked 0.0004 j rotor mr mar root (0,0) zero
      asupported=asup (Vec3 0 0 (total*9.81))
      afull=supportMassMatrix total ma locked 0.0004 j rotor mr mar root (0,0,0)
      atranslation i= -dotList (drop 2 (afull!!i)) rv / mt
      aallV=[atranslation 0,atranslation 1]++rv
      afullEnergy=0.5*dotList aallV (map (`dotList` aallV) afull)
      (ar0,ar1,ar2)=ared
      areducedEnergy=0.5*dotList rv (map ((`dotList` rv) . values) [ar0,ar1,ar2])
      arecoilUp=arec (Vec3 0 1 1)
  if abs(y asupported+(mr+mar)*9.81*total/mt)<1e-10
     && abs(z asupported+(mr+mar)*9.81*total/mt)<1e-10
     && abs(z arecoilUp+2*(mr+mar)*total/mt)<1e-10
     && abs(afullEnergy-areducedEnergy)<1e-10 && afullEnergy>0
     && abs(arat-total/mt)<1e-12
    then pure () else fail "translational added air must conserve energy, transmit weight and scale recoil"
  -- Added fluid resists acceleration but contributes no aircraft weight.
  -- A locked aircraft with lift exactly balancing dry weight must have zero
  -- acceleration at every bank and flap angle, including anhedral/inverted.
  -- With no support it falls at m*g/(m+2*ma) in the modeled lateral/heave axes.
  let gravityCase air angles gravity =
        let (l,r)=angles
            (joint,host)=supportGravity total air mr mar angles gravity
            ratio=total/(total+2*air)
            g=Vec3 0 (y gravity) (z gravity)
            lift=scaleVec (-total) g
            acceleration=addVec g (scaleVec (1/total) (addVec (scaleVec ratio lift) host))
            fall=addVec g (scaleVec (1/total) host)
            dry=Vec3 (mr*(-(sin l+sin r)*y g+(cos r-cos l)*z g))
                     (mr*(sin l*y g+cos l*z g)) (mr*(-sin r*y g+cos r*z g))
            full=scaleVec ((mr+mar)/mr) dry
            expected=addVec dry (scaleVec (-ratio) full)
        in magnitude acceleration<1e-12
           && magnitude(addVec fall (scaleVec (-ratio) g))<1e-12
           && magnitude(addVec joint (scaleVec (-1) expected))<1e-12
      vacuum angles gravity =
        let (joint,host)=supportGravity total 0 mr 0 angles gravity
        in magnitude joint+magnitude host<1e-12
      cases=[((l,r),g) | l<-[-0.8,0,0.8],r<-[-0.8,0,0.8],
                        g<-[Vec3 0 0 (-9.81),Vec3 0 9.81 0,Vec3 0 0 9.81,zero]]
  if and [gravityCase air angles g | air<-[0.01,0.02,0.04],(angles,g)<-cases]
     && all (uncurry vacuum) cases
    then pure () else fail "added air introduced phantom weight or gravity torque in vacuum"
  let symmetric=solve dt zero (0,0) (const (Vec3 0 1 1))
      damp v=Vec3 (-0.2*x v) (-0.1*y v) (-0.1*z v)
      moving=Vec3 2 (-3) 4
      damped=solve dt moving (0,0) damp
      stopped=solve dt moving (-80*pi/180,80*pi/180) (const zero)
  if abs (x symmetric) < 1e-9
     && rollEnergy locked j rotor damped < rollEnergy locked j rotor moving
     && abs (rollMomentum locked j stopped-rollMomentum locked j moving) < 1e-8
     && y stopped >= -1e-8 && z stopped <= 1e-8
     && rollEnergy locked j rotor stopped <= rollEnergy locked j rotor moving + 1e-9
    then pure () else fail "symmetry, passive dissipation and internal end-stop momentum must hold"
  if servoDriveTorque defaultServoSpec defaultBatterySpec 0 dt 40 0 j defaultServoState 1 == 0
    then pure () else fail "empty battery must supply no motor torque"
  -- If roll and hinge motion cancel in world space, the wing sees the same
  -- air velocity (including compensation for translation of the high root).
  let wingInput=VehicleInput dt (Vec3 8 0 0) zero 0 0 0 0 1000 (Vec3 0 0 1)
      sumForce=foldr (addVec . resultForce) zero
      steady=sumForce (wingLoadsWithStroke False 1 0 0 0 wingInput kestrelModel 0 initialStrips (replicate stripCount 0) 0 0)
      counter=sumForce (wingLoadsWithStroke False 1 0 (-1) 0
                 (wingInput {bodyRatesRadS=Vec3 1 0 0,bodyVelocityMS=Vec3 8 wingRootHeight 0}) kestrelModel 0 initialStrips (replicate stripCount 0) 0 0)
      falling=wingInput {bodyVelocityMS=Vec3 8 0 (-1)}
      before=sumForce (wingLoadsWithStroke True 1 0 (-1e-8) (-0.1) falling kestrelModel 0 initialStrips (replicate stripCount 0) 0 0)
      after=sumForce (wingLoadsWithStroke True 1 0 1e-8 (-0.1) falling kestrelModel 0 initialStrips (replicate stripCount 0) 0 0)
  if magnitude (addVec steady (scaleVec (-1) counter)) < 1e-8
     && magnitude (addVec before (scaleVec (-1) after)) < 1e-6
    then pure () else fail "world-stationary wing flow and continuous implicit reversal loads must hold"
  let loadedStrip = emptyStrip { stripSpringLoad = 1 }
      strip moment = StripResult loadedStrip (Vec3 0 0 1) moment 0 0 0 (zeroVec,zeroVec,zeroVec,zeroVec,zeroVec,zeroVec)
      leftTorque = hingeTorque (-1) [strip (Vec3 (-2) 90 0)]
      rightTorque = hingeTorque 1 [strip (Vec3 2 90 0)]
      wingI = shapeFlapInertia referenceWingMassKg airDensity defaultBirdWing
  if leftTorque == 2 && rightTorque == 2 then pure ()
    else fail "mirrored flap hinges must project signed X torque, ignoring pitch torque"
  -- Internal compliance cannot erase the external moment at the hinge.
  let unloadedStrip = emptyStrip { stripSpringLoad = 0 }
      easedTorque = hingeTorque (-1) [StripResult unloadedStrip (Vec3 0 0 1) (Vec3 (-2) 90 0) 0 0 0 (zeroVec,zeroVec,zeroVec,zeroVec,zeroVec,zeroVec)]
      twistedStrip = emptyStrip { stripSpringLoad = 0, stripTwistAero = -0.2 }
      twistedTorque = hingeTorque (-1) [StripResult twistedStrip (Vec3 0 0 1) (Vec3 (-2) 90 0) 0 0 0 (zeroVec,zeroVec,zeroVec,zeroVec,zeroVec,zeroVec)]
  if easedTorque == 2 && twistedTorque == 2 then pure ()
    else fail "elastic geometry must not discount an unchanged external hinge moment"
  let fwStep dt vel s = stepFirmwareVehicle True defaultRcChannels defaultFirmwareParams
                        defaultServoSpec defaultBatterySpec kestrelModel dt vel (Vec3 0 0 0) 1000 s
      badCases = [(0, Vec3 5 0 0), (0/0, Vec3 5 0 0), (0.01, Vec3 (1/0) 0 0)]
  mapM_ (\(dt, vel) -> let (o,s) = fwStep dt vel defaultFirmwareVehicleState
                       in if outputFlags o /= 0 && s == defaultFirmwareVehicleState
                          then pure () else fail "firmware failure must preserve all state") badCases
  let flap = defaultRcChannels {rcThrottle = 1811}
      batteryStep battery s = stepFirmwareVehicle True flap defaultFirmwareParams defaultServoSpec
                               battery kestrelModel (1/240) (Vec3 5 0 0) (Vec3 0 0 0) 1000 s
      (_, full) = batteryStep defaultBatterySpec defaultFirmwareVehicleState
      (_, empty) = batteryStep defaultBatterySpec (defaultFirmwareVehicleState {fvBatterySoc = 0})
      loaded = defaultFirmwareVehicleState {fvLeftHingeTorqueNm = 0.5, fvRightHingeTorqueNm = 0.5, fvMotorCurrentA = 10}
      stepLo = batteryStep (defaultBatterySpec {batteryInternalResistanceOhm = 0})
      stepHi = batteryStep (defaultBatterySpec {batteryInternalResistanceOhm = 0.8})
      -- Steady aero hinge load: integrate a few steps so the voltage-sag
      -- tracking difference accumulates beyond a single-step numerical jitter.
      lowR = iterate (snd . stepLo) loaded !! 8
      highR = iterate (snd . stepHi) loaded !! 8
      (mixCmd, _) = computeServoMixer True flap defaultFirmwareParams defaultFirmwareState (1 / 240)
      flapCmd = mixLeftFlapDevDeg mixCmd
  -- An unpowered wing can now be moved by air; it must not be frozen. Voltage
  -- sag must reduce the servo's POWERED motion: the sagged servo ends further
  -- from its commanded flap (larger tracking error) than the full-voltage one.
  -- Track the command directly rather than |rate| — under a strong aero hinge
  -- load the sagged servo is blown BACK off the command (correct, unpowered
  -- motion), which inflates |rate| even though its powered motion shrank.
  if abs (servoAngleDeg (fvServoLeft empty)) < abs (servoAngleDeg (fvServoLeft full))
     && abs (flapCmd - servoAngleDeg (fvServoLeft highR)) > abs (flapCmd - servoAngleDeg (fvServoLeft lowR))
    then pure () else fail "empty battery and voltage sag must reduce powered motion"
  let rollback = runSimulation (putState (99 :: Int) >> abort "failure") 7
                   :: Either String ((), Int)
  if rollback == Left "failure" then pure () else fail "transaction must not expose failed state"
  let MetresPerSecond positive = crossflowBaseline 0.3 (Radians 0.4) (MetresPerSecond 8)
      MetresPerSecond negative = crossflowBaseline 0.3 (Radians (-0.4)) (MetresPerSecond 8)
  if positive > 0 && negative < 0 && abs (positive + negative) < 1.0e-12
    then pure ()
    else fail "crossflow must reverse with signed sweep"
  let initialController = AxisController
        { proportionalGain = 1.0
        , integralGain = 0.2
        , derivativeGain = 0.01
        , integralLimit = 0.5
        , integralState = 0.0
        , previousError = 0.0
        }
      result = stepAxis (Seconds 0.01) (AxisInput 1.0 0.0) initialController
  if command result > 0 && integralState (controller result) <= 0.5
    then pure ()
    else fail "controller must respond positively and respect its integral bound"
  let wing = defaultBirdWing
      wpArea = shapeArea wing
      chords = map (shapeChord wing) [0, 0.25, 0.5, 0.75, 1.0]
      wpAR = shapeAspectRatio wing
      stations = wsStations wing
      rootChord = if null stations then 0 else ssChordM (head stations)
      tipChord = if null stations then 0 else ssChordM (last stations)
      strips = shapeStationsAt wing 16
  if wpArea > 0 && wpAR > 3 && wpAR < 12 && all (> 0) chords
     && shapeChord wing 0 == rootChord
     && shapeChord wing 1 == tipChord
     && shapeTwistRad wing 1 < shapeTwistRad wing 0
     && shapeSweepRad wing 0 > 0 && shapeSweepRad wing 1 > shapeSweepRad wing 0
     && length strips == 16 && all ((> 0) . spanChord) strips
    then pure ()
    else fail "default wing must be a proper tapered, washed-out, back-swept shape"
  let camber = 0.05
      plainMoment = sectionPitchMomentCoeff camber 0.0
      reflexedMoment = sectionPitchMomentCoeff camber 0.4
      restCamber = spRestCamber defaultBirdHandSection
      cupped = relaxCamber 0.01 0.05 restCamber
                 (membraneCamberTarget defaultBirdHandSection 1.0)
      twistLoaded = sectionTwistMomentCoeff camber 0.4 0.10 0.8
      twistReversed = sectionTwistMomentCoeff camber 0.4 0.10 (-0.8)
      leverInboard = sectionTwistLever 0.0
      leverOutboard = sectionTwistLever 1.0
  if zeroLiftAngle camber < 0
     && abs (sectionPitchMomentCoeff 0 0.9) < 1.0e-12
     && plainMoment < reflexedMoment && reflexedMoment < 0
     && cupped > restCamber && cupped < membraneCamberTarget defaultBirdHandSection 1.0
     && twistLoaded < reflexedMoment
     && twistReversed > 0 && twistReversed > twistLoaded
     && leverInboard < leverOutboard && leverInboard > 0 && leverOutboard < 0.25
    then pure ()
    else fail "section model must couple camber to lift, reflex to trim, membrane to load"
  let structDr = 0.05
      structForces = replicate 8 1.0
      structEIs = replicate 8 2.0
      (bendDefl, bendSlope) = integrateFlapBeam structForces structEIs structDr
      structMoms = replicate 8 (-0.1)
      structGJs = replicate 8 1.0
      twistDefl = integrateTwist structMoms structGJs structDr
  if last bendDefl > 0 && last bendSlope > 0
     && last twistDefl < 0 && and (zipWith (<=) bendDefl (tail bendDefl))
    then pure ()
    else fail "cantilever beam must bend tip-up under up-load and twist nose-down under nose-down moment"
  let input = VehicleInput (1 / 240) (Vec3 5 0 0) (Vec3 0 0 0) 0.7 0 0 0 1000 (Vec3 0 0 1)
      samples = take 1200 (tail (iterate (\(_, state) -> stepVehicle input state)
                                   (zeroVehicleOutput, defaultVehicle)))
      outputs = map fst samples
      finite value = not (isNaN value || isInfinite value)
      allFinite output = all finite
        [ x (totalForceN output), y (totalForceN output), z (totalForceN output)
        , x (totalMomentNm output), y (totalMomentNm output), z (totalMomentNm output)
        , totalMechanicalPowerW output, maxSeparation output
        ]
  if all allFinite outputs && all (\output -> maxSeparation output >= 0 && maxSeparation output <= 1) outputs
    then pure ()
    else fail "vehicle loads must remain finite and separation bounded"
  let (_, warmed) = last samples
      changedThrottle = input { throttleCommand = 0.1 }
      (_, changedState) = stepVehicle changedThrottle warmed
      phaseWarmed = oscPhase (vehicleOscillator warmed)
      phaseChanged = oscPhase (vehicleOscillator changedState)
      expectedPhase = phaseWarmed + stepSeconds input * 2 * pi * (1.2 + 3.8 * 0.1)
      phaseError = atan2 (sin (phaseChanged - expectedPhase))
                         (cos (phaseChanged - expectedPhase))
      invalidInputs =
        [ input { stepSeconds = 0 }, input { stepSeconds = 0.06 }
        , input { throttleCommand = 0 / 0 }, input { bodyRatesRadS = Vec3 (1 / 0) 0 0 }
        , input { bodyVelocityMS = Vec3 0 (0 / 0) 0 }
        ]
  if abs phaseError < 1e-12 && abs phaseChanged <= pi
    then pure () else fail "throttle change must integrate a bounded continuous phase"
  mapM_ (\bad -> let (rejected, unchanged) = stepVehicle bad warmed
                 in if outputFlags rejected /= 0 && unchanged == warmed
                    then pure () else fail "invalid input must roll back all state") invalidInputs
  let stationary = input { bodyVelocityMS = Vec3 0 0 0, pitchCommand = 0, yawCommand = 0 }
      (neutral, _) = stepVehicle stationary defaultVehicle
      (deflected, _) = stepVehicle (stationary { pitchCommand = 1, yawCommand = 1 }) defaultVehicle
  if totalForceN neutral == totalForceN deflected && totalMomentNm neutral == totalMomentNm deflected
    then pure () else fail "stationary tail must not generate control forces without flow"
  if all (\o -> abs (y (totalForceN o)) < 1e-10 && abs (x (totalMomentNm o)) < 1e-10
                && abs (z (totalMomentNm o)) < 1e-10) outputs
    then pure () else fail "symmetric wings must cancel lateral loads and roll/yaw moments"
  let rollInput = input { rollCommand = 0.8 }
      rollOutputs = map fst (take 240 (drop 1 (iterate (\(_, s) -> stepVehicle rollInput s)
                                        (zeroVehicleOutput, defaultVehicle))))
  if any ((> 1.0e-6) . abs . x . totalMomentNm) rollOutputs
    then pure () else fail "differential flapping must create a roll moment"
  -- Waveform fidelity (port of PteronautOS FlappingOscillator::shapeWave).
  let f = 4.0
      halfWaveSymmetry theta =
        abs (shapeWave theta f f (-1) 0 0 0 + shapeWave (theta + pi) f f (-1) 0 0 0)
  if all (< 1.0e-9) (map halfWaveSymmetry [0.0, 0.5, 1.2, 2.0, 2.9, pi - 0.01])
    then pure () else fail "equal ferocities must give an odd-symmetric wave"
  let lim = limiarFromFerocities 3 5
      bounded p = let v = shapeWave p 3 5 lim 20 10 (-10) in v >= -1.0000001 && v <= 1.0000001
  if all bounded [0.0, 0.7, 1.3, 2.1, 3.0, 4.5, 6.0]
    then pure () else fail "shaped wave must stay within [-1, +1]"
  let eps = 1.0e-5
      derivPoint = 1.3
      (_, dv) = shapeWaveWithDerivative derivPoint 3 5 lim 20 10 (-10)
      fd = (shapeWave (derivPoint + eps) 3 5 lim 20 10 (-10)
            - shapeWave (derivPoint - eps) 3 5 lim 20 10 (-10)) / (2 * eps)
  if abs (dv - fd) < 1.0e-3
    then pure () else fail "analytic wave derivative must match finite differences"
  let (_, osc1) = advanceOscillator 12 1 0 (1 / 240) defaultOscillator
  if oscPhase osc1 >= 0 && oscPhase osc1 < 2 * pi && oscDebtVel osc1 == 0
    then pure ()
    else fail "beat-locked oscillator must stay on-grid at nominal demand"
  -- Firmware mixer: glide holds STATIC stroke positions (no pump, no cadence),
  -- flapping at full throttle with correct freq.
  let glideMixer pilot phase = fst (computeServoMixer True (pilotToRc pilot)
                                  defaultFirmwareParams
                                  (defaultFirmwareState
                                    { fwOscillator = defaultOscillator
                                        { oscBasePhase = phase, oscPhase = phase } })
                                  (1 / 240))
      centre out = (mixLeftFlapDevDeg out + mixRightFlapDevDeg out) / 2
      neutral = glideMixer (pilot4 0 0 0 0) 0
      neutralBot = glideMixer (pilot4 0 0 0 0) pi
      rollTop = glideMixer (pilot4 0 0.5 0 0) 0
      rollBot = glideMixer (pilot4 0 0.5 0 0) pi
      yawTop = glideMixer (pilot4 0 0 0 0.5) 0
      yawBot = glideMixer (pilot4 0 0 0 0.5) pi
      pitchTop = glideMixer (pilot4 0 0 0.5 0) 0
      pitchBot = glideMixer (pilot4 0 0 0.5 0) pi
  -- The static stroke is phase-independent: the phase-0 and phase-π commands
  -- must be identical, and the glide cadence is 0 (no flapping in glide).
  if not (mixIsFlapping neutral) && mixFlapHz neutral == 0
     && mixLeftFlapDevDeg neutral == mixLeftFlapDevDeg neutralBot
     && mixRightFlapDevDeg neutral == mixRightFlapDevDeg neutralBot
     && abs (mixLeftFlapDevDeg neutral - mixRightFlapDevDeg neutral) < 1e-9
    then pure ()
    else fail "neutral glide must hold a static symmetric stroke, no cadence"
  -- Positive aileron raises the RIGHT wing (stick-right, like a real aileron):
  -- body counter-rotates the wing movement and banks right. Rudder must use
  -- the same orientation after removing the hidden ruddervators. Neither may
  -- shift the common centre — the centre stays the pitch trim (+4° flap
  -- deviation from the −4° glide angle). Pitch (elevator) shifts the centre.
  if mixRightFlapDevDeg rollTop > mixLeftFlapDevDeg rollTop
     && mixRightFlapDevDeg yawTop > mixLeftFlapDevDeg yawTop
     && abs (centre rollTop - 4) < 1e-9
     && abs (centre yawTop - 4) < 1e-9
     && centre pitchTop < 4
    then pure ()
    else fail "glide roll/yaw must be a static differential stroke, pitch a common centre trim only"
   -- Powered roll must still command position at both stroke reversals.
  let positionParams = defaultFirmwareParams
        { fwProfile = (fwProfile defaultFirmwareParams) { profAileronSkewMix = 0 } }
      rollAt phase = fst (computeServoMixer True (pilotToRc (pilot4 0.3 0.1 0 0))
        positionParams (defaultFirmwareState { fwOscillator = defaultOscillator { oscPhase = phase } }) (1/960))
  if all (\o -> mixRightFlapDevDeg o > mixLeftFlapDevDeg o) (map rollAt [0,pi/2,pi,3*pi/2])
    then pure () else fail "powered aileron position authority must persist throughout the stroke"
  let mixerAt throttle pitch yaw phase = fst (computeServoMixer True
        (pilotToRc (pilot4 throttle 0 pitch yaw)) positionParams
        (defaultFirmwareState { fwOscillator = defaultOscillator { oscBasePhase = phase, oscPhase = phase } }) (1/960))
      phases = [fromIntegral i*2*pi/720 | i<-[0..720::Int]]
      yawConsistent throttle sign = all (\phase ->
        let o=mixerAt throttle 0 (sign*0.5) phase
        in sign*(mixRightFlapDevDeg o-mixLeftFlapDevDeg o)>0) phases
      travel pitch = let angles=map (mixLeftFlapDevDeg . mixerAt 1 pitch 0) phases
                     in maximum angles-minimum angles
  if and [yawConsistent throttle sign | throttle<-[0,0.3,0.72,1],sign<-[-1,1]]
    then pure () else fail "yaw hinge commands must initiate the same bank at every beat phase and through glide transition"
  if all (\pitch -> abs(travel pitch-travel 0)<1e-8) [-1,1]
    then pure () else fail "extreme anhedral/dihedral trim must preserve full powered stroke travel"
  if all (\o -> abs (mixRudderUs o - 1500) < 1e-10)
       ([rollTop,rollBot] ++ map rollAt [0,pi/2,pi,3*pi/2])
       && mixRudderUs yawTop > 1500
    then pure () else fail "roll alone must leave the tail rudder neutral in glide and powered flight"
  let coherent output = abs (mixLeftFlapDevDeg output + (mixLeftWingDeg output-100)/2) < 1e-10
                     && abs (mixRightFlapDevDeg output - (mixRightWingDeg output-100)/2) < 1e-10
  if all coherent [neutral,rollTop,rollBot,yawTop,yawBot,pitchTop,pitchBot]
    then pure () else fail "physical flap angles must include the complete mixed servo command"
  if all (\a -> abs (crsfToNorm (normToRaw a) - a) < 1e-12) [-1,-0.5,0,0.5,1]
    then pure () else fail "RC normalization must preserve exact neutral and endpoint values"
  let flapRc = defaultRcChannels { rcThrottle = 1811 }
      (flap, flapState) = computeServoMixer True flapRc defaultFirmwareParams defaultFirmwareState (1 / 240)
  if mixIsFlapping flap && mixFlapHz flap > 0 && mixThrottlePct flap == 1
     && mixLeftFlapDevDeg flap /= 0 && mixRightFlapDevDeg flap /= 0
    then pure ()
    else fail "full throttle must flap with positive frequency and deflection"
  -- Flapping hysteresis: dropping to just below the on-threshold stays flapping.
  let lowThrottle = defaultRcChannels { rcThrottle = 300 }
      (hystOut, _) = computeServoMixer True lowThrottle defaultFirmwareParams flapState (1 / 240)
  if mixIsFlapping hystOut
    then pure ()
    else fail "flapping must persist within the hysteresis band"
  -- Servo: unloaded tracking converges toward the target.
  let servo = defaultServoSpec
      battery = defaultBatterySpec
      voltage = batteryNominalVoltage battery
      (a1, s1) = stepServo servo battery 120 0 wingI voltage (1 / 240) defaultServoState
      (a2, s2) = stepServo servo battery 120 0 wingI voltage (1 / 240) s1
  if a2 > a1 && a2 <= 120
    then pure ()
    else fail "unloaded servo must slew toward its target"
  -- Coordination may reserve motor capacity for roll, but cannot create
  -- extra torque or break left/right symmetry, even under saturation.
  let pair target load rates volts = servoPairDriveTorque servo battery volts (1/240)
        target load (wingI,wingI/10) (defaultServoState,defaultServoState) rates
      swap (a,b) = (b,a)
      pairCases = [(target,load,rates) | target<-[(50,50),(-60,70),(70,-60)]
                    ,load<-[(0,0),(-1,2)],rates<-[(0,0),(4,-7),(-10,8)]]
      boundedPair (target,load,rates) =
        let (l,r)=pair target load rates voltage
            (mr,ml)=pair (swap target) (swap load) (swap rates) voltage
        in all (\t -> abs t<=servoStallTorqueNm servo+1e-12) [l,r]
             && abs(l-ml)+abs(r-mr)<1e-10
             && pair target load rates 0 == (0,0)
  if all boundedPair pairCases then pure ()
    else fail "coordinated servos must respect torque supply, dead battery and mirrored controls"
  let ratePair reference target load rates volts = servoPairDriveTorqueWithRate servo battery volts (1/240)
        reference target load (wingI,wingI/10) (defaultServoState,defaultServoState) rates
      boundedRatePair (reference,(target,load,rates)) =
        let (l,r)=ratePair reference target load rates voltage
            (mr,ml)=ratePair (-reference) (swap target) (swap load) (swap rates) voltage
        in all (\t -> abs t<=servoStallTorqueNm servo+1e-12) [l,r]
             && abs(l-ml)+abs(r-mr)<1e-10
             && ratePair reference target load rates 0 == (0,0)
  if all boundedRatePair [(reference,entry) | reference<-[-100,0,100],entry<-pairCases]
    then pure () else fail "differential velocity tracking must preserve motor limits, dead battery and mirror symmetry"
  -- Differential flap amplitude must not be filtered into the opposite phase.
  -- A five-Hz, small-amplitude command is within the motor envelope. Track its
  -- commanded velocity as well as its position; measure the settled correlation.
  let tracking feed =
        let h=1/1000; inertia=0.0004+reflectedDriveInertia
            advance (angle,rate,correlation) i =
              let t=fromIntegral i*h; frequency=2*pi*5
                  target=5*sin(frequency*t)
                  desiredRate=if feed then radians(5*frequency*cos(frequency*t)) else 0
                  states=(ServoState (-angle) (-rate*180/pi),ServoState angle (rate*180/pi))
                  (_,torque)=servoPairDriveTorqueWithRate servo battery voltage h desiredRate
                    (-target,target) (0,0) (wingI,0.0004) states (-rate,rate)
                  nextRate=rate+h*torque/inertia
                  nextAngle=angle+h*nextRate*180/pi
              in (nextAngle,nextRate,correlation+if i>1000 then target*nextAngle else 0)
            (_,_,result)=foldl advance (0,0,0) [1..4000::Int]
        in result
  if tracking True>1000 && tracking False<0
    then pure () else fail "five-Hz differential amplitude must follow the commanded side instead of phase-reversing it"
  -- Servo: an overpowering load saturates the tracker's commanded rate to ZERO
  -- (the servo can no longer track the target). The unified tracker never
  -- actively back-drives the shaft in the load's direction — that assist is
  -- what flung the wing into extreme-up flutter. The wing wins through the
  -- passive 'servoBackdriveTorque' in the joint solve, not through the motor.
  let stall = servoStallTorqueNm servo
      (back, _) = stepServo servo battery 120 (negate stall * 2) wingI voltage (1 / 240) defaultServoState
  if back == 0
    then pure ()
    else fail "overloaded servo tracker must saturate to zero commanded rate"
  -- Closed loop: full throttle flaps the wings (servo tracks, lift appears).
  let flapRcClosed = defaultRcChannels { rcThrottle = 1811 }
      dtClosed = 1 / 240
      bodyVel = Vec3 5 0 0
      (out1, st1) = stepFirmwareVehicle True flapRcClosed defaultFirmwareParams
                     defaultServoSpec defaultBatterySpec kestrelModel dtClosed bodyVel (Vec3 0 0 0) 1000
                     defaultFirmwareVehicleState
      (out2, _) = stepFirmwareVehicle True flapRcClosed defaultFirmwareParams
                     defaultServoSpec defaultBatterySpec kestrelModel dtClosed bodyVel (Vec3 0 0 0) 1000 st1
      finite value = not (isNaN value || isInfinite value)
  if finite (z (totalForceN out2)) && finite (x (totalMomentNm out2))
     && totalMechanicalPowerW out2 >= 0
    then pure ()
    else fail "firmware-vehicle closed loop must produce finite loads"
  -- Ground effect: proximity to the surface (normalised by wingspan) boosts
  -- flapping lift AND forward thrust, while a distant wing feels neither.
  let geInput h = VehicleInput (1 / 240) (Vec3 5 0 0) (Vec3 0 0 0) 0 0 0 0 h (Vec3 0 0 1)
      strokeRateDown = -3.0
      (highGe, _) = stepWingWithStroke True (-1) 0 strokeRateDown (geInput 1000) kestrelModel 0 (replicate stripCount 0) 0 initialStrips
      (lowGe, _)  = stepWingWithStroke True (-1) 0 strokeRateDown (geInput 0.05) kestrelModel 0 (replicate stripCount 0) 0 initialStrips
      liftOf = sum . map (z . resultForce)
      thrustOf = sum . map (x . resultForce)
  if liftOf lowGe > liftOf highGe && thrustOf lowGe > thrustOf highGe
    then pure ()
    else fail "ground effect must raise flapping lift and forward thrust"
  -- Logical pilot input: maps faithfully onto the firmware's CRSF channel space.
  let rcFull = pilotToRc (pilot4 1 0 0 0)
  if rcThrottle rcFull == 1811 && rcAileron rcFull == 992 && rcRudder rcFull == 992
     && rcArm rcFull == 1811
    then pure ()
    else fail "full-throttle pilot input must map to full CRSF throttle with neutral sticks"
  let rcGlide = pilotToRc defaultPilotInput
      (glidePilot, _) = computeServoMixer True rcGlide defaultFirmwareParams defaultFirmwareState (1 / 240)
  if not (mixIsFlapping glidePilot)
    then pure ()
    else fail "zero-throttle pilot input must hold glide"
  let rcRoll = pilotToRc (pilot4 1 1 0 0)
  if rcAileron rcRoll > 992 && rcRudder rcRoll == 992 && rcElevator rcRoll == 992
    then pure ()
    else fail "roll stick must deflect the aileron channel only"
  -- Closed loop via logical pilot input: full throttle flaps and tracks battery.
  let pilotFlap = pilot4 1 0 0 0
      dtFw = 1 / 240
      bodyVelFw = Vec3 5 0 0
      stepFw s = stepFirmwareVehicle True (pilotToRc pilotFlap) defaultFirmwareParams
                   defaultServoSpec defaultBatterySpec kestrelModel dtFw bodyVelFw (Vec3 0 0 0) 1000 s
      goFw 0 s peak = (s, peak)
      goFw n s peak =
        let (_, s') = stepFw s
            peak' = max peak (max (abs (servoAngleDeg (fvServoLeft s')))
                                  (abs (servoAngleDeg (fvServoRight s'))))
        in goFw (n - 1) s' peak'
      (warmFw, peakFlap) = goFw 120 defaultFirmwareVehicleState 0
  if fwWasFlapping (fvFirmware warmFw) && peakFlap > 1.0 && fvBatterySoc warmFw <= 1.0
    then pure ()
    else fail "pilot-driven firmware loop must flap both wings and track battery"
  -- Isolate aileron timing: no static wing offset, rudder surface or amplitude
  -- differential is available to generate this roll moment.
  let skewParams = defaultFirmwareParams
        { fwServoSpeedMs = 50, fwFlapBaseFreqDh = 32, fwRudderRollWeight = 0
        , fwProfile = (fwProfile defaultFirmwareParams)
            { profThrottleFrequencyMix = 100, profAileronSkewMix = 55
            , profAileronScale = 0, profRudderAmplitudeDiff = 0 } }
      skewServo = defaultServoSpec { servoNoLoadSpeedDegPerSec = 1200, servoStallTorqueNm = 8 }
      skewBattery = defaultBatterySpec
        { batteryNominalVoltage = 11.1, batteryCapacityAh = 1.3, batteryInternalResistanceOhm = 0.08 }
      skewMoment roll =
        let rc = pilotToRc (pilot4 0.72 roll 0 0)
            tick (_,s) =
              let (out,next)=stepFirmwareVehicle True rc skewParams skewServo skewBattery kestrelModel
                              (1/240) (Vec3 8 0 (-1)) (Vec3 0 0 0) 1000 s
                  -- The tunnel constrains body attitude. Reconcile that with
                  -- the free-body predictor just as the Unreal host does;
                  -- otherwise air memory rotates while the fixture does not.
                  predictedAngle=x(totalMomentNm out)/0.008/(240*240)
              in (out,transportAirfields (Vec3 predictedAngle 0 0) next)
            samples = drop 481 $ take 1921 $ iterate tick (zeroVehicleOutput,defaultFirmwareVehicleState)
        in if any ((/=0) . outputFlags . fst) samples then error "stroke timing integration rejected a step"
           else sum (map (x . totalMomentNm . fst) samples) / fromIntegral (length samples)
      leftSkewMoment = skewMoment (-0.5)
      rightSkewMoment = skewMoment 0.5
  -- The property is the MIRROR (left skew = −right skew) and a non-zero roll
  -- authority; the asymmetric feathering (downstroke loaded, upstroke shed)
  -- legitimately shifts the sign and magnitude versus the old symmetric stroke,
  -- so the sign is not asserted here.
  -- The finite-difference nonlinear solve converges to 1e-7 rad/s; compare
  -- mirrored time-averaged torques within 1e-8 Nm, above its roundoff floor.
  if abs (leftSkewMoment + rightSkewMoment) < 1e-8
     && abs leftSkewMoment > 5e-5
    then pure () else fail ("opposite stroke timing alone must generate mirrored aerodynamic roll: " ++ show (leftSkewMoment,rightSkewMoment))
  -- ONDAS Rubedo M0: ferocity-price anchors (audit-verified 9.5).
  let priceCheck (d, p, f, t) = abs (ferocityPowerPrice d - p) < 1e-2
                               && abs (ferocityForcePrice d - f) < 1e-2
                               && abs (ferocityThrustPrice d - t) < 1e-2
  if all priceCheck [(0.05, 1.166, 1.108, 1.053), (0.3, 2.92, 2.04, 1.43), (0.8, 125, 25, 5)]
    then pure () else fail "ferocity price must match the 9.5 audit anchors"
  if abs (dwellFromFerocity 8 - 0.98) < 1e-12 && dwellFromFerocity 0 == 0
    then pure () else fail "dwell must scale ferocity 0..8 onto [0, 0.98]"
  -- Read-only response-phase diagnostic: it must idle below omega_min and keep
  -- its lag estimate within [-pi, pi] — pure telemetry, never a controller output.
  if rpPhaseError (stepResponsePhase 10 9 0 0.05 0.001 defaultResponsePhase) == 0
    then pure () else fail "response phase must idle below the omega threshold"
  let rpLag = rpPhaseError (stepResponsePhase 10 9 (pi/4) 10 0.001 defaultResponsePhase)
  if abs rpLag <= pi
    then pure () else fail "response-phase lag must stay within [-pi, pi]"
  -- ONDAS Rubedo M3: golden-angle strobe covers every bin without aliasing.
  if gcd (toInteger goldenAngleStep) (2 ^ (20 :: Integer)) == 1
    then pure () else fail "golden-angle step must be coprime to 2^20"
  let strobes = iterate (phaseStrobe 1.0) defaultPhaseEnvelope
      covered = phaseCoverage (strobes !! 1024)
      spreadPe = phaseEnvelope (strobes !! 1024)
      fixedRasterStrobe v st =
        st { peMeans = zipWith (\i m -> m * (1 - phaseEma)
                                          + if i == 0 then v * phaseEma else 0)
                               [0 :: Int ..] (peMeans st) }
      rasterPe = phaseEnvelope (iterate (fixedRasterStrobe 1.0) defaultPhaseEnvelope !! 1024)
  if covered == 1.0 && spreadPe < 0.65 && spreadPe < rasterPe
    then pure () else fail "golden-angle strobe must cover all bins and de-alias below a fixed raster"
  if detectReversal 0 3 1.5 && not (detectReversal 0 1 1.5) && detectReversal 6 0 1.5
    then pure () else fail "reversal must fire on the limiar crossing and the 2π wrap"
  -- Resonance is EMERGENT from the physical wing spring, not injected: after a
  -- long closed-loop flight the oscillator gain must remain at its nominal 1
  -- (the removed phase-lock no longer writes fwKGainMod).
  let flyFrom s = snd (stepFirmwareVehicle True flapRcClosed defaultFirmwareParams
                        defaultServoSpec defaultBatterySpec kestrelModel (1 / 240)
                        (Vec3 5 0 0) (Vec3 0 0 0) 1000 s)
      steady = iterate flyFrom defaultFirmwareVehicleState !! 2400
      kNominal = fwKGainMod (fvFirmware steady)
  if kNominal == 1.0
    then pure () else fail "oscillator gain must stay nominal (no phase-lock controller)"
  let flatStation st = st {ssLeadX=0,ssTwistDeg=0,ssDihedDeg=0,
                              ssSection=(ssSection st) {spRestCamber=0}}
      flatWing=defaultBirdWing {wsStations=map flatStation (wsStations defaultBirdWing)}
      flatModel=kestrelModel {wmWing=flatWing}
      flatState=replicate stripCount emptyStrip
      dotV a b=x a*x b+y a*y b+z a*z b
      checkAero (angle,rate,incidence,vzAir) =
        let input=flow {bodyVelocityMS=Vec3 8 0 vzAir, groundClearanceM=1000}
            loads=wingLoadsWithStroke True 1 angle rate rate input flatModel incidence flatState (replicate stripCount 0) angle 0
            mirror=wingLoadsWithStroke True 1 (-angle) (-rate) (-rate)
                      (input {bodyVelocityMS=Vec3 8 0 (-vzAir)}) flatModel (-incidence) flatState (replicate stripCount 0) (-angle) 0
            checkStrip (i,r) =
              let f=(fromIntegral i+0.5)/fromIntegral stripCount
                  radius=wsShoulderM flatWing+wsSpanM flatWing*f
                  chord=shapeChord flatWing f
                  c=Vec3 (cos incidence) (-sin incidence*sin angle) (sin incidence*cos angle)
                  p=addVec (Vec3 0 (radius*cos angle) (radius*sin angle)) (scaleVec (-0.25*chord) c)
                  velocity=addVec (bodyVelocityMS input) (crossVec (Vec3 rate 0 0) p)
              in dotV (resultForce r) velocity <= 1e-8
            f=summed loads; g=summed mirror
        in all checkStrip (zip [0::Int ..] loads)
           && magnitude (addVec f (Vec3 (-x g) (-y g) (z g))) < 1e-7
  if all checkAero [(a,r,i,v) | a<-[-0.6,0,0.6],r<-[-8,0,8],i<-[-0.25,0,0.25],v<-[-2,0,2]]
    then pure () else fail "surface forces must dissipate relative-flow energy and reflect for inverted geometry"
  -- Identical geometry and airflow must give identical pressure in glide
  -- and flap. Neither an arbitrary weight share nor mixer centre may create
  -- lift. Local wind, however, must affect the individual strip it reaches.
  let sameMode flag shares centre =
        wingLoadsWithStroke flag 1 0.2 0.4 0 flow flatModel 0.1 flatState shares centre 0
      glide = sameMode False (replicate stripCount 0) 0
      flap = sameMode True (replicate stripCount 10) 0.7
      forceError a b = magnitude (addVec (resultForce a) (scaleVec (-1) (resultForce b)))
      gustStates = case flatState of
        s:ss -> s {stripWindDelta=Vec3 0 0 2} : ss
        [] -> []
      gust = wingLoadsWithStroke False 1 0.2 0.4 0 flow flatModel 0.1 gustStates (replicate stripCount 0) 0 0
  if all (<1e-12) (zipWith forceError glide flap)
     && forceError (head gust) (head glide)>1e-3
     && all (<1e-12) (tail (zipWith forceError gust glide))
    then pure () else fail "shared strip forces must ignore mode/weight and sample their own wind"
  let speedLimit=servoNoLoadSpeedDegPerSec defaultServoSpec*pi/180
      stallLimit=servoStallTorqueNm defaultServoSpec
      voltage=batteryNominalVoltage defaultBatterySpec
      torqueAt rate target=servoDriveTorque defaultServoSpec defaultBatterySpec voltage (1/240)
                            target 0 0.002 defaultServoState rate
  if all (\r -> all (\target -> torqueAt r target*r <= stallLimit*speedLimit/4+1e-9) [-80,0,80])
         (map (*speedLimit) [-2,-1,-0.75,-0.5,0,0.5,0.75,1,2])
    then pure () else fail "servo mechanical power must respect torque-speed envelope"
  let mirroredStep (left,right,err) i =
        let t=fromIntegral i/240; a=0.4*cos (4*pi*t); rate= -1.6*pi*sin (4*pi*t)
            v=flow {bodyVelocityMS=Vec3 6 0 (-0.5),groundClearanceM=1000}
            (ls,ln)=stepCoupledWing True 1 a rate rate 0 v flatModel 0.1 left (replicate stripCount 0) a 0
            (rs,rn)=stepCoupledWing True 1 (-a) (-rate) (-rate) 0
                      (v {bodyVelocityMS=Vec3 6 0 0.5}) flatModel (-0.1) right (replicate stripCount 0) (-a) 0
            f=summed ls; g=summed rs
        in (ln,rn,max err (magnitude (addVec f (Vec3 (-x g) (-y g) (z g)))))
      (_,_,reflectionError)=foldl mirroredStep (flatState,flatState,0) [1..480::Int]
  if reflectionError < 1e-7 then pure ()
    else fail ("unsteady stall and structural history break inverted reflection: "++show reflectionError)
  if all (\(scale,f) -> abs (sparTwistGJSpanwise (scaleSpars scale kestrelSpars) f
                         / sparTwistGJSpanwise kestrelSpars f-scale**4)<1e-8)
         [(scale,f) | scale<-[0.5,2],f<-[0.2,0.7]]
    then pure () else fail "distributed torsional stiffness must scale as length to the fourth power"
  -- Broadside pressure must exist immediately, before separation memory grows.
  -- Both wings at zero forward speed: analytic Cp=1.2 resisting roll moment.
  let rollPressure separated =
        let omega=2
            input=flow {bodyVelocityMS=zero,bodyRatesRadS=Vec3 omega 0 0,groundClearanceM=1000}
            states=replicate stripCount (emptyStrip {stripSeparation=separated})
            rs=concat [wingLoadsWithStroke False side 0 0 0 input flatModel 0 states (replicate stripCount 0) 0 0 | side<-[-1,1]]
            moment=sum(map (x . resultMoment) rs)
            dr=wsSpanM flatWing/fromIntegral stripCount
            integral=sum [shapeChord flatWing f*dr*(wsShoulderM flatWing+wsSpanM flatWing*f)**3
                         | i<-[0..stripCount-1],let f=(fromIntegral i+0.5)/fromIntegral stripCount]
        in moment <= -airDensity*1.2*omega*omega*integral+1e-9
  if all rollPressure [0,1] then pure ()
    else fail "broadside roll pressure cannot wait for stall memory or a mode switch"
  -- A separated membrane must deform from its actual pressure, not the
  -- hypothetical attached lift that stall removed.
  let stalled=replicate stripCount (emptyStrip {stripSeparation=1})
      mount=0.1
      input=flow {bodyVelocityMS=Vec3 8 0 0,groundClearanceM=1000}
      (stalledLoads,stalledNext)=stepCoupledWing True 1 0 0 0 0 input flatModel mount stalled (replicate stripCount 0) 0 0
      checkMembrane (i,r,next) =
        let f=(fromIntegral i+0.5)/fromIntegral stripCount
            normal=stripNormal next
            d=atan(stripBendSlope next)
            spanAxis=Vec3 0 (cos d) (sin d)
            v=stripAirVelocity next
            planarSpeed2=dotV v v-dotV v spanAxis**2
            qArea=0.5*airDensity*planarSpeed2*shapeChord flatWing f*wsSpanM flatWing/fromIntegral stripCount
            profile=shapeSection flatWing f
            actualCn=dotV (resultForce r) normal/qArea
            expected=relaxCamber (stepSeconds input) (spMembraneTau profile) 0
                       (membraneCamberTarget profile (max (-12) (min 12 actualCn)))
        in abs (stripCamber next-expected)<1e-10
  if all checkMembrane (zip3 [0::Int ..] stalledLoads stalledNext)
    then pure () else fail "stalled membrane must follow solved pressure instead of unattained attached lift"
  -- Dihedral creates a rolling response to sideslip through strip geometry,
  -- not an attitude target. Reverse dihedral or sideslip to reverse the moment.
  let sideslipRoll dihedral slip =
        let input=flow {bodyVelocityMS=Vec3 8 slip (-0.2),bodyRatesRadS=zeroVec,groundClearanceM=1000}
            rs=concat [wingLoadsWithStroke False side dihedral 0 0 input flatModel 0.08 flatState
                       (replicate stripCount 0) 0 0 | side<-[-1,1]]
        in sum(map (vx.resultMoment) rs)
  if and [a*v*sideslipRoll a v>1e-4 | a<-[-0.35,0.35],v<-[-1,1]]
     && abs(sideslipRoll 0.35 0)<1e-10 && abs(sideslipRoll (-0.35) 0)<1e-10
    then pure () else fail "dihedral/anhedral sideslip response must reverse without a horizon torque"
  -- Neither a banked world-up vector nor stale pressure displacement may
  -- generate an aerodynamic restoring torque in identical high-altitude flow.
  let oldAnchors=map (\s -> s {stripPressureField=PressureField (Vec3 0 0 10) (Vec3 0 1 0) 100}) flatState
      unanchored up states=concat [wingLoadsWithStroke True side 0.15 (-2) (-2)
        (flow {groundClearanceM=1000,bodyUpB=up}) flatModel 0.08 states (replicate stripCount 0) 0 0 | side<-[-1,1]]
      baseline=unanchored (Vec3 0 0 1) flatState
      diagnostic r=let st=resultState r
                       f=addVec (stripLift st) (addVec (stripDrag st) (stripPressure st))
                   in magnitude(addVec f (scaleVec (-1) (resultForce r)))<1e-12
                      && magnitude(addVec (stripMoment st) (scaleVec (-1) (resultMoment r)))<1e-12
  if and [all (<1e-12) (zipWith forceError baseline (unanchored up states))
         | up<-[Vec3 0 0 1,Vec3 0 1 0,Vec3 0 0 (-1)],states<-[flatState,oldAnchors]]
     && all diagnostic baseline
    then pure () else fail "inverted/banked flow or stale air patches changed forces, or diagnostics lost a force term"
  let boundCases=[(f,g,m,k) | f<-[0,4,8],g<-[0,4,8],m<-[0,50,100],k<-[-80,0,80]]
      boundHolds (f,g,m,k)=
        let bound=wavePeakDerivativeBound f g (-1) m k (-k)
        in all (\i -> abs (snd (shapeWaveWithDerivative (2*pi*fromIntegral i/500)
                            f g (-1) m k (-k)))<=bound+1e-9) [0..499::Int]
  if all boundHolds boundCases then pure () else fail "waveform cadence bound underestimates peak shaft speed"
  let commandedTravel fer =
        let p=defaultFirmwareParams {fwProfile=(fwProfile defaultFirmwareParams)
                    {profStrokeFerocity=fer,profReturnFerocity=fer,profTailElevatorAngleDeg=0}}
            at i =
              let phase=2*pi*fromIntegral i/500
                  state=defaultFirmwareState {fwOscillator=defaultOscillator {oscBasePhase=phase,oscPhase=phase}}
              in mixLeftFlapDevDeg (fst (computeServoMixer True flapRc p state 1e-6))
            values=map at [0..499::Int]
        in maximum values-minimum values
  let ampMax = strokeAmpMaxDeg defaultFirmwareParams
      minTravel = 2 * ampMax - 1
  if all (\fer -> commandedTravel fer > minTravel) [0,50,100]
    then pure () else fail "full-throttle dwell must retain the full stroke travel rather than shrink the arc"
  putStrLn "MathCore properties passed"

zeroVehicleOutput :: VehicleOutput
zeroVehicleOutput = VehicleOutput (Vec3 0 0 0) (Vec3 0 0 0) 0 0 (-1) 0
