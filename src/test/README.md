# Frontend Testing Suite

This directory contains comprehensive tests for the Halo Watch Patient Monitoring System frontend application.

## Test Structure

```
src/test/
├── __tests__/                 # Unit tests for components
├── integration/               # Integration tests
├── e2e/                      # End-to-end tests
├── utils.tsx                 # Test utilities and helpers
├── mocks.ts                  # Mock data and functions
├── setup.ts                  # Test setup configuration
└── README.md                 # This file
```

## Test Categories

### 1. Unit Tests
- **Authentication Components**: Login form, protected routes, user profile
- **Dashboard Components**: Patient panel, floor plan, alert management
- **UI Components**: Form validation, interactive elements
- **Utilities**: Helper functions, data transformations

### 2. Integration Tests
- **Authentication Flow**: Login/logout, token management, role-based access
- **Real-time Updates**: WebSocket connections, live data updates
- **API Integration**: Data fetching, error handling, state management

### 3. End-to-End Tests
- **Critical User Workflows**: Complete patient monitoring scenarios
- **Alert Management**: Alert creation, acknowledgment, resolution
- **Zone Management**: Zone configuration, patient assignments
- **System Administration**: User management, reporting

## Running Tests

```bash
# Run all tests
npm run test

# Run tests in watch mode
npm run test:watch

# Run tests once
npm run test:run

# Run specific test file
npm run test:run src/test/basic.test.tsx

# Run tests with coverage
npm run test:coverage

# Run tests with UI
npm run test:ui
```

## Test Configuration

### Vitest Configuration
- **Environment**: jsdom for DOM testing
- **Setup Files**: Automatic test setup and cleanup
- **Globals**: Global test functions available without imports
- **Path Aliases**: Same aliases as main application

### Mock Strategy
- **API Calls**: Mocked fetch responses with realistic data
- **WebSocket**: Mock WebSocket implementation for real-time testing
- **Authentication**: Mock auth context and user sessions
- **Local Storage**: Mock browser storage APIs

## Test Data

### Mock Patients
```typescript
const mockPatients = [
  {
    id: 'P001',
    name: 'John Doe',
    age: 45,
    status: 'normal',
    vitals: { heartRate: 72, temperature: 36.8 }
  }
  // ... more patients
]
```

### Mock Alerts
```typescript
const mockAlerts = [
  {
    id: 'A001',
    type: 'alert',
    title: 'High Heart Rate',
    message: 'Patient P002 heart rate exceeded 90 BPM'
  }
  // ... more alerts
]
```

## Testing Best Practices

### 1. Test Structure
- **Arrange**: Set up test data and mocks
- **Act**: Perform the action being tested
- **Assert**: Verify the expected outcome

### 2. Component Testing
- Test user interactions (clicks, form submissions)
- Verify component rendering with different props
- Test error states and edge cases
- Mock external dependencies

### 3. Integration Testing
- Test complete user workflows
- Verify data flow between components
- Test real-time updates and WebSocket connections
- Test authentication and authorization

### 4. Accessibility Testing
- Test keyboard navigation
- Verify ARIA labels and roles
- Test screen reader compatibility
- Check color contrast and visual indicators

## Test Coverage Goals

- **Unit Tests**: 80%+ coverage for components and utilities
- **Integration Tests**: Cover all critical user workflows
- **E2E Tests**: Cover main application features
- **Error Handling**: Test all error scenarios and edge cases

## Continuous Integration

Tests are configured to run automatically on:
- Pull requests
- Main branch commits
- Release builds

### CI Configuration
```yaml
# Example GitHub Actions workflow
- name: Run Tests
  run: |
    npm ci
    npm run test:run
    npm run test:coverage
```

## Debugging Tests

### Common Issues
1. **Mock Failures**: Ensure mocks are properly configured
2. **Async Issues**: Use proper async/await patterns
3. **DOM Cleanup**: Tests should clean up after themselves
4. **Timing Issues**: Use waitFor for async operations

### Debug Commands
```bash
# Run tests with verbose output
npm run test -- --reporter=verbose

# Run single test file with debugging
npm run test -- --run src/test/basic.test.tsx --reporter=verbose

# Run tests with coverage report
npm run test:coverage
```

## Future Enhancements

1. **Visual Regression Testing**: Screenshot comparison tests
2. **Performance Testing**: Component render performance
3. **Accessibility Automation**: Automated a11y testing
4. **Cross-browser Testing**: Multi-browser compatibility
5. **Mobile Testing**: Responsive design validation

## Contributing

When adding new features:
1. Write tests for new components
2. Update existing tests for modified components
3. Ensure all tests pass before submitting PR
4. Maintain test coverage above 80%
5. Document any new testing patterns or utilities