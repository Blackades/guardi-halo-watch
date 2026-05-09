#ifndef DHT_H
#define DHT_H

#include <Arduino.h>

// DHT sensor types
#define DHT11 11
#define DHT22 22
#define DHT21 21
#define AM2301 21

class DHT {
public:
  DHT(uint8_t pin, uint8_t type, uint8_t count = 6);
  void begin();
  float readTemperature(bool S = false, bool force = false);
  float readHumidity(bool force = false);
  float computeHeatIndex(float temperature, float humidity, bool isFahrenheit = true);
  bool read(bool force = false);

private:
  uint8_t data[5];
  uint8_t _pin, _type;
  uint8_t _count;
  unsigned long _lastreadtime;
  bool _firstreading;
  
  uint32_t expectPulse(bool level);
};

#endif