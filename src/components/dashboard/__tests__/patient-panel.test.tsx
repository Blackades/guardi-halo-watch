import { describe, it, expect, vi, beforeEach } from 'vitest'
import { screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { render } from '@/test/utils'
import { setupMockFetch, mockPatients } from '@/test/mocks'
import PatientPanel from '../patient-panel'

// Mock the auth context
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
  }),
}))

describe('PatientPanel', () => {
  const mockOnPatientClick = vi.fn()

  beforeEach(() => {
    setupMockFetch()
    mockOnPatientClick.mockClear()
  })

  it('renders patient list correctly', async () => {
    render(<PatientPanel onPatientClick={mockOnPatientClick} />)
    
    expect(screen.getByText('Patient Monitoring')).toBeInTheDocument()
    expect(screen.getByPlaceholderText('Search patients...')).toBeInTheDocument()
    
    await waitFor(() => {
      expect(screen.getByText('John Doe')).toBeInTheDocument()
      expect(screen.getByText('Jane Smith')).toBeInTheDocument()
    })
  })

  it('filters patients based on search term', async () => {
    const user = userEvent.setup()
    render(<PatientPanel onPatientClick={mockOnPatientClick} />)
    
    await waitFor(() => {
      expect(screen.getByText('John Doe')).toBeInTheDocument()
      expect(screen.getByText('Jane Smith')).toBeInTheDocument()
    })
    
    const searchInput = screen.getByPlaceholderText('Search patients...')
    await user.type(searchInput, 'John')
    
    await waitFor(() => {
      expect(screen.getByText('John Doe')).toBeInTheDocument()
      expect(screen.queryByText('Jane Smith')).not.toBeInTheDocument()
    })
  })

  it('calls onPatientClick when patient is clicked', async () => {
    const user = userEvent.setup()
    render(<PatientPanel onPatientClick={mockOnPatientClick} />)
    
    await waitFor(() => {
      expect(screen.getByText('John Doe')).toBeInTheDocument()
    })
    
    await user.click(screen.getByText('John Doe'))
    expect(mockOnPatientClick).toHaveBeenCalledWith('P001')
  })

  it('highlights selected patient', async () => {
    render(<PatientPanel selectedPatientId="P001" onPatientClick={mockOnPatientClick} />)
    
    await waitFor(() => {
      const patientElement = screen.getByText('John Doe').closest('div')
      expect(patientElement).toHaveClass('bg-accent')
    })
  })

  it('displays patient vital signs', async () => {
    render(<PatientPanel onPatientClick={mockOnPatientClick} />)
    
    await waitFor(() => {
      expect(screen.getByText('72')).toBeInTheDocument() // Heart rate
      expect(screen.getByText('36.8°C')).toBeInTheDocument() // Temperature
    })
  })

  it('shows loading state', () => {
    // Mock loading state
    vi.mocked(global.fetch).mockImplementation(() => 
      new Promise(() => {}) // Never resolves to simulate loading
    )
    
    render(<PatientPanel onPatientClick={mockOnPatientClick} />)
    
    expect(screen.getByText('Loading...')).toBeInTheDocument()
  })

  it('switches between tabs correctly', async () => {
    const user = userEvent.setup()
    render(<PatientPanel selectedPatientId="P001" onPatientClick={mockOnPatientClick} />)
    
    // Should start on list tab
    expect(screen.getByRole('tab', { name: /patient list/i })).toHaveAttribute('data-state', 'active')
    
    // Click on details tab
    await user.click(screen.getByRole('tab', { name: /details/i }))
    
    await waitFor(() => {
      expect(screen.getByRole('tab', { name: /details/i })).toHaveAttribute('data-state', 'active')
    })
  })
})