#pragma once

#include "Kinematics.h"

namespace Kinematics {
    class DifferentialDrive : public KinematicSystem {
    public:
        DifferentialDrive() = default;

        DifferentialDrive(const DifferentialDrive&)            = delete;
        DifferentialDrive(DifferentialDrive&&)                 = delete;
        DifferentialDrive& operator=(const DifferentialDrive&) = delete;
        DifferentialDrive& operator=(DifferentialDrive&&)      = delete;

        virtual bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        virtual void init() override;
        virtual void init_position() override;
        virtual void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        virtual void imu_update() override;
        
        virtual void transform_cartesian_to_motors(float* motors, float* cartesian) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;

        void afterParse() override {}
        virtual void group(Configuration::HandlerBase& handler) override;
        void validate() override {}

        const char* name() const override { return "DifferentialDrive"; }

    protected:
        ~DifferentialDrive() {}

    private:

        // State
        float m_heading; // last accepted forward angle (radians)
        float m_motor_left; // last accepted left (usually x) motor position, in mm of motion
        float m_motor_right; // last accepted right (usually y) motor position
        // also track cartesian position so we can report it back to FluidNC
        float m_prev_x;
        float m_prev_y;
        float m_next_x;
        float m_next_y;
        // we'll also need some motor position state to update cartesian pos during moves...
        float m_prev_left; // motor start
        float m_prev_right;
        float m_next_left; // motor finish
        float m_next_right;
        float m_left_last_report; // latest motor pos while move in progress
        float m_right_last_report;
        // state for capturing and delaying Z-down moves
        bool m_have_captured_z;
        float m_captured_z_target;
        float m_captured_z_prev;
        plan_line_data_t* m_captured_z_pldata;
        // state for turn-balancing
        float m_total_turn_angle;


        // Parameters
        int _left_motor_axis    = 0;
        int _right_motor_axis   = 1;
        //float _wheel_radius     = 20.0;
        float _distance_between_wheels = 50.0;
        bool _use_z_delay = false; // attempt to delay Z down moves (engage pen/media) until after in-place turns
        float _z_up_min_angle = 30; // raise Z to prev height during turns, for angles greater than this
        bool _use_turn_balancing = false; // keep total turn angle from accumulating, to reduce angular error buildup

    };
}
