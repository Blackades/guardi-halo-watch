import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/react'

// Simple component for testing
const TestComponent = ({ message }: { message: string }) => {
  return <div data-testid="test-message">{message}</div>
}

describe('Basic Test Suite', () => {
  it('renders a simple component', () => {
    render(<TestComponent message="Hello, Testing!" />)
    
    expect(screen.getByTestId('test-message')).toBeInTheDocument()
    expect(screen.getByText('Hello, Testing!')).toBeInTheDocument()
  })

  it('performs basic assertions', () => {
    expect(1 + 1).toBe(2)
    expect('hello').toContain('ell')
    expect([1, 2, 3]).toHaveLength(3)
  })

  it('tests async functionality', async () => {
    const promise = Promise.resolve('async result')
    const result = await promise
    expect(result).toBe('async result')
  })
})