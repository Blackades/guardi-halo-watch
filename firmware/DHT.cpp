#include "DHT.h"

#define MIN_INTERVAL 2000

DHT::DHT(uint8_t pin, uint8_t type, uint8_t count) {
  _pin = pin;
  _type = type;
  _count = count;
  _firstreading = true;
}

void DHT::begin() {
  pinMode(_pin, INPUT_PULLUP);
  _lastreadtime = 0;
}

float DHT::readTemperature(bool S, bool force) {
  float f = NAN;
  
  if (read(force)) {
    switch (_type) {
      case DHT11:
        f = data[2];
        if (data[3] & 0x80) {
          f = -1 - f;
        }
        f += (data[3] & 0x0f) * 0.1;
        if (S) {
          f = f * 1.8 + 32;
        }
        break;
      case DHT22:
      case DHT21:
        f = ((word)(data[2] & 0x7F)) << 8 | data[3];
        f *= 0.1;
        if (data[2] & 0x80) {
          f *= -1;
        }
        if (S) {
          f = f * 1.8 + 32;
        }
        break;
    }
  }
  return f;
}

float DHT::readHumidity(bool force) {
  float f = NAN;
  if (read(force)) {
    switch (_type) {
      case DHT11:
        f = data[0] + data[1] * 0.1;
        break;
      case DHT22:
      case DHT21:
        f = ((word)data[0]) << 8 | data[1];
        f *= 0.1;
        break;
    }
  }
  return f;
}

float DHT::computeHeatIndex(float temperature, float humidity, bool isFahrenheit) {
  float hi;
  
  if (!isFahrenheit)
    temperature = temperature * 1.8 + 32;
  
  hi = 0.5 * (temperature + 61.0 + ((temperature - 68.0) * 1.2) + (humidity * 0.094));
  
  if (hi > 79) {
    hi = -42.379 +
         2.04901523 * temperature +
         10.14333127 * humidity +
         -0.22475541 * temperature * humidity +
         -0.00683783 * pow(temperature, 2) +
         -0.05481717 * pow(humidity, 2) +
         0.00122874 * pow(temperature, 2) * humidity +
         0.00085282 * temperature * pow(humidity, 2) +
         -0.00000199 * pow(temperature, 2) * pow(humidity, 2);
    
    if ((humidity < 13) && (temperature >= 80.0) && (temperature <= 112.0))
      hi -= ((13.0 - humidity) * 0.25) * sqrt((17.0 - abs(temperature - 95.0)) * 0.05882);
    
    else if ((humidity > 85.0) && (temperature >= 80.0) && (temperature <= 87.0))
      hi += ((humidity - 85.0) * 0.1) * ((87.0 - temperature) * 0.2);
  }
  
  return isFahrenheit ? hi : (hi - 32) * 0.55556;
}

bool DHT::read(bool force) {
  unsigned long currenttime = millis();
  if (!_firstreading && ((currenttime - _lastreadtime) < MIN_INTERVAL) && !force) {
    return true; // return last correct measurement
  }
  _firstreading = false;
  _lastreadtime = currenttime;
  
  data[0] = data[1] = data[2] = data[3] = data[4] = 0;
  
  // Send start signal
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  delay(20);
  
  uint32_t cycles[80];
  {
    pinMode(_pin, INPUT_PULLUP);
    delayMicroseconds(55);
    
    if (expectPulse(LOW) == 0) {
      return false;
    }
    
    if (expectPulse(HIGH) == 0) {
      return false;
    }
    
    // Read the 40 bits sent by the sensor
    for (int i = 0; i < 80; i += 2) {
      cycles[i] = expectPulse(LOW);
      cycles[i + 1] = expectPulse(HIGH);
    }
  }
  
  // Inspect pulses and determine which ones are 0 (high state cycle count < low
  // state cycle count), or 1 (high state cycle count > low state cycle count).
  for (int i = 0; i < 40; ++i) {
    uint32_t lowCycles = cycles[2 * i];
    uint32_t highCycles = cycles[2 * i + 1];
    if ((lowCycles == 0) || (highCycles == 0)) {
      return false;
    }
    data[i / 8] <<= 1;
    // Now compare the low and high cycle times to see if the bit is a 0 or 1.
    if (highCycles > lowCycles) {
      // High cycles are greater than 50us low cycle count, must be a 1.
      data[i / 8] |= 1;
    }
    // Else high cycles are less than (or equal to, a weird case) the 50us low
    // cycle count so this must be a zero.  Nothing needs to be changed in the
    // stored data.
  }
  
  // Check we read 40 bits and that the checksum matches.
  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    return true;
  } else {
    return false;
  }
}

uint32_t DHT::expectPulse(bool level) {
  uint32_t count = 0;
  uint32_t maxcount = microsecondsToClockCycles(1000); // 1 millisecond timeout
  
  while (digitalRead(_pin) == level) {
    if (count++ >= maxcount) {
      return 0; // Exceeded timeout, fail.
    }
  }
  
  return count;
}