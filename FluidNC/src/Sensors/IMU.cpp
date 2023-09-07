// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "../Machine/MachineConfig.h"

TwoWire _i2c_port = TwoWire(1);

// IMU constructor
IMU::IMU() {

    // Allocate memory for IMU
    _icm_20948 = new ICM_20948_I2C();
}

// IMU destructor
IMU::~IMU() {

    // Deallocate memory for the IMU
    delete(_icm_20948);
}

// Initializes the IMU subsystem
void IMU::init() {

    bool success = true; // Use success to show if the DMP configuration was successful

    // Start the IMU
    success &= (_icm_20948->begin(config->_i2c[_i2c_num], ((_i2c_address & 0x1) ? true : false)) == ICM_20948_Stat_Ok);

    // Initialize the DMP
    success &= (_icm_20948->initializeDMP() == ICM_20948_Stat_Ok);

    // Enable the DMP orientation sensor
    success &= (_icm_20948->enableDMPSensor(INV_ICM20948_SENSOR_ORIENTATION) == ICM_20948_Stat_Ok);

    // Configure DMP to output data at maximum ODR
    success &= (_icm_20948->setDMPODRrate(DMP_ODR_Reg_Quat9, 0) == ICM_20948_Stat_Ok);

    // Enable the FIFO
    success &= (_icm_20948->enableFIFO() == ICM_20948_Stat_Ok);

    // Enable the DMP
    success &= (_icm_20948->enableDMP() == ICM_20948_Stat_Ok);

    // Reset DMP
    success &= (_icm_20948->resetDMP() == ICM_20948_Stat_Ok);

    // Reset FIFO
    success &= (_icm_20948->resetFIFO() == ICM_20948_Stat_Ok);

    // Print configuration info message if successful
    if (!success) {
        log_warn("IMU: Configuration failed");
    } else {
        log_info("IMU (DMP Enabled): I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
            " INT:" << (_int_pin.defined() ? _int_pin.name() : "None"));
    }
}

// Reads the latest values from the IMU
void IMU::read() {
    
    icm_20948_DMP_data_t data;
    
    // Read the DMP FIFO for data
    _icm_20948->readDMPdataFromFIFO(&data);

    // Valid data available, process it
    if ((_icm_20948->status == ICM_20948_Stat_Ok) || 
        (_icm_20948->status == ICM_20948_Stat_FIFOMoreDataAvail)) {
        
        // It should be orientation data, but let's check...
        if ((data.header & DMP_header_bitmap_Quat9) > 0) {

            // Q0 value is computed from this equation: Q0^2 + Q1^2 + Q2^2 + Q3^2 = 1.
            // In case of drift, the sum will not add to 1, therefore, quaternion data 
            // needs to be corrected with right bias values. The quaternion data is 
            // scaled by 2^30.
            
            // Save off quaternion data, scaled to +/- 1
            q[1] = ((double)data.Quat9.Data.Q1) / 1073741824.0; // Convert to double. Divide by 2^30
            q[2] = ((double)data.Quat9.Data.Q2) / 1073741824.0; // Convert to double. Divide by 2^30
            q[3] = ((double)data.Quat9.Data.Q3) / 1073741824.0; // Convert to double. Divide by 2^30
            q[0] = sqrt(1.0 - ((q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3])));

            // Save off accuracy
            accuracy = data.Quat9.Data.Accuracy;
        }
    }
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
