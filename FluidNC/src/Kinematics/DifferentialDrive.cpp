#include "DifferentialDrive.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"
#include "src/Limits.h"
#include "src/Machine/Homing.h"

namespace Kinematics {
    void DifferentialDrive::group(Configuration::HandlerBase& handler) {
        handler.item("left_motor_axis", _left_motor_axis);
        handler.item("right_motor_axis", _right_motor_axis);
        //handler.item("wheel_radius", _wheel_radius);  // this is folded into the motor config's steps/mm
        handler.item("distance_between_wheels", _distance_between_wheels);
        handler.item("use_z_delay", _use_z_delay);
        handler.item("z_up_min_angle", _z_up_min_angle);
        log_info("DD wheel distance: " << _distance_between_wheels);
        log_info("Turtle Z handling on: " << _use_z_delay);
    }

    void DifferentialDrive::init() {
        log_info("Kinematic system: " << name());
        m_heading = 0.0; // define current heading as zero angle (note this faces down positive X)
        m_motor_left = 0.0;
        m_motor_right = 0.0;
        m_prev_x = 0.0;
        m_prev_y = 0.0;
        m_next_x = 0.0;
        m_next_y = 0.0;
        m_prev_left = 0.0;
        m_prev_right = 0.0;
        m_next_left = 0.0;
        m_next_right = 0.0;
        m_left_last_report = 0.0;
        m_right_last_report = 0.0;
        m_have_captured_z = false;
        m_captured_z_target = 0.0;
        m_captured_z_prev = 0.0;
        m_captured_z_pldata = NULL;
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

        auto n_axis = config->_axes->_numberAxis;
        float motors[n_axis];

        // First check whether we are moving in X/Y at all, if not the equations below won't make sense
        float XY_cartesian_distance = vector_distance(position, target, 2);
        if (XY_cartesian_distance == 0) {
            motors[X_AXIS] = m_motor_left; // don't move from current position
            motors[Y_AXIS] = m_motor_right;
            if (_use_z_delay && (target[Z_AXIS] < position[Z_AXIS])) { // capture Z-down only move and save for later
                motors[Z_AXIS] = position[Z_AXIS]; // don't move
                m_captured_z_target = target[Z_AXIS];
                m_captured_z_prev = position[Z_AXIS];
                m_captured_z_pldata = pl_data;
                m_have_captured_z = true;
            } else {
                motors[Z_AXIS] = target[Z_AXIS];
                if (target[Z_AXIS] > position[Z_AXIS]) {
                    // this is a Z up move; clear Z capture state until next Z down move
                    m_have_captured_z = false;
                }
            }
            for (size_t axis = Z_AXIS+1; axis < n_axis; axis++) { // pass through any remaining axis moves
                motors[axis] = target[axis];
            }
            return mc_move_motors(motors, pl_data);
        }
        // finished with no X/Y movement case

        // record starting motor positions and cartesian start and end
        m_prev_left = m_motor_left;
        m_prev_right = m_motor_right;
        m_prev_x = position[X_AXIS];
        m_prev_y = position[Y_AXIS];
        m_next_x = target[X_AXIS];
        m_next_y = target[Y_AXIS];

        // calc new heading
        //   angle between 2D points = atan2(y2 - y1, x2 - x1)
        float new_heading = atan2f((target[Y_AXIS]-position[Y_AXIS]),(target[X_AXIS]-position[X_AXIS])); // (radians)
        // angle diff
        float turn_angle = new_heading - m_heading;
        // Note this would turn in an arbitrary direction based on what atan gave us; may turn 270 when it could turn 90.
        if (turn_angle > PI) { turn_angle -= 2.0*PI; }
        if (turn_angle < -PI) { turn_angle += 2.0*PI; }
        // Now it should be within +- 180
        // NotTodo: Possible optimization: if angle beyond +/-90, could turn the supplementary and move backward - but some media may not draw well backward

        bool leaving_z_down = false;
        if (m_have_captured_z && (abs(turn_angle) >= _z_up_min_angle*PI/180.0)) {
            // if we're turning farther than the cutoff, return Z to previous Up position first
            motors[X_AXIS] = m_motor_left; // don't move XY from current position
            motors[Y_AXIS] = m_motor_right;
            motors[Z_AXIS] = m_captured_z_prev; // "prev" is recent up Z from before last Z down move
            for (size_t axis = Z_AXIS+1; axis < n_axis; axis++) {
                motors[axis] = position[axis]; // keep anything else at current values for this extra move
            }
            if (!mc_move_motors(motors, m_captured_z_pldata)) { // execute Z up move
                return false;
            }
        } else {
            leaving_z_down = true; // note that we're leaving Z down during a short turn
        }

        // To turn in place we move each wheel in opposite directions.
        // The distance covered by each wheel is given by the formula
        //      Dist = (angle_radians/2pi)*(wheelbase*pi) = angle*wheelbase/2
        float wheel_turn_dist = turn_angle * _distance_between_wheels / 2.0;
        // Now if we were controlling motors directly, here we'd use
        //      Revs = Dist / wheel_circumference = = Dist / 2*pi*_wheel_radius
        // BUT we want a motor movement in distance traveled; the radius should be handled by config steps/mm.
        // Meanwhile, we need to convert these relative distance movements to absolute targets
        float left_target = m_motor_left + wheel_turn_dist;
        float right_target = m_motor_right - wheel_turn_dist;
        motors[X_AXIS] = left_target;
        motors[Y_AXIS] = right_target;
        if (m_have_captured_z && !leaving_z_down) { // if we're holding Z until after turn, keep it at previous value here
            motors[Z_AXIS] = m_captured_z_prev;
        } else {
            motors[Z_AXIS] = target[Z_AXIS];
        }
        for (size_t axis = Z_AXIS+1; axis < n_axis; axis++) { // pass through any other axis positions
            motors[axis] = target[axis];
        }
        // execute the turn and continue
        if (!mc_move_motors(motors, pl_data)) {
            return false;
        }
        // if we're still here, turn move was accepted so update accepted state
        m_heading = new_heading;
        m_motor_left = left_target;
        m_motor_right = right_target;

        // if we have a previously captured Z-down move, we execute it here after the turn but before the traverse
        if(m_have_captured_z) {
            motors[X_AXIS] = m_motor_left; // no move
            motors[Y_AXIS] = m_motor_right; // no move
            motors[Z_AXIS] = m_captured_z_target;
            //m_have_captured_z = false; // clear flag preemptively so we don't try to do this again
            // note - now keeping captured Z to raise for following large turns, until next explicit Z up
            if (!mc_move_motors(motors, m_captured_z_pldata)) { // execute Z down move
                return false;
            }
        }

        // set starting motor positions for straight move reporting
        m_prev_left = m_motor_left;
        m_prev_right = m_motor_right;
        m_left_last_report = m_motor_left;
        m_right_last_report = m_motor_right;

        // Now just move forward the required distance to the target
        m_next_left = m_motor_left + XY_cartesian_distance;
        m_next_right = m_motor_right + XY_cartesian_distance;
        motors[X_AXIS] = m_next_left;
        motors[Y_AXIS] = m_next_right;
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) { // pass through any other axis moves
            motors[axis] = target[axis];
        }
        if (!mc_move_motors(motors, pl_data)) {
            return false;
        }
        // update accepted state
        m_motor_left = m_next_left;
        m_motor_right = m_next_right;
        return true;
    }

    void DifferentialDrive::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Insert conversion from differential drive kinematics to cartesian here.

        // We cannot derive cartesian position from motors in a vacuum, because it's state dependent.
        // However, we want this so the WebUI shows position. Solution will have to be to keep up a running
        //  cartesian XY position state. During linear moves, we'll have to store the starting motor pos and interpolate
        //  to the current motor pos based on the start and end points. Gonna need lots of state.

        // First pass for testing, we're just going to report the position after the last processed move
        // Note this just set reports to the next target as soon as a move set began, not great
//        cartesian[X_AXIS] = m_next_x;
//        cartesian[Y_AXIS] = m_next_y;
        // now with more state, interpolate motor progress into cartesian progress
        // I expect this to work during straight moves but turns will report nonsense... and that is indeed the case.

        // idea: we always turn first then move. During turn, X/Y should not change at all (ideally).
        //  When we are turning, the motors are moving in opposite directions.
        //  So, still more state for previous in-progress motors report, compare to current, if XY in opp dirs,
        //  then just return m_prev_xy, if same, do the distance interpolation...

        bool xdir = motors[X_AXIS] > m_left_last_report;
        bool ydir = motors[Y_AXIS] > m_right_last_report;

        if (xdir != ydir) {
            // motors moving opposite directions, we're in turning stage, XY should not change yet
            cartesian[X_AXIS] = m_prev_x;
            cartesian[Y_AXIS] = m_prev_y;
            for (size_t axis = Z_AXIS; axis < n_axis; axis++) { // pass through any other axes
                cartesian[axis] = motors[axis];
            }
            m_left_last_report = motors[X_AXIS];
            m_right_last_report = motors[Y_AXIS];
            return;
        }

        // else motors are moving the same direction, we're travelling
        // this bit is the linear move interpolation (with an escape clause for zero-length moves)
        if (m_next_left-m_prev_left == 0) {
            cartesian[X_AXIS] = m_next_x;
        } else {
            cartesian[X_AXIS] = m_prev_x + ((motors[X_AXIS]-m_prev_left)/(m_next_left-m_prev_left))*(m_next_x-m_prev_x);
        }
        if (m_next_right-m_prev_right == 0) {
            cartesian[Y_AXIS] = m_next_y;
        } else {
            cartesian[Y_AXIS] = m_prev_y + ((motors[Y_AXIS]-m_prev_right)/(m_next_right-m_prev_right))*(m_next_y-m_prev_y);
        }
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) { // pass through any other axes
            cartesian[axis] = motors[axis];
        }
        m_left_last_report = motors[X_AXIS];
        m_right_last_report = motors[Y_AXIS];
    }

    void DifferentialDrive::transform_cartesian_to_motors(float* cartesian, float* motors) {
        // Insert conversion from cartesian to differential drive kinematics here.

        // Some kinematics use this as a sub-function of cartesian_to_motors but as ours isn't one-to-one we may not use it.
    }

    void DifferentialDrive::imu_update() {

        // Trigger an IMU read
        config->_sensors->_imu->read();
        
        // Obtain the IMU lock (prevents read updating values during calculations)
        config->_sensors->_imu->_mutex.lock();

        // TODO: Do something with the data...for now we just print it out
        log_info("ypr: [" << config->_sensors->_imu->_imu_data.yaw << " " 
                          << config->_sensors->_imu->_imu_data.pitch << " " 
                          << config->_sensors->_imu->_imu_data.roll << "]");

        // Return the IMU lock
        config->_sensors->_imu->_mutex.unlock();
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
