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
        float m_heading; // current forward angle (radians)
        float m_motor_left; // current left (x?) motor position, in mm of motion
        float m_motor_right; // current right (y?) motor position
        // TODO: also track cartesian position so we can report it back to FluidNC
        float m_cartesian_x;
        float m_cartesian_y;
        // we'll also need some motor position state to track cartesian pos during moves...

        // Parameters
        int _left_motor_axis    = 0;
        int _right_motor_axis   = 1;
        float _wheel_radius     = 20.0;
        float _distance_between_wheels = 50.0;

    };
}
