/* List of files and sections to place in RAM instead of FLASH */
/* See README.md in ../esp32 for a complete explanation */

/* Only vtables (_ZTV) and typeinfo (_ZTI, _ZTS) are pulled into DRAM.
 * String literals (.rodata.*.str1.1) and any other rodata of these objects
 * stay in flash; the IRAM_ATTR functions in these files reference strings
 * only on Assert/log_error failure paths, which already call flash code.
 * -fno-jump-tables and -fno-tree-switch-conversion are set framework-wide,
 * so no switch tables are emitted.  If an IRAM function here ever needs a
 * const table, add its own .rodata.<mangled name> entry below.
 *
 * .xt.prop is deliberately NOT listed.  It is a non-allocated Xtensa
 * property table used only by the linker and debuggers; listing it here
 * made it occupy ~25 KB of loaded DRAM for no runtime purpose. */

    *Laser.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)

    /* All files whose name ends with Spindle.cpp */
    **Spindle.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)

    /* Motors */
    *Motor.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *Dynamixel2.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *MotorDriver.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *NullMotor.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *RcServo.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *Servo.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *Solenoid.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *StandardStepper.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *StepStick.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *TMC*Driver.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *TrinamicBase.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *TrinamicSpiDriver.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *TrinamicUartDriver.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *UnipolarMotor.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)

    /* Pin Details */
    **Detail.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *PwmPin.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *Pin.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *PinExtenderDriver.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
    *Extender.cpp.o(.rodata._ZTV* .rodata._ZTI* .rodata._ZTS*)
