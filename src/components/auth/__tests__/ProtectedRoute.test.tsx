import { describe, it, expect, vi } from 'vitest'
import { screen } from '@testing-library/react'
import { render } from '@/test/utils'
import ProtectedRoute from '../ProtectedRoute'

// Mock the auth context
const mockAuthContext = {
  isAuthenticated: true,
  isLoading: false,
  user: {
    id: '1',
    username: 'testuser',
    fullName: 'Test User',
    email: 'test@example.com',
    role: 'admin' as const,
  },
  login: vi.fn(),
  logout: vi.fn(),
  refreshToken: vi.fn(),
}

vi.mock('@/contexts/AuthContext', () => ({
  useAuth: () => mockAuthContext,
}))

// Mock react-router-dom
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual('react-router-dom')
  return {
    ...actual,
    Navigate: ({ to }: { to: string }) => <div data-testid="navigate-to">{to}</div>,
  }
})

describe('ProtectedRoute', () => {
  it('renders children when user is authenticated', () => {
    render(
      <ProtectedRoute>
        <div>Protected Content</div>
      </ProtectedRoute>
    )
    
    expect(screen.getByText('Protected Content')).toBeInTheDocument()
  })

  it('shows loading state when auth is loading', () => {
    vi.mocked(require('@/contexts/AuthContext').useAuth).mockReturnValue({
      ...mockAuthContext,
      isLoading: true,
    })
    
    render(
      <ProtectedRoute>
        <div>Protected Content</div>
      </ProtectedRoute>
    )
    
    expect(screen.getByText('Loading...')).toBeInTheDocument()
  })

  it('redirects to login when user is not authenticated', () => {
    vi.mocked(require('@/contexts/AuthContext').useAuth).mockReturnValue({
      ...mockAuthContext,
      isAuthenticated: false,
      user: null,
    })
    
    render(
      <ProtectedRoute>
        <div>Protected Content</div>
      </ProtectedRoute>
    )
    
    expect(screen.getByTestId('navigate-to')).toHaveTextContent('/login')
  })

  it('shows access denied for insufficient role', () => {
    vi.mocked(require('@/contexts/AuthContext').useAuth).mockReturnValue({
      ...mockAuthContext,
      user: {
        ...mockAuthContext.user,
        role: 'viewer' as const,
      },
    })
    
    render(
      <ProtectedRoute allowedRoles={['admin', 'doctor']}>
        <div>Protected Content</div>
      </ProtectedRoute>
    )
    
    expect(screen.getByText('Access Denied')).toBeInTheDocument()
    expect(screen.getByText("You don't have permission to access this page.")).toBeInTheDocument()
  })

  it('renders children when user has required role', () => {
    render(
      <ProtectedRoute allowedRoles={['admin', 'doctor']}>
        <div>Protected Content</div>
      </ProtectedRoute>
    )
    
    expect(screen.getByText('Protected Content')).toBeInTheDocument()
  })
})