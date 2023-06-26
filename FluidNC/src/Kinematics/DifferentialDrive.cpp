#include "DifferentialDrive.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
#include "src/Limits.h"

namespace Kinematics {
    void DifferentialDrive::init() {
        log_info("Kinematic system: " << name());
        init_position();
    }

    // Initialize the machine position
    void DifferentialDrive::init_position() {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            set_motor_steps(axis, 0);  // Set to zeros
        }
    }

    bool DifferentialDrive::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        // Insert conversion from cartesian to differential drive kinematics here.
        return mc_move_motors(target, pl_data);
    }

    void DifferentialDrive::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Insert conversion from differential drive kinematics to cartesian here.
    }

    void DifferentialDrive::transform_cartesian_to_motors(float* cartesian, float* motors) {
        // Insert conversion from cartesian to differential drive kinematics here.
    }

    bool DifferentialDrive::canHome(AxisMask axisMask) {
        // Adjust this to the specifics of a differential drive system
    }

    bool DifferentialDrive::limitReached(AxisMask& axisMask, MotorMask& motorMask, MotorMask limited) {
        // Adjust this to the specifics of a differential drive system
    }

    void DifferentialDrive::releaseMotors(AxisMask axisMask, MotorMask motors) {
        // Adjust this to the specifics of a differential drive system
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<DifferentialDrive> registration("DifferentialDrive");
    }
}
