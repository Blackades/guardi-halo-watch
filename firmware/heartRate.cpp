#include "heartRate.h"

// Constants
#define BUFFER_SIZE 100
#define AVERAGE_SIZE 16
#define FIR_COEFFS 128

// Global variables
long rates[RATE_SIZE]; // Array of heart rates
byte rateArray = 0; // Pointer to the rate array
long lastBeat = 0; // Time at which the last beat occurred
int beatsPerMinute = 0; // Calculated BPM
byte ratePointer = 0; // Pointer for the rate array

// Beat detection variables
static int32_t an_x[BUFFER_SIZE]; // PPG signal buffer
static int32_t an_y[BUFFER_SIZE]; // Filtered PPG signal buffer
static uint16_t an_dx = 0; // Buffer index

bool checkForBeat(int32_t sample) {
  bool beatDetected = false;
  
  // Save current sample in circular buffer
  an_x[an_dx] = sample;
  
  // Apply DC removal and low-pass filtering
  an_y[an_dx] = lowPassFIRFilter(averageDCEstimator(&an_x[an_dx], sample));
  
  // Beat detection algorithm
  static int32_t peak_threshold = 0;
  static int32_t valley_threshold = 0;
  static bool looking_for_peak = true;
  static int32_t last_peak_value = 0;
  static int32_t last_valley_value = 0;
  static uint32_t last_peak_time = 0;
  static uint32_t last_valley_time = 0;
  
  int32_t current_sample = an_y[an_dx];
  uint32_t current_time = millis();
  
  if (looking_for_peak) {
    if (current_sample > peak_threshold) {
      // Found a peak
      if (current_sample > last_peak_value) {
        last_peak_value = current_sample;
        last_peak_time = current_time;
      }
    } else {
      // Peak has ended, now look for valley
      if (last_peak_value > 0) {
        looking_for_peak = false;
        valley_threshold = last_peak_value * 0.6; // 60% of peak
        last_valley_value = current_sample;
        last_valley_time = current_time;
      }
    }
  } else {
    if (current_sample < valley_threshold) {
      // Found a valley
      if (current_sample < last_valley_value) {
        last_valley_value = current_sample;
        last_valley_time = current_time;
      }
    } else {
      // Valley has ended, beat detected
      if (last_valley_value < valley_threshold) {
        // Calculate time between beats
        uint32_t delta = last_peak_time - lastBeat;
        lastBeat = last_peak_time;
        
        // Valid beat if delta is reasonable (30-200 BPM range)
        if (delta > 300 && delta < 2000) {
          // Store this reading in the array
          rates[ratePointer++] = (60000 / delta); // Convert to BPM
          ratePointer %= RATE_SIZE; // Wrap variable
          
          // Take average of readings to get the BPM
          long total = 0;
          for (byte i = 0; i < RATE_SIZE; i++) {
            total += rates[i];
          }
          beatsPerMinute = total / RATE_SIZE;
          
          beatDetected = true;
        }
        
        // Reset for next beat
        looking_for_peak = true;
        peak_threshold = last_valley_value + (last_peak_value - last_valley_value) * 0.3;
        last_peak_value = 0;
        last_valley_value = 0;
      }
    }
  }
  
  // Move to next position in buffer
  an_dx++;
  an_dx %= BUFFER_SIZE;
  
  return beatDetected;
}

int16_t averageDCEstimator(int32_t *p, uint16_t x) {
  static int16_t w = 0;
  static int32_t sum = 0;
  static int16_t idx = 0;
  static int16_t an_dx[AVERAGE_SIZE];
  
  sum -= an_dx[idx];
  an_dx[idx] = x;
  sum += an_dx[idx];
  
  idx++;
  idx %= AVERAGE_SIZE;
  
  w = sum / AVERAGE_SIZE;
  return (x - w);
}

int16_t lowPassFIRFilter(int16_t din) {
  static int16_t cbuf[FIR_COEFFS];
  static int16_t offset = 0;
  static int32_t z;
  
  // FIR filter coefficients (low-pass, cutoff ~5Hz for 100Hz sampling)
  static const int16_t coeffs[FIR_COEFFS] = {
    172, 321, 579, 927, 1360, 1858, 2390, 2916, 3391, 3768,
    4019, 4125, 4077, 3876, 3535, 3073, 2516, 1896, 1248, 610,
    10, -536, -1011, -1398, -1669, -1795, -1760, -1554, -1176, -634,
    67, 918, 1900, 2980, 4122, 5280, 6407, 7455, 8377, 9127,
    9663, 9951, 9963, 9679, 9090, 8202, 7034, 5612, 3974, 2165,
    238, -1756, -3734, -5618, -7347, -8861, -10106, -11029, -11588, -11749,
    -11494, -10824, -9758, -8332, -6595, -4607, -2434, -148, 2199, 4546,
    6835, 9006, 11000, 12759, 14232, 15375, 16150, 16526, 16485, 16019,
    15134, 13847, 12188, 10199, 7928, 5431, 2772, 1, -2770, -5428,
    -7926, -10197, -12186, -13845, -15132, -16017, -16483, -16524, -16148, -15373,
    -14230, -12757, -10998, -9004, -6833, -4544, -2197, 150, 2436, 4609,
    6597, 8334, 9760, 10826, 11496, 11751, 11590, 11031, 10108, 8863,
    7349, 5620, 3736, 1758, -236, -2163, -3972, -5610, -7032, -8200,
    -9088, -9677, -9961, -9949, -9661, -9125, -8375, -7453, -6405, -5278,
    -4120, -2978, -1896, -916, -65, 636, 1178, 1556, 1762, 1797,
    1671, 1400, 1013, 538, -10, -608, -1246, -1894, -2514, -3071,
    -3533, -3874, -4075, -4123, -4017, -3766, -3389, -2914, -2388, -1856,
    -1358, -925, -577, -319, -170
  };
  
  cbuf[offset] = din;
  
  z = mul16(coeffs[0], cbuf[(offset - 0) & (FIR_COEFFS - 1)]);
  
  for (int i = 1; i < FIR_COEFFS; i++) {
    z += mul16(coeffs[i], cbuf[(offset - i) & (FIR_COEFFS - 1)]);
  }
  
  offset++;
  offset %= FIR_COEFFS;
  
  return (z >> 15);
}

int32_t mul16(int16_t x, int16_t y) {
  return ((int32_t)x * (int32_t)y);
}