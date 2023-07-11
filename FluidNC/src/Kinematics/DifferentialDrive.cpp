#include "DifferentialDrive.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"
#include "src/Limits.h"
#include "src/Machine/Homing.h"

namespace Kinematics {
    void DifferentialDrive::group(Configuration::HandlerBase& handler) {
        handler.item("left_motor_axis", _left_motor_axis);
        handler.item("right_motor_axis", _right_motor_axis);
        handler.item("wheel_radius", _wheel_radius);
        handler.item("distance_between_wheels", _distance_between_wheels);
    }

    void DifferentialDrive::init() {
        log_info("Kinematic system: " << name());
        m_heading = 0.0; // define current heading as zero angle
        init_position();
    }

    // Initialize the machine position
    void DifferentialDrive::init_position() {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            set_motor_steps(axis, 0);  // Set to zeros
        }
    }

    /*
      cartesian_to_motors() converts from cartesian coordinates to motor space.

      All linear motions pass through cartesian_to_motors() to be planned as mc_move_motors operations.

      Parameters:
        target = an n_axis array of target positions (where the move is supposed to go)
        pl_data = planner data (see the definition of this type to see what it is)
        position = an n_axis array of where the machine is starting from for this move
    */
    bool DifferentialDrive::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        //        log_debug("cartesian_to_motors position (" << position[X_AXIS] << "," << position[Y_AXIS] << ") target (" << target[X_AXIS] << "," << target[Y_AXIS] << ")");

        // Strategy here will be to calculate the angular of the vector between position and target,
        // generate a motor move that will rotate us about our center to change from our current heading to that angle,
        // then move the necessary distance straight forward (+ both motors equally) to the target.
        // Make sure Z and any other axes are passed through unchanged during the final move.

        return mc_move_motors(target, pl_data);
    }

    void DifferentialDrive::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Insert conversion from differential drive kinematics to cartesian here.
    }

    void DifferentialDrive::transform_cartesian_to_motors(float* cartesian, float* motors) {
        // Insert conversion from cartesian to differential drive kinematics here.
    }

    bool DifferentialDrive::canHome(AxisMask axisMask) {
        // There is no homing for DiffDrive wheels...
        log_error("Differential Drive kinematic system cannot home");
        return false;
    }

    bool DifferentialDrive::limitReached(AxisMask& axisMask, MotorMask& motorMask, MotorMask limited) {
        // There are no limit switches, so I'm assuming this will never be called...
        return false;
    }

    void DifferentialDrive::releaseMotors(AxisMask axisMask, MotorMask motors) {
        // Also assuming this never happens with no limit switches...
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<DifferentialDrive> registration("DifferentialDrive");
    }
}
