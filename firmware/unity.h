#ifndef UNITY_H
#define UNITY_H

#include <Arduino.h>

// Unity test framework macros for Arduino
#define UNITY_BEGIN() unity_begin()
#define UNITY_END() unity_end()
#define RUN_TEST(func) unity_run_test(func, #func)

#define TEST_ASSERT_TRUE(condition) unity_assert_true(condition, __LINE__, #condition)
#define TEST_ASSERT_FALSE(condition) unity_assert_false(condition, __LINE__, #condition)
#define TEST_ASSERT_EQUAL(expected, actual) unity_assert_equal(expected, actual, __LINE__)
#define TEST_ASSERT_INT_WITHIN(delta, expected, actual) unity_assert_int_within(delta, expected, actual, __LINE__)
#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual) unity_assert_float_within(delta, expected, actual, __LINE__)

#define TEST_ASSERT_TRUE_MESSAGE(condition, message) unity_assert_true_message(condition, __LINE__, message)
#define TEST_ASSERT_FALSE_MESSAGE(condition, message) unity_assert_false_message(condition, __LINE__, message)
#define TEST_ASSERT_EQUAL_MESSAGE(expected, actual, message) unity_assert_equal_message(expected, actual, __LINE__, message)
#define TEST_ASSERT_INT_WITHIN_MESSAGE(delta, expected, actual, message) unity_assert_int_within_message(delta, expected, actual, __LINE__, message)
#define TEST_ASSERT_FLOAT_WITHIN_MESSAGE(delta, expected, actual, message) unity_assert_float_within_message(delta, expected, actual, __LINE__, message)

// Global test counters
extern int unity_test_count;
extern int unity_test_passed;
extern int unity_test_failed;

// Function prototypes
void unity_begin();
void unity_end();
void unity_run_test(void (*test_func)(), const char* test_name);
void unity_assert_true(bool condition, int line, const char* expression);
void unity_assert_false(bool condition, int line, const char* expression);
void unity_assert_equal(int expected, int actual, int line);
void unity_assert_int_within(int delta, int expected, int actual, int line);
void unity_assert_float_within(float delta, float expected, float actual, int line);
void unity_assert_true_message(bool condition, int line, const char* message);
void unity_assert_false_message(bool condition, int line, const char* message);
void unity_assert_equal_message(int expected, int actual, int line, const char* message);
void unity_assert_int_within_message(int delta, int expected, int actual, int line, const char* message);
void unity_assert_float_within_message(float delta, float expected, float actual, int line, const char* message);

#endif