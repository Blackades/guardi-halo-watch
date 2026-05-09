# ESP32 Receiver Node Firmware Testing Suite

This directory contains comprehensive testing tools for the ESP32 Receiver Node firmware, including unit tests, integration tests, and automated test runners.

## Test Files

### `test_receiver_node_firmware.cpp`
Complete test suite with Unity framework containing:
- **RFID Reader Tests**: Tag validation, signal processing, detection filtering
- **Zone Management Tests**: Access control, occupancy limits, violation detection
- **Communication Tests**: JSON payload creation, offline storage, configuration validation
- **Status Indicator Tests**: LED patterns, alert mapping, diagnostic modes
- **Integration Tests**: End-to-end workflows, system recovery scenarios
- **Performance Tests**: Processing speed, memory usage validation

### `run_receiver_tests.py`
Python-based automated test runner that:
- Connects to ESP32 via serial port
- Executes test suites automatically
- Parses and reports test results
- Supports different test types (unit, diagnostic, connectivity)
- Generates detailed test reports

## Running Tests

### Prerequisites
1. **Hardware**: ESP32 development board with receiver node firmware
2. **Software**: 
   - Arduino IDE or PlatformIO
   - Python 3.6+ with pyserial (`pip install pyserial`)
   - Unity testing framework for Arduino

### Manual Testing
1. Upload `test_receiver_node_firmware.cpp` to ESP32
2. Open Serial Monitor (115200 baud)
3. Tests run automatically on startup
4. View results in serial output

### Automated Testing
```bash
# Run all tests
python run_receiver_tests.py --port COM3

# Run specific test types
python run_receiver_tests.py --port /dev/ttyUSB0 --test-type unit
python run_receiver_tests.py --port COM3 --test-type diagnostic
python run_receiver_tests.py --port COM3 --test-type connectivity

# Save results to file
python run_receiver_tests.py --port COM3 --output test_results.txt
```

## Test Categories

### 1. RFID Reader Tests
- **Tag Validation**: Verifies RFID tag format and checksum validation
- **Signal Processing**: Tests RSSI to distance conversion algorithms
- **Detection Filtering**: Validates filtering of weak signals and invalid tags
- **Continuous Scanning**: Tests scanning intervals and detection persistence

**Key Test Cases:**
```cpp
test_rfid_tag_validation()          // Valid/invalid tag formats
test_signal_strength_to_distance()  // RSSI to distance conversion
test_rfid_detection_filtering()     // Signal strength filtering
```

### 2. Zone Management Tests
- **Access Authorization**: Tests patient authorization for different zone types
- **Occupancy Limits**: Validates occupancy counting and limit enforcement
- **Violation Detection**: Tests detection of unauthorized access and overcrowding
- **Zone Configuration**: Validates zone setup and configuration updates

**Key Test Cases:**
```cpp
test_zone_access_authorization()    // Patient access control
test_occupancy_limit_enforcement()  // Occupancy counting
test_zone_violation_detection()     // Violation alert generation
```

### 3. Communication Tests
- **JSON Payload**: Tests location update payload creation and validation
- **Offline Storage**: Validates offline data storage and synchronization
- **Configuration Management**: Tests device configuration loading and saving
- **Error Handling**: Validates communication error recovery

**Key Test Cases:**
```cpp
test_json_payload_creation()        // API payload formatting
test_offline_data_storage()         // Offline data management
test_configuration_validation()     // Config parameter validation
```

### 4. Status Indicator Tests
- **LED Patterns**: Tests RGB LED color combinations and blinking patterns
- **Alert Mapping**: Validates alert type to visual/audio pattern mapping
- **Diagnostic Mode**: Tests hardware diagnostic sequences
- **Status Feedback**: Validates status indication for different system states

**Key Test Cases:**
```cpp
test_led_color_patterns()           // LED color validation
test_alert_pattern_mapping()        // Alert to pattern mapping
```

### 5. Integration Tests
- **End-to-End Flow**: Tests complete detection to backend communication flow
- **System Recovery**: Validates recovery from WiFi disconnection and errors
- **Device Registration**: Tests automatic device registration with backend
- **Configuration Updates**: Tests remote configuration updates

**Key Test Cases:**
```cpp
test_end_to_end_detection_flow()    // Complete workflow
test_system_recovery_scenarios()    // Error recovery
```

### 6. Performance Tests
- **Processing Speed**: Measures detection processing performance
- **Memory Usage**: Validates memory consumption and leak detection
- **Concurrent Operations**: Tests handling of multiple simultaneous detections
- **Resource Limits**: Validates behavior under resource constraints

**Key Test Cases:**
```cpp
test_detection_processing_performance()  // Speed benchmarks
test_memory_usage()                      // Memory consumption
```

## Test Results Interpretation

### Success Criteria
- **All unit tests pass**: Core functionality works correctly
- **No memory leaks**: Memory usage remains stable
- **Performance targets met**: Processing completes within time limits
- **Error recovery works**: System recovers from common failure scenarios

### Common Issues and Solutions

#### Test Failures
1. **RFID Tag Validation Fails**
   - Check tag format validation logic
   - Verify hexadecimal character checking
   - Ensure proper length validation

2. **Zone Access Tests Fail**
   - Verify authorized patient list configuration
   - Check zone type handling logic
   - Validate occupancy counting algorithm

3. **Communication Tests Fail**
   - Check JSON serialization/deserialization
   - Verify network configuration
   - Test offline storage implementation

#### Performance Issues
1. **Slow Processing**
   - Optimize detection algorithms
   - Reduce memory allocations
   - Improve data structure efficiency

2. **Memory Usage High**
   - Check for memory leaks
   - Optimize string operations
   - Reduce buffer sizes if possible

## Continuous Integration

### Automated Testing Pipeline
1. **Code Commit**: Triggers automated testing
2. **Hardware-in-Loop**: Tests run on actual ESP32 hardware
3. **Result Reporting**: Test results posted to development dashboard
4. **Quality Gates**: Prevent deployment of failing code

### Test Coverage Requirements
- **Unit Tests**: >90% code coverage
- **Integration Tests**: All major workflows covered
- **Performance Tests**: All critical paths benchmarked
- **Error Scenarios**: All error conditions tested

## Adding New Tests

### Creating Unit Tests
```cpp
void test_new_functionality() {
    Serial.println("Testing new functionality...");
    
    // Setup test data
    TestData data = createTestData();
    
    // Execute function under test
    bool result = newFunction(data);
    
    // Verify results
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(expected_value, actual_value);
}
```

### Adding to Test Runner
1. Add test function to `runAllTests()`
2. Update test categories in documentation
3. Add corresponding automated test case if needed

### Test Naming Convention
- `test_<component>_<functionality>()`
- Use descriptive names that explain what is being tested
- Group related tests together

## Debugging Test Failures

### Serial Output Analysis
- Enable verbose logging during tests
- Check for assertion failures and error messages
- Monitor memory usage and performance metrics

### Hardware Debugging
- Use logic analyzer for signal timing
- Check power supply stability
- Verify hardware connections

### Software Debugging
- Add debug prints to isolate issues
- Use debugger breakpoints where possible
- Check variable values at failure points

## Test Maintenance

### Regular Updates
- Update tests when firmware functionality changes
- Add tests for new features
- Remove obsolete tests

### Performance Monitoring
- Track test execution times
- Monitor memory usage trends
- Update performance benchmarks as needed

### Documentation
- Keep test documentation current
- Document known issues and workarounds
- Update test procedures for new hardware versions