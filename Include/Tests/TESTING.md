# RTOS Runtime Testing Framework

## Overview

This runtime testing framework provides comprehensive testing capabilities for the RTOS kernel components during system initialization. It ensures that all critical components (Scheduler, MPU, TrustZone) are functioning correctly before allowing normal system operation.

## Features

- **Comprehensive Component Testing**: Tests for Scheduler, MPU, and TrustZone components
- **Multiple Test Categories**: Unit, Integration, Stress, Fault, and Security tests
- **Configurable Test Execution**: Run critical tests only or comprehensive test suites
- **Real-time Results**: Immediate feedback on component functionality
- **Shell Integration**: Manual test execution and debugging capabilities
- **Safe Testing**: Tests are designed to avoid system crashes while thoroughly validating functionality

## Architecture

```
Test Framework
├── Core Framework (test_framework.c/.h)
│   ├── Test execution engine
│   ├── Result tracking and reporting
│   ├── Timeout handling
│   └── Statistics collection
│
├── Integration Layer (test_integration.c/.h)
│   ├── Component test orchestration
│   ├── Configuration management
│   ├── Shell command interface
│   └── Kernel integration hooks
│
└── Component Test Suites
    ├── Scheduler Tests (test_scheduler.c/.h)
    ├── MPU Tests (test_mpu.c/.h)
    └── TrustZone Tests (test_tz.c/.h)
```

## Test Categories

### Unit Tests
- **Purpose**: Test individual component functions
- **Severity**: Critical to Medium
- **Examples**: Scheduler initialization, task creation, MPU region configuration

### Integration Tests  
- **Purpose**: Test component interactions
- **Severity**: High to Medium
- **Examples**: Task switching with MPU, TrustZone state transitions

### Stress Tests
- **Purpose**: Test system behavior under load
- **Severity**: Low to Medium
- **Examples**: Many concurrent tasks, rapid configuration changes

### Fault Tests
- **Purpose**: Test error handling and recovery
- **Severity**: Medium
- **Examples**: Invalid parameters, resource exhaustion

### Security Tests
- **Purpose**: Test security boundaries and isolation
- **Severity**: High to Critical
- **Examples**: Memory isolation, privilege escalation prevention

## Component Test Coverage

### Scheduler Tests
- ✅ Initialization and startup
- ✅ Task creation and lifecycle
- ✅ Priority-based scheduling
- ✅ Multicore task distribution
- ✅ Deadline scheduling (soft/hard deadlines)
- ✅ Task yield and synchronization
- ✅ Statistics and monitoring
- ✅ Error handling and edge cases

### MPU (Memory Protection Unit) Tests
- ✅ Hardware support detection
- ✅ Region configuration and alignment
- ✅ Access permission enforcement
- ✅ Read-only and execute-never protection
- ✅ Task-specific MPU configurations
- ✅ Fault detection and handling
- ✅ Performance under load
- ✅ Integration with scheduler

### TrustZone Tests
- ✅ Hardware support detection
- ✅ Security state transitions
- ✅ Secure function registration
- ✅ Non-secure callable (NSC) functions
- ✅ SAU (Security Attribution Unit) configuration
- ✅ Security boundary enforcement
- ✅ Memory isolation
- ✅ Integration with scheduler

## Integration Guide

### 1. Add Files to Project

```c
// Core framework
Src/Tests/test_framework.c
Src/Tests/test_framework.h
Src/Tests/test_integration.c
Src/Tests/test_integration.h

// Component test suites
Src/Tests/test_scheduler.c
Src/Tests/test_scheduler.h
Src/Tests/test_mpu.c
Src/Tests/test_mpu.h
Src/Tests/test_tz.c
Src/Tests/test_tz.h

// Integration guide
Src/Tests/kernel_test_integration.h
```

### 2. Modify Kernel Initialization

Add to `kernel_init.c`:

```c
#include "test_integration.h"
#include "kernel_test_integration.h"

// In kernel_init() after core system initialization:
if (config && config->test_config.enable_runtime_tests) {
    log_message(LOG_LEVEL_INFO, "Kernel Init", "Running component tests...");
    
    bool tests_passed = kernel_run_component_tests(&config->test_config);
    
    if (!tests_passed && config->test_config.abort_on_test_failure) {
        log_message(LOG_LEVEL_FATAL, "Kernel Init", "Aborting due to test failures");
        return SYS_INIT_ERROR_GENERAL;
    }
}
```

### 3. Update Configuration Structure

Add to `kernel_config_t`:

```c
typedef struct {
    // ... existing fields ...
    kernel_test_config_t test_config;
} kernel_config_t;
```

### 4. Build System Integration

Add to CMakeLists.txt:

```cmake
target_sources(your_target PRIVATE
    Src/Tests/test_framework.c
    Src/Tests/test_integration.c
    Src/Tests/test_scheduler.c
    Src/Tests/test_mpu.c
    Src/Tests/test_tz.c
)

target_include_directories(your_target PRIVATE
    Src/Tests
)
```

## Configuration Options

### Runtime Behavior
- `enable_runtime_tests`: Enable/disable testing during initialization
- `run_critical_only`: Run only critical tests for faster boot
- `abort_on_test_failure`: Halt initialization if tests fail
- `verbose_output`: Show detailed test results

### Test Categories
- `run_stress_tests`: Include stress and performance tests
- `run_fault_tests`: Include fault injection tests  
- `run_security_tests`: Include security and isolation tests

### Timing
- `test_timeout_ms`: Maximum time per test (default: 5000ms)

## Usage Examples

### Production Configuration (Fast Boot)
```c
kernel_test_config_t config = {
    .enable_runtime_tests = true,
    .run_critical_only = true,
    .abort_on_test_failure = false,
    .test_config.verbose_output = false,
    .test_config.test_timeout_ms = 2000
};
```

### Development Configuration (Comprehensive)
```c
kernel_test_config_t config = {
    .enable_runtime_tests = true,
    .run_critical_only = false,
    .abort_on_test_failure = true,
    .test_config.run_stress_tests = true,
    .test_config.run_fault_tests = true,
    .test_config.verbose_output = true
};
```

### Disable Testing (Release Build)
```c
kernel_test_config_t config = {
    .enable_runtime_tests = false
};
```

## Shell Commands

When shell integration is enabled, the following commands are available:

### `test run <target>`
Run tests for specific components or test types:
- `test run all` - Run all registered tests
- `test run critical` - Run only critical tests
- `test run comprehensive` - Run full test suite
- `test run scheduler` - Run scheduler tests only
- `test run mpu` - Run MPU tests only
- `test run tz` - Run TrustZone tests only

### `test_status`
Show current test system status and component health.

### `test_config <show|set>`
Configure test system behavior:
- `test_config show` - Display current configuration
- `test_config set stress on|off` - Enable/disable stress tests
- `test_config set verbose on|off` - Enable/disable verbose output
- `test_config set timeout <ms>` - Set test timeout

### `test_results [verbose]`
Display test execution results and statistics.

## Expected Output

### Critical Tests (Fast Boot)
```
[INFO] Test Integration: Running critical system tests
[INFO] Test Integration: Scheduler test suite registered
[INFO] Test Integration: MPU test suite registered  
[INFO] Test Integration: TrustZone test suite registered
[INFO] Scheduler: ✓ init PASSED (145 μs)
[INFO] Scheduler: ✓ task_creation PASSED (2.1 ms)
[INFO] MPU: ✓ hardware_support PASSED (89 μs)
[INFO] TZ: ✓ hardware_support PASSED (76 μs)
[INFO] Test Integration: Critical tests completed in 156 ms: PASSED
```

### Comprehensive Tests (Development)
```
=================== SYSTEM TEST SUMMARY ===================
Overall Status: PASSED
Critical Tests: PASSED
Total Tests: 67 (Passed: 65, Failed: 0, Skipped: 2)
Execution Time: 3247 ms

Component Test Summary:
Component   | Registered | Critical | All Tests | Tests Run | Failed
------------+------------+----------+-----------+-----------+--------
scheduler   | Yes        | PASS     | PASS      | 22        | 0
mpu         | Yes        | PASS     | PASS      | 23        | 0  
tz          | Yes        | PASS     | PASS      | 22        | 0
============================================================
```

## Troubleshooting

### Common Issues

**Tests failing on MPU/TrustZone:**
- These components may not be available on all hardware
- Tests will skip gracefully if hardware support is not detected
- Check hardware capabilities and enable appropriate features

**Timeouts during stress tests:**
- Increase timeout values for slower hardware
- Reduce number of stress test iterations
- Consider disabling stress tests for production builds

**Memory allocation failures:**
- Ensure adequate heap space for test contexts
- Test framework uses minimal dynamic allocation
- Most test data is statically allocated

### Debug Configuration
```c
kernel_test_config_t debug_config = {
    .enable_runtime_tests = true,
    .run_critical_only = false,
    .abort_on_test_failure = true,
    .test_config.verbose_output = true,
    .test_config.run_stress_tests = false,  // Disable to isolate issues
    .test_config.run_fault_tests = false,   // Enable one category at a time
    .test_config.test_timeout_ms = 10000     // Longer timeout for debugging
};
```

## Performance Impact

### Critical Tests Only
- **Execution Time**: ~150-300ms
- **Memory Usage**: ~8KB additional RAM
- **Boot Delay**: Minimal impact on startup time

### Comprehensive Tests  
- **Execution Time**: ~2-5 seconds
- **Memory Usage**: ~16KB additional RAM
- **Boot Delay**: Significant but acceptable for development

### Disabled Tests
- **Execution Time**: 0ms
- **Memory Usage**: ~2KB (compiled but not executed)
- **Boot Delay**: No impact

## Safety Considerations

1. **Non-Destructive**: Tests are designed to verify functionality without damaging system state
2. **Timeout Protection**: All tests have configurable timeouts to prevent hangs
3. **Graceful Degradation**: System can continue operation even if some tests fail
4. **Minimal Side Effects**: Tests clean up after themselves and restore system state
5. **Hardware Protection**: Fault tests are carefully designed to avoid system crashes

## Future Enhancements

- **Test Result Persistence**: Save test results to flash memory
- **Runtime Health Monitoring**: Continuous component monitoring during operation  
- **Remote Test Execution**: Network-based test control and reporting
- **Custom Test Addition**: Framework for adding application-specific tests
- **Performance Benchmarking**: Detailed timing and performance metrics
- **Test Coverage Analysis**: Code coverage reporting for test suites

## Contributing

When adding new tests:

1. Follow the existing test patterns and naming conventions
2. Include appropriate test categories and severity levels
3. Add comprehensive error checking and cleanup
4. Document test purpose and expected behavior
5. Ensure tests are safe and non-destructive
6. Add shell command integration where appropriate
