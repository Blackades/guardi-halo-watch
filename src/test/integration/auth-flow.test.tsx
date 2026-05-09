import { describe, it, expect, vi, beforeEach } from 'vitest'
import { screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { render } from '@/test/utils'
import { setupMockFetch } from '@/test/mocks'
import App from '@/App'

describe('Authentication Flow Integration', () => {
  beforeEach(() => {
    setupMockFetch()
    
    // Mock successful login
    vi.mocked(global.fetch).mockImplementation((url: string | URL | Request) => {
      const urlString = url.toString()
      
      if (urlString.includes('/api/v1/auth/login')) {
        return Promise.resolve({
          ok: true,
          json: () => Promise.resolve({
            access_token: 'mock-access-token',
            refresh_token: 'mock-refresh-token',
          }),
        } as Response)
      }
      
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve([]),
      } as Response)
    })
  })

  it('redirects unauthenticated users to login', async () => {
    render(<App />)
    
    await waitFor(() => {
      expect(screen.getByText('Halo Watch')).toBeInTheDocument()
      expect(screen.getByText('Patient Monitoring System')).toBeInTheDocument()
    })
  })

  it('allows user to login and access dashboard', async () => {
    const user = userEvent.setup()
    render(<App />)
    
    // Wait for login page to load
    await waitFor(() => {
      expect(screen.getByLabelText(/username/i)).toBeInTheDocument()
    })
    
    // Fill in login form
    await user.type(screen.getByLabelText(/username/i), 'admin')
    await user.type(screen.getByLabelText(/password/i), 'admin123')
    await user.click(screen.getByRole('button', { name: /sign in/i }))
    
    // Should redirect to dashboard after successful login
    await waitFor(() => {
      expect(screen.getByText('Aga Khan Hospital')).toBeInTheDocument()
      expect(screen.getByText('Real-time Patient Monitoring System')).toBeInTheDocument()
    }, { timeout: 3000 })
  })

  it('shows error message for invalid credentials', async () => {
    const user = userEvent.setup()
    
    // Mock failed login
    vi.mocked(global.fetch).mockImplementation((url: string | URL | Request) => {
      const urlString = url.toString()
      
      if (urlString.includes('/api/v1/auth/login')) {
        return Promise.resolve({
          ok: false,
          status: 401,
          json: () => Promise.resolve({
            detail: 'Invalid credentials',
          }),
        } as Response)
      }
      
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve([]),
      } as Response)
    })
    
    render(<App />)
    
    await waitFor(() => {
      expect(screen.getByLabelText(/username/i)).toBeInTheDocument()
    })
    
    await user.type(screen.getByLabelText(/username/i), 'wronguser')
    await user.type(screen.getByLabelText(/password/i), 'wrongpass')
    await user.click(screen.getByRole('button', { name: /sign in/i }))
    
    await waitFor(() => {
      expect(screen.getByText(/invalid credentials/i)).toBeInTheDocument()
    })
  })

  it('allows user to logout', async () => {
    const user = userEvent.setup()
    
    // Mock authenticated state
    vi.mock('@/contexts/AuthContext', () => ({
      useAuth: () => ({
        user: {
          id: '1',
          username: 'testuser',
          fullName: 'Test User',
          email: 'test@example.com',
          role: 'admin',
        },
        isAuthenticated: true,
        isLoading: false,
        logout: vi.fn(),
      }),
      AuthProvider: ({ children }: { children: React.ReactNode }) => <>{children}</>,
    }))
    
    render(<App />)
    
    await waitFor(() => {
      expect(screen.getByText('Aga Khan Hospital')).toBeInTheDocument()
    })
    
    // Click on user profile
    const userButton = screen.getByText('Test User')
    await user.click(userButton)
    
    // Click logout button
    await waitFor(() => {
      const logoutButton = screen.getByRole('button', { name: /sign out/i })
      expect(logoutButton).toBeInTheDocument()
    })
  })
})