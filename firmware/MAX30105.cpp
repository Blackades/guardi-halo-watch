#include "MAX30105.h"

MAX30105::MAX30105() {
  _i2caddr = MAX30105_ADDRESS;
}

bool MAX30105::begin(uint8_t powerLevel, uint8_t sampleAverage, uint8_t ledMode, int sampleRate, int pulseWidth, int adcRange) {
  _i2caddr = MAX30105_ADDRESS;
  
  // Check if device is present
  Wire.beginTransmission(_i2caddr);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  
  // Check part ID
  if (readPartID() != 0x15) {
    return false;
  }
  
  setup(powerLevel, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  
  return true;
}

void MAX30105::setup(uint8_t powerLevel, uint8_t sampleAverage, uint8_t ledMode, int sampleRate, int pulseWidth, int adcRange) {
  softReset(); // Reset all configuration, threshold, and data registers to POR values
  
  // FIFO Configuration
  if (sampleAverage == 1) setFIFOAverage(MAX30105_SAMPLEAVG_1);
  else if (sampleAverage == 2) setFIFOAverage(MAX30105_SAMPLEAVG_2);
  else if (sampleAverage == 4) setFIFOAverage(MAX30105_SAMPLEAVG_4);
  else if (sampleAverage == 8) setFIFOAverage(MAX30105_SAMPLEAVG_8);
  else if (sampleAverage == 16) setFIFOAverage(MAX30105_SAMPLEAVG_16);
  else if (sampleAverage == 32) setFIFOAverage(MAX30105_SAMPLEAVG_32);
  else setFIFOAverage(MAX30105_SAMPLEAVG_4);
  
  enableFIFORollover();
  
  // Mode Configuration
  if (ledMode == 2) writeRegister8(MAX30105_MODE_CONFIG, MAX30105_MODE_REDONLY);
  else if (ledMode == 3) writeRegister8(MAX30105_MODE_CONFIG, MAX30105_MODE_REDIRONLY);
  else writeRegister8(MAX30105_MODE_CONFIG, MAX30105_MODE_MULTILED);
  
  // SpO2 Configuration
  uint8_t adcRange_reg;
  if (adcRange < 4096) adcRange_reg = MAX30105_ADCRANGE_2048;
  else if (adcRange < 8192) adcRange_reg = MAX30105_ADCRANGE_4096;
  else if (adcRange < 16384) adcRange_reg = MAX30105_ADCRANGE_8192;
  else adcRange_reg = MAX30105_ADCRANGE_16384;
  
  uint8_t sampleRate_reg;
  if (sampleRate < 100) sampleRate_reg = MAX30105_SAMPLERATE_50;
  else if (sampleRate < 200) sampleRate_reg = MAX30105_SAMPLERATE_100;
  else if (sampleRate < 400) sampleRate_reg = MAX30105_SAMPLERATE_200;
  else if (sampleRate < 800) sampleRate_reg = MAX30105_SAMPLERATE_400;
  else if (sampleRate < 1000) sampleRate_reg = MAX30105_SAMPLERATE_800;
  else if (sampleRate < 1600) sampleRate_reg = MAX30105_SAMPLERATE_1000;
  else if (sampleRate < 3200) sampleRate_reg = MAX30105_SAMPLERATE_1600;
  else sampleRate_reg = MAX30105_SAMPLERATE_3200;
  
  uint8_t pulseWidth_reg;
  if (pulseWidth < 118) pulseWidth_reg = MAX30105_PULSEWIDTH_69;
  else if (pulseWidth < 215) pulseWidth_reg = MAX30105_PULSEWIDTH_118;
  else if (pulseWidth < 411) pulseWidth_reg = MAX30105_PULSEWIDTH_215;
  else pulseWidth_reg = MAX30105_PULSEWIDTH_411;
  
  writeRegister8(MAX30105_SPO2_CONFIG, adcRange_reg | sampleRate_reg | pulseWidth_reg);
  
  // LED Pulse Amplitude Configuration
  setPulseAmplitudeRed(powerLevel);
  setPulseAmplitudeIR(powerLevel);
  setPulseAmplitudeGreen(powerLevel);
  setPulseAmplitudeProximity(powerLevel);
  
  // Multi-LED Mode Configuration
  if (ledMode == 3) {
    writeRegister8(MAX30105_MULTI_LED_CTRL1, 0x21); // Slot 1 = Red LED, Slot 2 = IR LED
    writeRegister8(MAX30105_MULTI_LED_CTRL2, 0x00); // Slot 3 and 4 are off
  }
  
  clearFIFO();
}

uint32_t MAX30105::getRed() {
  if (sense.head == sense.tail) return 0;
  
  uint32_t temp = sense.red[sense.tail];
  sense.tail++;
  sense.tail %= 32;
  
  return temp;
}

uint32_t MAX30105::getIR() {
  if (sense.head == sense.tail) return 0;
  
  uint32_t temp = sense.IR[sense.tail];
  sense.tail++;
  sense.tail %= 32;
  
  return temp;
}

uint32_t MAX30105::getGreen() {
  if (sense.head == sense.tail) return 0;
  
  uint32_t temp = sense.green[sense.tail];
  sense.tail++;
  sense.tail %= 32;
  
  return temp;
}

bool MAX30105::available() {
  return (sense.head != sense.tail);
}

void MAX30105::nextSample() {
  if (available()) {
    sense.tail++;
    sense.tail %= 32;
  }
}

uint16_t MAX30105::check() {
  uint8_t readPointer = getReadPointer();
  uint8_t writePointer = getWritePointer();
  
  int numberOfSamples = 0;
  
  if (readPointer != writePointer) {
    numberOfSamples = writePointer - readPointer;
    if (numberOfSamples < 0) numberOfSamples += 32;
    
    int bytesLeftToRead = numberOfSamples * 6; // 3 bytes per sample (Red + IR + Green)
    
    Wire.beginTransmission(_i2caddr);
    Wire.write(MAX30105_FIFO_DATA);
    Wire.endTransmission(false);
    
    Wire.requestFrom(_i2caddr, bytesLeftToRead);
    
    while (numberOfSamples--) {
      uint32_t tempLong = 0;
      
      // Red
      tempLong = Wire.read() & 0x03;
      tempLong <<= 16;
      tempLong |= Wire.read() << 8;
      tempLong |= Wire.read();
      sense.red[sense.head] = tempLong;
      
      // IR
      tempLong = Wire.read() & 0x03;
      tempLong <<= 16;
      tempLong |= Wire.read() << 8;
      tempLong |= Wire.read();
      sense.IR[sense.head] = tempLong;
      
      sense.head++;
      sense.head %= 32;
    }
  }
  
  return numberOfSamples;
}

void MAX30105::setPulseAmplitudeRed(uint8_t amplitude) {
  writeRegister8(MAX30105_LED1_PULSEAMP, amplitude);
}

void MAX30105::setPulseAmplitudeIR(uint8_t amplitude) {
  writeRegister8(MAX30105_LED2_PULSEAMP, amplitude);
}

void MAX30105::setPulseAmplitudeGreen(uint8_t amplitude) {
  writeRegister8(MAX30105_LED3_PULSEAMP, amplitude);
}

void MAX30105::setPulseAmplitudeProximity(uint8_t amplitude) {
  writeRegister8(MAX30105_PILOT_PA, amplitude);
}

void MAX30105::softReset() {
  bitMask(MAX30105_MODE_CONFIG, MAX30105_RESET, MAX30105_RESET);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 100) {
    uint8_t response = readRegister8(MAX30105_MODE_CONFIG);
    if ((response & MAX30105_RESET) == 0) break;
    delay(1);
  }
}

void MAX30105::shutDown() {
  bitMask(MAX30105_MODE_CONFIG, MAX30105_SHUTDOWN, MAX30105_SHUTDOWN);
}

void MAX30105::wakeUp() {
  bitMask(MAX30105_MODE_CONFIG, MAX30105_SHUTDOWN, 0);
}

void MAX30105::setFIFOAverage(uint8_t numberOfSamples) {
  bitMask(MAX30105_FIFO_CONFIG, 0xE0, numberOfSamples);
}

void MAX30105::enableFIFORollover() {
  bitMask(MAX30105_FIFO_CONFIG, MAX30105_ROLLOVER_ENABLE, MAX30105_ROLLOVER_ENABLE);
}

void MAX30105::disableFIFORollover() {
  bitMask(MAX30105_FIFO_CONFIG, MAX30105_ROLLOVER_ENABLE, 0);
}

void MAX30105::setFIFOAlmostFull(uint8_t numberOfSamples) {
  bitMask(MAX30105_FIFO_CONFIG, MAX30105_A_FULL_MASK, numberOfSamples);
}

void MAX30105::clearFIFO() {
  writeRegister8(MAX30105_FIFO_WR_PTR, 0);
  writeRegister8(MAX30105_OVF_COUNTER, 0);
  writeRegister8(MAX30105_FIFO_RD_PTR, 0);
}

uint8_t MAX30105::getWritePointer() {
  return readRegister8(MAX30105_FIFO_WR_PTR);
}

uint8_t MAX30105::getReadPointer() {
  return readRegister8(MAX30105_FIFO_RD_PTR);
}

float MAX30105::readTemperature() {
  writeRegister8(MAX30105_TEMP_CONFIG, 0x01);
  
  unsigned long startTime = millis();
  while (millis() - startTime < 100) {
    uint8_t response = readRegister8(MAX30105_TEMP_CONFIG);
    if ((response & 0x01) == 0) break;
    delay(1);
  }
  
  int8_t tempInt = readRegister8(MAX30105_TEMP_INTR);
  uint8_t tempFrac = readRegister8(MAX30105_TEMP_FRAC);
  
  return (float)tempInt + ((float)tempFrac * 0.0625);
}

float MAX30105::readTemperatureF() {
  float temp = readTemperature();
  if (temp != -999.0) {
    temp = temp * 1.8 + 32.0;
  }
  return temp;
}

uint8_t MAX30105::getRevisionID() {
  return readRegister8(MAX30105_REV_ID);
}

uint8_t MAX30105::readPartID() {
  return readRegister8(MAX30105_PART_ID);
}

void MAX30105::writeRegister8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t MAX30105::readRegister8(uint8_t reg) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(reg);
  Wire.endTransmission(false);
  
  Wire.requestFrom(_i2caddr, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0;
}

void MAX30105::bitMask(uint8_t reg, uint8_t mask, uint8_t thing) {
  uint8_t originalContents = readRegister8(reg);
  originalContents = originalContents & mask;
  writeRegister8(reg, originalContents | thing);
}