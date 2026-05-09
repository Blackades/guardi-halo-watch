import { describe, it, expect, vi, beforeEach } from 'vitest'
import { screen, fireEvent, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { render } from '@/test/utils'
import { setupMockFetch } from '@/test/mocks'
import LoginForm from '../LoginForm'

// Mock the auth context
vi.mock('@/contexts/AuthContext', () => ({
  useAuth: () => ({
    login: vi.fn(),
    isLoading: false,
  }),
}))

describe('LoginForm', () => {
  beforeEach(() => {
    setupMockFetch()
  })

  it('renders login form correctly', () => {
    render(<LoginForm />)
    
    expect(screen.getByText('Halo Watch')).toBeInTheDocument()
    expect(screen.getByText('Patient Monitoring System')).toBeInTheDocument()
    expect(screen.getByLabelText(/username/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/password/i)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /sign in/i })).toBeInTheDocument()
  })

  it('shows validation errors for empty fields', async () => {
    const user = userEvent.setup()
    render(<LoginForm />)
    
    const submitButton = screen.getByRole('button', { name: /sign in/i })
    await user.click(submitButton)
    
    await waitFor(() => {
      expect(screen.getByText('Username is required')).toBeInTheDocument()
      expect(screen.getByText('Password is required')).toBeInTheDocument()
    })
  })

  it('toggles password visibility', async () => {
    const user = userEvent.setup()
    render(<LoginForm />)
    
    const passwordInput = screen.getByLabelText(/password/i)
    const toggleButton = screen.getByRole('button', { name: '' }) // Eye icon button
    
    expect(passwordInput).toHaveAttribute('type', 'password')
    
    await user.click(toggleButton)
    expect(passwordInput).toHaveAttribute('type', 'text')
    
    await user.click(toggleButton)
    expect(passwordInput).toHaveAttribute('type', 'password')
  })

  it('submits form with valid credentials', async () => {
    const user = userEvent.setup()
    const mockLogin = vi.fn()
    
    vi.mocked(require('@/contexts/AuthContext').useAuth).mockReturnValue({
      login: mockLogin,
      isLoading: false,
    })
    
    render(<LoginForm />)
    
    await user.type(screen.getByLabelText(/username/i), 'testuser')
    await user.type(screen.getByLabelText(/password/i), 'password123')
    await user.click(screen.getByRole('button', { name: /sign in/i }))
    
    await waitFor(() => {
      expect(mockLogin).toHaveBeenCalledWith('testuser', 'password123')
    })
  })

  it('displays demo credentials', () => {
    render(<LoginForm />)
    
    expect(screen.getByText('Demo Credentials:')).toBeInTheDocument()
    expect(screen.getByText(/admin \/ admin123/)).toBeInTheDocument()
  })
})