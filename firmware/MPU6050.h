#ifndef MPU6050_H
#define MPU6050_H

#include <Arduino.h>
#include <Wire.h>

// MPU6050 Register Addresses
#define MPU6050_ADDR 0x68
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H 0x43
#define MPU6050_CONFIG 0x1A
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C

// Range configurations
typedef enum {
  MPU6050_RANGE_2_G = 0,
  MPU6050_RANGE_4_G = 1,
  MPU6050_RANGE_8_G = 2,
  MPU6050_RANGE_16_G = 3
} mpu6050_accel_range_t;

typedef enum {
  MPU6050_RANGE_250_DEG = 0,
  MPU6050_RANGE_500_DEG = 1,
  MPU6050_RANGE_1000_DEG = 2,
  MPU6050_RANGE_2000_DEG = 3
} mpu6050_gyro_range_t;

typedef enum {
  MPU6050_BAND_260_HZ = 0,
  MPU6050_BAND_184_HZ = 1,
  MPU6050_BAND_94_HZ = 2,
  MPU6050_BAND_44_HZ = 3,
  MPU6050_BAND_21_HZ = 4,
  MPU6050_BAND_10_HZ = 5,
  MPU6050_BAND_5_HZ = 6
} mpu6050_bandwidth_t;

// Sensor event structure
typedef struct {
  float x, y, z;
} sensor_vec_t;

typedef struct {
  sensor_vec_t acceleration;
  sensor_vec_t gyro;
  float temperature;
} sensors_event_t;

class MPU6050 {
public:
  MPU6050();
  bool begin(uint8_t addr = MPU6050_ADDR);
  void setAccelerometerRange(mpu6050_accel_range_t range);
  void setGyroRange(mpu6050_gyro_range_t range);
  void setFilterBandwidth(mpu6050_bandwidth_t bandwidth);
  void getEvent(sensors_event_t *accel, sensors_event_t *gyro, sensors_event_t *temp);
  
private:
  uint8_t _i2caddr;
  mpu6050_accel_range_t _accel_range;
  mpu6050_gyro_range_t _gyro_range;
  
  void writeRegister(uint8_t reg, uint8_t value);
  uint8_t readRegister(uint8_t reg);
  void readRegisters(uint8_t reg, uint8_t *buffer, uint8_t len);
  int16_t readInt16(uint8_t reg);
};

#endif