#ifndef HEARTRATE_H
#define HEARTRATE_H

#include <Arduino.h>

// Heart rate calculation constants
#define RATE_SIZE 4 // Increase this for more averaging. 4 is good.

// Function prototypes
bool checkForBeat(int32_t sample);
int16_t averageDCEstimator(int32_t *p, uint16_t x);
int16_t lowPassFIRFilter(int16_t din);
int32_t mul16(int16_t x, int16_t y);

// Global variables for heart rate calculation
extern long rates[RATE_SIZE]; // Array of heart rates
extern byte rateArray; // Pointer to the rate array
extern long lastBeat; // Time at which the last beat occurred
extern int beatsPerMinute; // Calculated BPM
extern byte ratePointer; // Pointer for the rate array

#endif