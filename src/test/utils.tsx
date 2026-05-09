import React, { ReactElement } from 'react'
import { render, RenderOptions } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { BrowserRouter } from 'react-router-dom'
import { AuthProvider } from '@/contexts/AuthContext'

// Mock user for testing
export const mockUser = {
  id: 'test-user-1',
  username: 'testuser',
  fullName: 'Test User',
  email: 'test@example.com',
  role: 'admin' as const,
}

// Mock auth context
export const MockAuthProvider: React.FC<{ children: React.ReactNode; user?: any }> = ({ 
  children, 
  user = mockUser 
}) => {
  const mockAuthValue = {
    user,
    isAuthenticated: !!user,
    isLoading: false,
    login: vi.fn(),
    logout: vi.fn(),
    refreshToken: vi.fn(),
  }

  return (
    <div data-testid="mock-auth-provider">
      {React.cloneElement(children as ReactElement, { authValue: mockAuthValue })}
    </div>
  )
}

// Custom render function with providers
const AllTheProviders: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const queryClient = new QueryClient({
    defaultOptions: {
      queries: {
        retry: false,
      },
    },
  })

  return (
    <QueryClientProvider client={queryClient}>
      <AuthProvider>
        <BrowserRouter>
          {children}
        </BrowserRouter>
      </AuthProvider>
    </QueryClientProvider>
  )
}

const customRender = (
  ui: ReactElement,
  options?: Omit<RenderOptions, 'wrapper'>
) => render(ui, { wrapper: AllTheProviders, ...options })

export * from '@testing-library/react'
export { customRender as render }