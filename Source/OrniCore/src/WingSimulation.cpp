#include "ornicore/WingSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ornicore {
namespace {

double lerp(double from, double to, double amount) noexcept {
    return from + (to - from) * amount;
}

}  // namespace

WingSimulation::WingSimulation(WingGeometry geometry, Kinematics kinematics,
                               Environment environment, ModelParameters parameters)
    : geometry_(std::move(geometry)),
      kinematics_(std::move(kinematics)),
      environment_(std::move(environment)),
      parameters_(std::move(parameters)) {
    if (geometry_.elementCount < 3 || geometry_.spanM <= 0.0 ||
        geometry_.rootChordM <= 0.0 || geometry_.tipChordM <= 0.0) {
        throw std::invalid_argument("Wing geometry must contain at least three positive elements");
    }
    if (kinematics_.frequencyHz < 0.0 || environment_.airDensityKgM3 <= 0.0) {
        throw std::invalid_argument("Frequency must be non-negative and density must be positive");
    }
    initializeElements();
}

void WingSimulation::initializeElements() {
    elements_.resize(geometry_.elementCount);
    nextSeparation_.resize(geometry_.elementCount);
    const double dr = geometry_.spanM / static_cast<double>(geometry_.elementCount);

    for (std::size_t index = 0; index < elements_.size(); ++index) {
        const double fraction = (static_cast<double>(index) + 0.5) /
                                static_cast<double>(elements_.size());
        auto& element = elements_[index];
        element.radiusM = dr * (static_cast<double>(index) + 0.5);
        element.chordM = lerp(geometry_.rootChordM, geometry_.tipChordM, fraction);
        element.sweepDeg = lerp(geometry_.rootSweepDeg, geometry_.tipSweepDeg, fraction);
        element.twistDeg = lerp(geometry_.rootTwistDeg, geometry_.tipTwistDeg, fraction);
    }
}

void WingSimulation::reset() noexcept {
    timeS_ = 0.0;
    for (auto& element : elements_) {
        element.angleOfAttackRad = 0.0;
        element.reynoldsNumber = 0.0;
        element.spanwiseVelocityMS = 0.0;
        element.separationFraction = 0.0;
        element.liftN = 0.0;
        element.dragN = 0.0;
    }
    std::fill(nextSeparation_.begin(), nextSeparation_.end(), 0.0);
}

WingSnapshot WingSimulation::step(double deltaTimeS) {
    if (!(deltaTimeS > 0.0) || deltaTimeS > 0.05) {
        throw std::invalid_argument("Aerodynamic time step must be in (0, 0.05] seconds");
    }

    timeS_ += deltaTimeS;
    const double omega = 2.0 * kPi * kinematics_.frequencyHz;
    const double amplitude = radians(kinematics_.amplitudeDeg);
    const double strokeAngle = amplitude * std::sin(omega * timeS_);
    const double strokeRate = amplitude * omega * std::cos(omega * timeS_);
    const double dr = geometry_.spanM / static_cast<double>(elements_.size());

    // First pass: local flow and equilibrium separation. Crossflow changes sign
    // with signed local sweep; the empirical multiplier remains intentionally exposed.
    for (std::size_t index = 0; index < elements_.size(); ++index) {
        auto& element = elements_[index];
        const double normalVelocity = -strokeRate * element.radiusM;
        const double chordVelocity = std::max(0.05, environment_.forwardAirspeedMS);
        const double planarSpeed = std::hypot(chordVelocity, normalVelocity);
        const double pitch = radians(element.twistDeg + kinematics_.pitchBiasDeg);
        element.angleOfAttackRad = pitch + std::atan2(-normalVelocity, chordVelocity);
        element.reynoldsNumber = planarSpeed * element.chordM /
                                 environment_.kinematicViscosityM2S;

        const double phaseFactor = 0.65 + 0.35 * std::abs(std::sin(omega * timeS_));
        element.spanwiseVelocityMS = parameters_.crossflowGain *
                                     std::sin(radians(element.sweepDeg)) *
                                     planarSpeed * phaseFactor;

        const double excessDeg = std::abs(element.angleOfAttackRad) * 180.0 / kPi -
                                 parameters_.staticStallDeg;
        const double targetSeparation = smoothStep(
            -parameters_.stallTransitionDeg * 0.5,
            parameters_.stallTransitionDeg, excessDeg);
        const double timeConstant = targetSeparation > element.separationFraction
                                        ? parameters_.separationTimeS
                                        : parameters_.reattachmentTimeS;
        const double relaxation = 1.0 - std::exp(-deltaTimeS / timeConstant);
        nextSeparation_[index] = element.separationFraction +
                                 relaxation * (targetSeparation - element.separationFraction);
    }

    // Second pass: signed upwind advection plus mild diffusion couples adjacent strips.
    for (std::size_t index = 0; index < elements_.size(); ++index) {
        const auto& element = elements_[index];
        const std::size_t upstream = element.spanwiseVelocityMS >= 0.0
                                         ? (index == 0 ? 0 : index - 1)
                                         : std::min(index + 1, elements_.size() - 1);
        const std::size_t left = index == 0 ? 0 : index - 1;
        const std::size_t right = std::min(index + 1, elements_.size() - 1);
        const double courant = clamp(std::abs(element.spanwiseVelocityMS) * deltaTimeS / dr,
                                     0.0, 0.45);
        const double advected = parameters_.separationAdvectionGain * courant *
                                (elements_[upstream].separationFraction -
                                 elements_[index].separationFraction);
        const double laplacian = elements_[left].separationFraction -
                                 2.0 * elements_[index].separationFraction +
                                 elements_[right].separationFraction;
        nextSeparation_[index] = clamp(nextSeparation_[index] + advected +
                                           parameters_.separationDiffusion * laplacian,
                                       0.0, 1.0);
    }

    WingLoads total{};
    for (std::size_t index = 0; index < elements_.size(); ++index) {
        auto& element = elements_[index];
        element.separationFraction = nextSeparation_[index];

        const double normalVelocity = -strokeRate * element.radiusM;
        const double chordVelocity = std::max(0.05, environment_.forwardAirspeedMS);
        const double speedSquared = chordVelocity * chordVelocity +
                                    normalVelocity * normalVelocity;
        const double speed = std::sqrt(speedSquared);
        const double alpha = element.angleOfAttackRad;
        const double attachedCl = clamp(2.0 * kPi * alpha, -1.8, 1.8);
        const double separatedCl = 1.05 * std::sin(2.0 * alpha);
        const double separation = element.separationFraction;
        const double cl = lerp(attachedCl, separatedCl, separation);
        const double attachedCd = parameters_.minimumDragCoefficient + 0.055 * cl * cl;
        const double separatedCd = 0.20 + 1.20 * std::pow(std::sin(alpha), 2.0);
        const double cd = lerp(attachedCd, separatedCd, separation);
        const double area = element.chordM * dr;
        const double dynamicPressure = 0.5 * environment_.airDensityKgM3 * speedSquared;
        element.liftN = dynamicPressure * area * cl;
        element.dragN = dynamicPressure * area * cd;

        // Lift is normal to the local planar flow; drag opposes it.
        const double flowX = chordVelocity / speed;
        const double flowZ = normalVelocity / speed;
        const double forceX = -element.dragN * flowX - element.liftN * flowZ;
        const double forceZ = -element.dragN * flowZ + element.liftN * flowX;
        const Vec3 force{forceX, 0.0, forceZ};
        total.forceN += force;
        total.momentNm.x += element.radiusM * forceZ;
        total.mechanicalPowerW += std::abs(forceZ * normalVelocity);
    }

    return {timeS_, strokeAngle, strokeRate, total, elements_};
}

}  // namespace ornicore
