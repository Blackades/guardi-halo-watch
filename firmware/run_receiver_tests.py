#!/usr/bin/env python3
"""
ESP32 Receiver Node Firmware Test Runner
Automates the testing process for the receiver node firmware
"""

import serial
import time
import sys
import argparse
import re
from datetime import datetime

class ReceiverTestRunner:
    def __init__(self, port, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial_conn = None
        self.test_results = []
        
    def connect(self):
        """Connect to the ESP32 device"""
        try:
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=2)
            time.sleep(2)  # Wait for connection to stabilize
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"Failed to connect to {self.port}: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from the ESP32 device"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            print("Disconnected from device")
    
    def send_command(self, command):
        """Send a command to the device"""
        if not self.serial_conn or not self.serial_conn.is_open:
            return False
        
        try:
            self.serial_conn.write((command + '\n').encode())
            return True
        except serial.SerialException as e:
            print(f"Failed to send command: {e}")
            return False
    
    def read_output(self, timeout=30):
        """Read output from the device"""
        output_lines = []
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            if self.serial_conn.in_waiting > 0:
                try:
                    line = self.serial_conn.readline().decode('utf-8').strip()
                    if line:
                        output_lines.append(line)
                        print(line)  # Print to console in real-time
                        
                        # Check for test completion
                        if "All tests completed!" in line:
                            break
                except UnicodeDecodeError:
                    continue
            time.sleep(0.1)
        
        return output_lines
    
    def parse_test_results(self, output_lines):
        """Parse test results from output"""
        test_pattern = r'(\w+):(\w+)'  # Match test results
        summary_pattern = r'(\d+) Tests (\d+) Failures (\d+) Ignored'
        
        tests_passed = 0
        tests_failed = 0
        test_details = []
        
        for line in output_lines:
            # Look for individual test results
            if ':PASS' in line or ':FAIL' in line:
                test_name = line.split(':')[0].strip()
                result = 'PASS' if ':PASS' in line else 'FAIL'
                test_details.append({'name': test_name, 'result': result})
                
                if result == 'PASS':
                    tests_passed += 1
                else:
                    tests_failed += 1
            
            # Look for summary line
            summary_match = re.search(summary_pattern, line)
            if summary_match:
                total_tests = int(summary_match.group(1))
                failures = int(summary_match.group(2))
                ignored = int(summary_match.group(3))
                
                return {
                    'total': total_tests,
                    'passed': total_tests - failures,
                    'failed': failures,
                    'ignored': ignored,
                    'details': test_details
                }
        
        # If no summary found, use parsed results
        return {
            'total': tests_passed + tests_failed,
            'passed': tests_passed,
            'failed': tests_failed,
            'ignored': 0,
            'details': test_details
        }
    
    def run_tests(self):
        """Run the complete test suite"""
        print("Starting ESP32 Receiver Node Firmware Tests")
        print("=" * 50)
        
        if not self.connect():
            return False
        
        try:
            # Reset the device to start fresh
            print("Resetting device...")
            self.send_command("restart")
            time.sleep(3)
            
            # Read test output
            print("Running tests...")
            output = self.read_output(timeout=60)
            
            # Parse results
            results = self.parse_test_results(output)
            
            # Display summary
            self.display_results(results)
            
            return results['failed'] == 0
            
        finally:
            self.disconnect()
    
    def display_results(self, results):
        """Display test results summary"""
        print("\n" + "=" * 50)
        print("TEST RESULTS SUMMARY")
        print("=" * 50)
        print(f"Total Tests: {results['total']}")
        print(f"Passed: {results['passed']}")
        print(f"Failed: {results['failed']}")
        print(f"Ignored: {results['ignored']}")
        print(f"Success Rate: {(results['passed'] / results['total'] * 100):.1f}%")
        
        if results['details']:
            print("\nDetailed Results:")
            for test in results['details']:
                status = "✓" if test['result'] == 'PASS' else "✗"
                print(f"  {status} {test['name']}: {test['result']}")
        
        print("=" * 50)
    
    def run_diagnostic_tests(self):
        """Run diagnostic tests on the device"""
        print("Running diagnostic tests...")
        
        if not self.connect():
            return False
        
        try:
            # Run diagnostic command
            self.send_command("diagnostic")
            time.sleep(5)
            
            # Check status
            self.send_command("status")
            output = self.read_output(timeout=10)
            
            # Look for diagnostic completion
            diagnostic_complete = any("Diagnostic mode complete" in line for line in output)
            
            if diagnostic_complete:
                print("✓ Diagnostic tests completed successfully")
                return True
            else:
                print("✗ Diagnostic tests failed or incomplete")
                return False
                
        finally:
            self.disconnect()
    
    def run_connectivity_test(self):
        """Test WiFi connectivity and backend communication"""
        print("Testing connectivity...")
        
        if not self.connect():
            return False
        
        try:
            # Test WiFi connection
            self.send_command("wifi")
            time.sleep(10)
            
            # Check registration
            self.send_command("register")
            time.sleep(5)
            
            # Get status
            self.send_command("status")
            output = self.read_output(timeout=10)
            
            # Check for successful connection
            wifi_connected = any("WiFi Status: Connected" in line for line in output)
            
            if wifi_connected:
                print("✓ Connectivity test passed")
                return True
            else:
                print("✗ Connectivity test failed")
                return False
                
        finally:
            self.disconnect()

def main():
    parser = argparse.ArgumentParser(description='ESP32 Receiver Node Test Runner')
    parser.add_argument('--port', '-p', required=True, help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baudrate', '-b', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--test-type', '-t', choices=['all', 'unit', 'diagnostic', 'connectivity'], 
                       default='all', help='Type of tests to run')
    parser.add_argument('--output', '-o', help='Output file for test results')
    
    args = parser.parse_args()
    
    runner = ReceiverTestRunner(args.port, args.baudrate)
    
    success = True
    
    if args.test_type in ['all', 'unit']:
        print("Running unit tests...")
        success &= runner.run_tests()
    
    if args.test_type in ['all', 'diagnostic']:
        print("\nRunning diagnostic tests...")
        success &= runner.run_diagnostic_tests()
    
    if args.test_type in ['all', 'connectivity']:
        print("\nRunning connectivity tests...")
        success &= runner.run_connectivity_test()
    
    if success:
        print("\n✓ All tests passed!")
        sys.exit(0)
    else:
        print("\n✗ Some tests failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()