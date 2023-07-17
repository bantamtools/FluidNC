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

        // TODO .... but I think all of the below is actually going to be wrong because I'm generating relative
        //  moves and mc_move_motors is expecting absolute moves. So hmm. Have to store our last motor positions I guess?

        auto n_axis = config->_axes->_numberAxis;
        float motors[n_axis];

        // First check whether we are moving in X/Y at all, if not the below won't make sense
        float XY_cartesian_distance = vector_distance(position, target, 2);
        if (XY_cartesian_distance == 0) {
            return mc_move_motors(target, pl_data);
        }

        // calc new heading
        //   angle between 2D points = atan2(y2 - y1, x2 - x1)
        float new_heading = atan2f((target[1]-position[1]),(target[0]-position[0])); // (radians)
        // angle diff
        float turn_angle = new_heading - m_heading;
        // To turn in place we move each wheel in opposite directions.
        // The distance covered by each wheel is given by the formula
        //      Dist = (angle_radians/2pi)*(wheelbase*pi) = angle*wheelbase/2
        float wheel_turn_dist = turn_angle * _distance_between_wheels / 2.0;
        // Now if we were controlling motors directly, here we'd use
        //      Revs = Dist / wheel_circumference = = Dist / 2*pi*_wheel_radius
        // BUT we want a motor movement in distance traveled, so I think the radius should be handled downstream?
        // Also note the below will always turn to the right. Depends how we calc the angle...
        motors[0] = wheel_turn_dist;
        motors[1] = -wheel_turn_dist;
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) {
            motors[axis] = 0.0; // dont move other axes during turn
        }
        // execute the turn and continue
        if (!mc_move_motors(motors, pl_data)) {
            return false;
        }
        // if we're still here, turn move completed so update current angle
        m_heading = new_heading;

        // Now just move forward the required distance to the target
        motors[0] = XY_cartesian_distance;
        motors[1] = XY_cartesian_distance;
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) { // pass through any other axis moves (like Z for pen)
            motors[axis] = target[axis];
        }
        return mc_move_motors(target, pl_data);
    }

    void DifferentialDrive::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Insert conversion from differential drive kinematics to cartesian here.

        // I don't think this is even possible for DiffDrive - our motor position does not tell us our location.
    }

    void DifferentialDrive::transform_cartesian_to_motors(float* cartesian, float* motors) {
        // Insert conversion from cartesian to differential drive kinematics here.

        // Some kinematics use this as a sub-function of cartesian_to_motors but as ours isn't one-to-one we may not use it.
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
