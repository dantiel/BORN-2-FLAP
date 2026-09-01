#pragma once

#include "ornicore/Math.hpp"

#include <cstddef>
#include <vector>

namespace ornicore {

// OrniCore uses SI units. The local aerodynamic frame is:
// x = chordwise towards the trailing edge, y = root to tip, z = wing normal.
struct WingGeometry {
    double spanM{0.72};
    double rootChordM{0.20};
    double tipChordM{0.10};
    double rootSweepDeg{20.0};
    double tipSweepDeg{20.0};
    double rootTwistDeg{18.0};
    double tipTwistDeg{8.0};
    std::size_t elementCount{16};
};

struct Kinematics {
    double frequencyHz{3.0};
    double amplitudeDeg{42.0};
    double pitchBiasDeg{0.0};
};

struct Environment {
    double airDensityKgM3{1.225};
    double kinematicViscosityM2S{1.48e-5};
    double forwardAirspeedMS{5.0};
};

struct ModelParameters {
    double staticStallDeg{16.0};
    double stallTransitionDeg{7.0};
    double separationTimeS{0.045};
    double reattachmentTimeS{0.090};
    double crossflowGain{0.32};
    double separationAdvectionGain{0.75};
    double separationDiffusion{0.018};
    double minimumDragCoefficient{0.025};
};

struct ElementState {
    double radiusM{};
    double chordM{};
    double sweepDeg{};
    double twistDeg{};
    double angleOfAttackRad{};
    double reynoldsNumber{};
    double spanwiseVelocityMS{};
    double separationFraction{};
    double liftN{};
    double dragN{};
};

struct WingLoads {
    Vec3 forceN{};
    Vec3 momentNm{};
    double mechanicalPowerW{};
};

struct WingSnapshot {
    double timeS{};
    double strokeAngleRad{};
    double strokeRateRadS{};
    WingLoads loads{};
    std::vector<ElementState> elements;
};

class WingSimulation {
public:
    WingSimulation(WingGeometry geometry, Kinematics kinematics,
                   Environment environment = {}, ModelParameters parameters = {});

    [[nodiscard]] WingSnapshot step(double deltaTimeS);
    [[nodiscard]] const std::vector<ElementState>& elements() const noexcept { return elements_; }
    [[nodiscard]] double timeS() const noexcept { return timeS_; }
    void reset() noexcept;

private:
    WingGeometry geometry_;
    Kinematics kinematics_;
    Environment environment_;
    ModelParameters parameters_;
    std::vector<ElementState> elements_;
    std::vector<double> nextSeparation_;
    double timeS_{};

    void initializeElements();
};

}  // namespace ornicore
