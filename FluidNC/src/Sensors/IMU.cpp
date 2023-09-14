// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "../Machine/MachineConfig.h"

// IMU constructor
IMU::IMU() {

    // Allocate memory for IMU
    _mpu_6050 = new MPU6050(config->_i2c[_i2c_num], _i2c_address);

    // Mark DMP uninitialized
    _dmp_ready = false;
}

// IMU destructor
IMU::~IMU() {

    // Deallocate memory for the IMU
    delete(_mpu_6050);

    // Mark DMP uninitialized
    _dmp_ready = false;
}

// Initializes the IMU subsystem
void IMU::init() {

    bool success = false; // Use success to show if IMU connection was successful
    uint8_t dmp_status = 1;

    // Initialize the IMU data structure
    _imu_data.yaw = 0.0;
    _imu_data.pitch = 0.0;
    _imu_data.roll = 0.0;

    // Initialize the IMU
    _mpu_6050->initialize();

    // Verify connection
    success = _mpu_6050->testConnection();

    // Print configuration info message if successful
    if (!success) {
        log_warn("IMU: Connection failed");
    } else {
        log_info("IMU: I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
            " INT:" << (_int_pin.defined() ? _int_pin.name() : "None"));
    }

    // Load and configure the DMP
    dmp_status = _mpu_6050->dmpInitialize();

    // Initialized, now calibrate and enable DMP...
    if (dmp_status == 0) {

        // Calibration time, generate offsets and calibrate
        _mpu_6050->CalibrateAccel(10);
        _mpu_6050->CalibrateGyro(10);
        _mpu_6050->PrintActiveOffsets();

        // Turn on the DMP now that it's ready
        _mpu_6050->setDMPEnabled(true);

        // Set ready flag and print success message
        _dmp_ready = true;
        log_info("IMU: DMP initialized!")

    // Error, print message
    } else {

        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        log_warn("IMU: DMP initialization failed, error code = " << dmp_status)
    }
}

// Reads the latest values from the IMU
void IMU::read() {

    uint8_t fifo_buffer[64];    // FIFO storage buffer
    Quaternion q;               // [w, x, y, z]         quaternion container
    VectorFloat gravity;        // [x, y, z]            gravity vector
    float ypr[3];               // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

    // Packet structure for InvenSense teapot demo
    uint8_t teapot_packet[14] = { '$', 0x02, 0,0, 0,0, 0,0, 0,0, 0x00, 0x00, '\r', '\n' };

    // DMP not ready, exit
    if (!_dmp_ready) return;

    // Obtain the lock
    _mutex.lock();

    // DEBUG: Read and display raw accel/gyro measurements from IMU
    //int16_t ax, ay, az, gx, gy, gz;
    //_mpu_6050->getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    //log_info("a/g: [" << ax << " " << ay << " " << az << "]  [" << gx << " " << gy << " " << gz << "]");
    //delay_ms(100);

    // Read a DMP packet from FIFO
    if (_mpu_6050->dmpGetCurrentFIFOPacket(fifo_buffer)) { // Get the Latest packet 

        // Get yaw/pitch/roll data
        _mpu_6050->dmpGetQuaternion(&q, fifo_buffer);
        _mpu_6050->dmpGetGravity(&gravity, &q);
        _mpu_6050->dmpGetYawPitchRoll(ypr, &q, &gravity);

        // Store yaw/pitch/roll angles locally in degrees
        _imu_data.yaw   = ypr[0] * 180/M_PI;
        _imu_data.pitch = ypr[1] * 180/M_PI;
        _imu_data.roll  = ypr[2] * 180/M_PI;

        // DEBUG: Display yaw/pitch/roll angles in degrees
        //log_info("ypr: [" << _imu_data.yaw << " " << _imu_data.pitch << " " << _imu_data.roll << "]");
        //delay_ms(100);

        // DEBUG: Output for InvenSense Teapot (plane) demo Processing animation
        // Ref: https://github.com/ElectronicCats/mpu6050/tree/master/examples/MPU6050_DMP6/Processing/MPUTeapot
        // Requires Processing app: https://processing.org/download AND
        // Requires ToxicLibs: https://github.com/postspectacular/toxiclibs/releases (download to [userdir]/Processing/libraries)
        //teapot_packet[2] = fifo_buffer[0];
        //teapot_packet[3] = fifo_buffer[1];
        //teapot_packet[4] = fifo_buffer[4];
        //teapot_packet[5] = fifo_buffer[5];
        //teapot_packet[6] = fifo_buffer[8];
        //teapot_packet[7] = fifo_buffer[9];
        //teapot_packet[8] = fifo_buffer[12];
        //teapot_packet[9] = fifo_buffer[13];
        //Serial.write(teapot_packet, 14);
        //teapot_packet[11]++;    // Packet count, loops at 0xFF on purpose
    }

    // Return the lock
    _mutex.unlock();
}

// Configurable functions
void IMU::validate() {

    if (!config->_i2c[_i2c_num]) {
        Assert((config->_i2c[_i2c_num]), "IMU I2C section [i2c%d] not defined.", _i2c_num);
    }

    if (!_int_pin.undefined()) {
        Assert(!_int_pin.undefined(), "IMU INT pin should be configured.");
    }
}

void IMU::group(Configuration::HandlerBase& handler) {

    handler.item("i2c_num", _i2c_num);
    handler.item("i2c_address", _i2c_address);

    handler.item("int_pin", _int_pin);
}
