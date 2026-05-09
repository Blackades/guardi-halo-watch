#ifndef MAX30105_H
#define MAX30105_H

#include <Arduino.h>
#include <Wire.h>

// MAX30105 I2C Address
#define MAX30105_ADDRESS 0x57

// MAX30105 Register Addresses
#define MAX30105_INTR_STATUS_1 0x00
#define MAX30105_INTR_STATUS_2 0x01
#define MAX30105_INTR_ENABLE_1 0x02
#define MAX30105_INTR_ENABLE_2 0x03
#define MAX30105_FIFO_WR_PTR 0x04
#define MAX30105_OVF_COUNTER 0x05
#define MAX30105_FIFO_RD_PTR 0x06
#define MAX30105_FIFO_DATA 0x07
#define MAX30105_FIFO_CONFIG 0x08
#define MAX30105_MODE_CONFIG 0x09
#define MAX30105_SPO2_CONFIG 0x0A
#define MAX30105_LED1_PULSEAMP 0x0C
#define MAX30105_LED2_PULSEAMP 0x0D
#define MAX30105_LED3_PULSEAMP 0x0E
#define MAX30105_PILOT_PA 0x10
#define MAX30105_MULTI_LED_CTRL1 0x11
#define MAX30105_MULTI_LED_CTRL2 0x12
#define MAX30105_TEMP_INTR 0x1F
#define MAX30105_TEMP_FRAC 0x20
#define MAX30105_TEMP_CONFIG 0x21
#define MAX30105_PROX_INT_THRESH 0x30
#define MAX30105_REV_ID 0xFE
#define MAX30105_PART_ID 0xFF

// Configuration values
#define MAX30105_SAMPLEAVG_1 0x00
#define MAX30105_SAMPLEAVG_2 0x20
#define MAX30105_SAMPLEAVG_4 0x40
#define MAX30105_SAMPLEAVG_8 0x60
#define MAX30105_SAMPLEAVG_16 0x80
#define MAX30105_SAMPLEAVG_32 0xA0

#define MAX30105_ROLLOVER_ENABLE 0x10
#define MAX30105_ROLLOVER_DISABLE 0x00

#define MAX30105_A_FULL_MASK 0x0F

#define MAX30105_SHUTDOWN 0x80
#define MAX30105_RESET 0x40
#define MAX30105_MODE_REDONLY 0x02
#define MAX30105_MODE_REDIRONLY 0x03
#define MAX30105_MODE_MULTILED 0x07

#define MAX30105_ADCRANGE_2048 0x00
#define MAX30105_ADCRANGE_4096 0x20
#define MAX30105_ADCRANGE_8192 0x40
#define MAX30105_ADCRANGE_16384 0x60

#define MAX30105_SAMPLERATE_50 0x00
#define MAX30105_SAMPLERATE_100 0x04
#define MAX30105_SAMPLERATE_200 0x08
#define MAX30105_SAMPLERATE_400 0x0C
#define MAX30105_SAMPLERATE_800 0x10
#define MAX30105_SAMPLERATE_1000 0x14
#define MAX30105_SAMPLERATE_1600 0x18
#define MAX30105_SAMPLERATE_3200 0x1C

#define MAX30105_PULSEWIDTH_69 0x00
#define MAX30105_PULSEWIDTH_118 0x01
#define MAX30105_PULSEWIDTH_215 0x02
#define MAX30105_PULSEWIDTH_411 0x03

class MAX30105 {
public:
  MAX30105();
  bool begin(uint8_t powerLevel = 0x1F, uint8_t sampleAverage = 4, uint8_t ledMode = 3, int sampleRate = 400, int pulseWidth = 411, int adcRange = 4096);
  
  uint32_t getRed();
  uint32_t getIR();
  uint32_t getGreen();
  bool available();
  void nextSample();
  uint16_t check();
  
  void setup(uint8_t powerLevel = 0x1F, uint8_t sampleAverage = 4, uint8_t ledMode = 3, int sampleRate = 400, int pulseWidth = 411, int adcRange = 4096);
  void setPulseAmplitudeRed(uint8_t amplitude);
  void setPulseAmplitudeIR(uint8_t amplitude);
  void setPulseAmplitudeGreen(uint8_t amplitude);
  void setPulseAmplitudeProximity(uint8_t amplitude);
  
  void shutDown();
  void wakeUp();
  void softReset();
  
  void enableAFULL();
  void disableAFULL();
  void enableDATARDY();
  void disableDATARDY();
  void enableALCOVF();
  void disableALCOVF();
  void enablePROXINT();
  void disablePROXINT();
  void enableDIETEMPRDY();
  void disableDIETEMPRDY();
  
  uint8_t getINT1();
  uint8_t getINT2();
  
  void clearFIFO();
  
  void setFIFOAverage(uint8_t numberOfSamples);
  void enableFIFORollover();
  void disableFIFORollover();
  void setFIFOAlmostFull(uint8_t numberOfSamples);
  
  uint8_t getWritePointer();
  uint8_t getReadPointer();
  
  float readTemperature();
  float readTemperatureF();
  
  uint8_t getRevisionID();
  uint8_t readPartID();
  
  void bitMask(uint8_t reg, uint8_t mask, uint8_t thing);
  
private:
  uint8_t _i2caddr;
  
  // FIFO variables
  struct Record {
    uint32_t red[32];
    uint32_t IR[32];
    uint32_t green[32];
    uint8_t head;
    uint8_t tail;
  } sense;
  
  void writeRegister8(uint8_t reg, uint8_t value);
  uint8_t readRegister8(uint8_t reg);
  void readRegister24(uint8_t reg, uint32_t *value);
};

#endif