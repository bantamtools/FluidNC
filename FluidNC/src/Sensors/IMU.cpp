// Copyright (c) 2023 Matt Staniszewski, Bantam Tools

#include "IMU.h"
#include "../Machine/MachineConfig.h"

TwoWire _i2c_port = TwoWire(1);

// IMU constructor
IMU::IMU() {

    // Allocate memory for IMU and its data
    _icm_20948 = new ICM_20948_I2C();
    _imu_data = new struct IMUDataType;
}

// IMU destructor
IMU::~IMU() {

    // Deallocate memory for the IMU and its data
    delete(_imu_data);
    delete(_icm_20948);
}

// Initializes the IMU subsystem
void IMU::init() {

    esp_err_t status = ESP_OK;

    // Initialize the data structure
    _imu_data->x      = 0;
    _imu_data->y      = 0;
    _imu_data->z      = 0;
    _imu_data->roll   = 0;
    _imu_data->pitch  = 0;

    // Start the IMU
    _icm_20948->begin(config->_i2c[_i2c_num], ((_i2c_address & 0x1) ? true : false));
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU begin failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    // Issue SW reset to start in known state
    _icm_20948->swReset();
    delay_ms(250);
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU swReset failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    // Wakey, wakey!
    _icm_20948->sleep(false);
    _icm_20948->lowPower(false);

    // Set accelerometer and gyro to continuous mode
    _icm_20948->setSampleMode((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), ICM_20948_Sample_Mode_Continuous);
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU setSampleMode failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    // Set full scale ranges for accelerometer and gyro
    ICM_20948_fss_t fss;
    fss.a = gpm2;
    fss.g = dps250;
    _icm_20948->setFullScale((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), fss);
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU setFullScale failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    // Set up and enable digital LPF configuration (variables are 3dB bandwidth and Nyquist bandwidth in Hz)
    ICM_20948_dlpcfg_t dlpf;
    dlpf.a = acc_d473bw_n499bw;
    dlpf.g = gyr_d361bw4_n376bw5;
    _icm_20948->setDLPFcfg((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), dlpf);
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU setDLPFcfg failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    _icm_20948->enableDLPF(ICM_20948_Internal_Acc, true);
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU accel enableDLPF failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    _icm_20948->enableDLPF(ICM_20948_Internal_Gyr, true);
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU gyro enableDLPF failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    // Start the magnetometer
    _icm_20948->startupMagnetometer();
    if (_icm_20948->status != ICM_20948_Stat_Ok) {
        log_warn("IMU startupMagnetometer failed with error: " << _icm_20948->statusString());
        status = ESP_FAIL;
    }

    // Print configuration info message if successful
    if (status == ESP_FAIL) {
        log_warn("IMU: Configuration failed");
    } else {
        log_info("IMU: I2C Number:" << _i2c_num << " Address:" << to_hex(_i2c_address) << 
            " INT:" << (_int_pin.defined() ? _int_pin.name() : "None"));
    }
}

// Reads the latest values from the IMU
void IMU::read() {

    // Read new values when an update is available
}

// Returns the current IMU data
struct IMUDataType* IMU::get_data() {
    return _imu_data;
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
