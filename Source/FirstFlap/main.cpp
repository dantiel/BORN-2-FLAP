#include "ornicore/WingSimulation.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        const std::string outputPath = argc > 1 ? argv[1] : "first-flap.csv";
        std::ofstream output(outputPath);
        if (!output) {
            throw std::runtime_error("Could not open telemetry output: " + outputPath);
        }

        ornicore::WingGeometry geometry;
        geometry.rootSweepDeg = 24.0;
        geometry.tipSweepDeg = -8.0;  // demonstrates a local crossflow reversal
        geometry.rootTwistDeg = 19.0;
        geometry.tipTwistDeg = 7.0;
        geometry.elementCount = 16;

        ornicore::WingSimulation simulation(geometry, ornicore::Kinematics{});
        constexpr double deltaTime = 1.0 / 240.0;
        constexpr double duration = 2.0;

        output << "time_s,stroke_deg,element,radius_m,sweep_deg,alpha_deg,reynolds,"
                  "crossflow_m_s,separation,lift_n,drag_n,total_fx_n,total_fz_n,power_w\n";
        output << std::fixed << std::setprecision(6);

        double maxSeparation = 0.0;
        double peakVerticalForce = 0.0;
        ornicore::WingSnapshot snapshot;
        for (int tick = 0; tick < static_cast<int>(duration / deltaTime); ++tick) {
            snapshot = simulation.step(deltaTime);
            maxSeparation = std::max(maxSeparation, snapshot.elements.front().separationFraction);
            peakVerticalForce = std::max(peakVerticalForce, std::abs(snapshot.loads.forceN.z));
            for (std::size_t index = 0; index < snapshot.elements.size(); ++index) {
                const auto& element = snapshot.elements[index];
                output << snapshot.timeS << ','
                       << snapshot.strokeAngleRad * 180.0 / ornicore::kPi << ','
                       << index << ',' << element.radiusM << ',' << element.sweepDeg << ','
                       << element.angleOfAttackRad * 180.0 / ornicore::kPi << ','
                       << element.reynoldsNumber << ',' << element.spanwiseVelocityMS << ','
                       << element.separationFraction << ',' << element.liftN << ','
                       << element.dragN << ',' << snapshot.loads.forceN.x << ','
                       << snapshot.loads.forceN.z << ',' << snapshot.loads.mechanicalPowerW << '\n';
            }
        }

        std::cout << "BORN-2-FLAP First Flap 0.1\n"
                  << "Simulated " << geometry.elementCount << " coupled elements at 240 Hz\n"
                  << "Peak |vertical force|: " << peakVerticalForce << " N\n"
                  << "Maximum root separation: " << maxSeparation << "\n"
                  << "Telemetry: " << outputPath << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "first_flap: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
