#include "ornicore/WingSimulation.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ornicore::WingSnapshot settle(ornicore::WingSimulation& simulation, int ticks = 120) {
    ornicore::WingSnapshot result;
    for (int tick = 0; tick < ticks; ++tick) {
        result = simulation.step(1.0 / 240.0);
    }
    return result;
}

void crossflowReversesWithSweep() {
    ornicore::WingGeometry positive;
    positive.rootSweepDeg = positive.tipSweepDeg = 25.0;
    ornicore::WingGeometry negative = positive;
    negative.rootSweepDeg = negative.tipSweepDeg = -25.0;
    ornicore::WingSimulation positiveSimulation(positive, {});
    ornicore::WingSimulation negativeSimulation(negative, {});
    const auto positiveResult = positiveSimulation.step(1.0 / 240.0);
    const auto negativeResult = negativeSimulation.step(1.0 / 240.0);
    const double positiveFlow = positiveResult.elements[8].spanwiseVelocityMS;
    const double negativeFlow = negativeResult.elements[8].spanwiseVelocityMS;
    require(positiveFlow > 0.0, "positive sweep must produce positive baseline crossflow");
    require(negativeFlow < 0.0, "negative sweep must produce negative baseline crossflow");
    require(std::abs(positiveFlow + negativeFlow) < 1e-12,
            "opposite sweep must reverse equal baseline crossflow");
}

void stallIsLocalAndContinuous() {
    ornicore::WingGeometry geometry;
    geometry.rootTwistDeg = 27.0;
    geometry.tipTwistDeg = 2.0;
    geometry.rootSweepDeg = 18.0;
    geometry.tipSweepDeg = -12.0;
    ornicore::Kinematics stationary;
    stationary.frequencyHz = 0.0;
    ornicore::WingSimulation simulation(geometry, stationary);
    const auto result = settle(simulation, 240);
    const double rootSeparation = result.elements.front().separationFraction;
    const double tipSeparation = result.elements.back().separationFraction;
    require(rootSeparation > 0.70, "high-twist root should be substantially separated");
    require(tipSeparation < 0.20, "low-twist tip should remain mostly attached");
    require(rootSeparation <= 1.0 && tipSeparation >= 0.0,
            "separation fractions must remain bounded");
}

void loadsRemainFinite() {
    ornicore::WingSimulation simulation({}, {});
    for (int tick = 0; tick < 2400; ++tick) {
        const auto result = simulation.step(1.0 / 240.0);
        require(std::isfinite(result.loads.forceN.x) && std::isfinite(result.loads.forceN.z),
                "aerodynamic loads must remain finite");
        for (const auto& element : result.elements) {
            require(element.separationFraction >= 0.0 && element.separationFraction <= 1.0,
                    "separation must stay in [0, 1]");
        }
    }
}

}  // namespace

int main() {
    try {
        crossflowReversesWithSweep();
        stallIsLocalAndContinuous();
        loadsRemainFinite();
        std::cout << "All OrniCore tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "OrniCore test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
