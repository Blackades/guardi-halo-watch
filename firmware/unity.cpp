#include "unity.h"

// Global test counters
int unity_test_count = 0;
int unity_test_passed = 0;
int unity_test_failed = 0;

void unity_begin() {
  unity_test_count = 0;
  unity_test_passed = 0;
  unity_test_failed = 0;
  Serial.println("Unity Test Framework - Starting Tests");
  Serial.println("=====================================");
}

void unity_end() {
  Serial.println("=====================================");
  Serial.printf("Tests run: %d\n", unity_test_count);
  Serial.printf("Tests passed: %d\n", unity_test_passed);
  Serial.printf("Tests failed: %d\n", unity_test_failed);
  
  if (unity_test_failed == 0) {
    Serial.println("ALL TESTS PASSED!");
  } else {
    Serial.printf("%d TESTS FAILED!\n", unity_test_failed);
  }
}

void unity_run_test(void (*test_func)(), const char* test_name) {
  unity_test_count++;
  Serial.printf("Running test: %s\n", test_name);
  
  try {
    test_func();
    unity_test_passed++;
    Serial.printf("✓ PASS: %s\n", test_name);
  } catch (...) {
    unity_test_failed++;
    Serial.printf("✗ FAIL: %s\n", test_name);
  }
  
  Serial.println();
}

void unity_assert_true(bool condition, int line, const char* expression) {
  if (!condition) {
    Serial.printf("ASSERTION FAILED at line %d: %s\n", line, expression);
    throw "Assertion failed";
  }
}

void unity_assert_false(bool condition, int line, const char* expression) {
  if (condition) {
    Serial.printf("ASSERTION FAILED at line %d: Expected false but got true for %s\n", line, expression);
    throw "Assertion failed";
  }
}

void unity_assert_equal(int expected, int actual, int line) {
  if (expected != actual) {
    Serial.printf("ASSERTION FAILED at line %d: Expected %d but got %d\n", line, expected, actual);
    throw "Assertion failed";
  }
}

void unity_assert_int_within(int delta, int expected, int actual, int line) {
  if (abs(expected - actual) > delta) {
    Serial.printf("ASSERTION FAILED at line %d: Expected %d ± %d but got %d\n", line, expected, delta, actual);
    throw "Assertion failed";
  }
}

void unity_assert_float_within(float delta, float expected, float actual, int line) {
  if (abs(expected - actual) > delta) {
    Serial.printf("ASSERTION FAILED at line %d: Expected %.2f ± %.2f but got %.2f\n", line, expected, delta, actual);
    throw "Assertion failed";
  }
}

void unity_assert_true_message(bool condition, int line, const char* message) {
  if (!condition) {
    Serial.printf("ASSERTION FAILED at line %d: %s\n", line, message);
    throw "Assertion failed";
  }
}

void unity_assert_false_message(bool condition, int line, const char* message) {
  if (condition) {
    Serial.printf("ASSERTION FAILED at line %d: %s\n", line, message);
    throw "Assertion failed";
  }
}

void unity_assert_equal_message(int expected, int actual, int line, const char* message) {
  if (expected != actual) {
    Serial.printf("ASSERTION FAILED at line %d: %s (Expected %d but got %d)\n", line, message, expected, actual);
    throw "Assertion failed";
  }
}

void unity_assert_int_within_message(int delta, int expected, int actual, int line, const char* message) {
  if (abs(expected - actual) > delta) {
    Serial.printf("ASSERTION FAILED at line %d: %s (Expected %d ± %d but got %d)\n", line, message, expected, delta, actual);
    throw "Assertion failed";
  }
}

void unity_assert_float_within_message(float delta, float expected, float actual, int line, const char* message) {
  if (abs(expected - actual) > delta) {
    Serial.printf("ASSERTION FAILED at line %d: %s (Expected %.2f ± %.2f but got %.2f)\n", line, message, expected, delta, actual);
    throw "Assertion failed";
  }
}