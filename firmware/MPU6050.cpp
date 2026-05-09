#include "MPU6050.h"

MPU6050::MPU6050() {
  _i2caddr = MPU6050_ADDR;
  _accel_range = MPU6050_RANGE_2_G;
  _gyro_range = MPU6050_RANGE_250_DEG;
}

bool MPU6050::begin(uint8_t addr) {
  _i2caddr = addr;
  
  // Check if device is present
  Wire.beginTransmission(_i2caddr);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  
  // Wake up the MPU6050
  writeRegister(MPU6050_PWR_MGMT_1, 0x00);
  delay(100);
  
  // Set default ranges
  setAccelerometerRange(MPU6050_RANGE_2_G);
  setGyroRange(MPU6050_RANGE_250_DEG);
  setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  return true;
}

void MPU6050::setAccelerometerRange(mpu6050_accel_range_t range) {
  _accel_range = range;
  writeRegister(MPU6050_ACCEL_CONFIG, range << 3);
}

void MPU6050::setGyroRange(mpu6050_gyro_range_t range) {
  _gyro_range = range;
  writeRegister(MPU6050_GYRO_CONFIG, range << 3);
}

void MPU6050::setFilterBandwidth(mpu6050_bandwidth_t bandwidth) {
  writeRegister(MPU6050_CONFIG, bandwidth);
}

void MPU6050::getEvent(sensors_event_t *accel, sensors_event_t *gyro, sensors_event_t *temp) {
  uint8_t buffer[14];
  readRegisters(MPU6050_ACCEL_XOUT_H, buffer, 14);
  
  // Parse accelerometer data
  int16_t ax = (buffer[0] << 8) | buffer[1];
  int16_t ay = (buffer[2] << 8) | buffer[3];
  int16_t az = (buffer[4] << 8) | buffer[5];
  
  // Parse temperature data
  int16_t temp_raw = (buffer[6] << 8) | buffer[7];
  
  // Parse gyroscope data
  int16_t gx = (buffer[8] << 8) | buffer[9];
  int16_t gy = (buffer[10] << 8) | buffer[11];
  int16_t gz = (buffer[12] << 8) | buffer[13];
  
  // Convert to physical units
  float accel_scale;
  switch (_accel_range) {
    case MPU6050_RANGE_2_G: accel_scale = 16384.0; break;
    case MPU6050_RANGE_4_G: accel_scale = 8192.0; break;
    case MPU6050_RANGE_8_G: accel_scale = 4096.0; break;
    case MPU6050_RANGE_16_G: accel_scale = 2048.0; break;
  }
  
  float gyro_scale;
  switch (_gyro_range) {
    case MPU6050_RANGE_250_DEG: gyro_scale = 131.0; break;
    case MPU6050_RANGE_500_DEG: gyro_scale = 65.5; break;
    case MPU6050_RANGE_1000_DEG: gyro_scale = 32.8; break;
    case MPU6050_RANGE_2000_DEG: gyro_scale = 16.4; break;
  }
  
  accel->acceleration.x = ax / accel_scale * 9.81; // Convert to m/s²
  accel->acceleration.y = ay / accel_scale * 9.81;
  accel->acceleration.z = az / accel_scale * 9.81;
  
  gyro->gyro.x = gx / gyro_scale * PI / 180.0; // Convert to rad/s
  gyro->gyro.y = gy / gyro_scale * PI / 180.0;
  gyro->gyro.z = gz / gyro_scale * PI / 180.0;
  
  temp->temperature = temp_raw / 340.0 + 36.53; // Convert to Celsius
}

void MPU6050::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t MPU6050::readRegister(uint8_t reg) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(_i2caddr, (uint8_t)1);
  return Wire.read();
}

void MPU6050::readRegisters(uint8_t reg, uint8_t *buffer, uint8_t len) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(_i2caddr, len);
  
  for (uint8_t i = 0; i < len; i++) {
    buffer[i] = Wire.read();
  }
}