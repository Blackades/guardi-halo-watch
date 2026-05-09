import { describe, it, expect, vi, beforeEach } from 'vitest'
import { screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { render } from '@/test/utils'
import { setupMockFetch, mockPatients, mockAlerts } from '@/test/mocks'
import Index from '@/pages/Index'

// Mock auth context with admin user
vi.mock('@/contexts/AuthContext', () => ({
  useAuth: () => ({
    user: {
      id: '1',
      username: 'admin',
      fullName: 'Admin User',
      email: 'admin@example.com',
      role: 'admin',
    },
    isAuthenticated: true,
    isLoading: false,
    logout: vi.fn(),
  }),
}))

describe('Critical User Workflows E2E', () => {
  beforeEach(() => {
    setupMockFetch()
  })

  it('complete patient monitoring workflow', async () => {
    const user = userEvent.setup()
    render(<Index />)
    
    // Wait for dashboard to load
    await waitFor(() => {
      expect(screen.getByText('Aga Khan Hospital')).toBeInTheDocument()
    })
    
    // 1. View patient list
    await waitFor(() => {
      expect(screen.getByText('John Doe')).toBeInTheDocument()
      expect(screen.getByText('Jane Smith')).toBeInTheDocument()
    })
    
    // 2. Select a patient
    await user.click(screen.getByText('John Doe'))
    
    // 3. View patient details
    await waitFor(() => {
      expect(screen.getByText('P001')).toBeInTheDocument()
    })
    
    // 4. Check floor plan shows selected patient
    const floorPlan = screen.getByText('Hospital Floor Plan - Ward A')
    expect(floorPlan).toBeInTheDocument()
    
    // 5. View patient vital signs
    await waitFor(() => {
      expect(screen.getByText('72')).toBeInTheDocument() // Heart rate
      expect(screen.getByText('36.8°C')).toBeInTheDocument() // Temperature
    })
  })

  it('alert management workflow', async () => {
    const user = userEvent.setup()
    render(<Index />)
    
    await waitFor(() => {
      expect(screen.getByText('Alert Management')).toBeInTheDocument()
    })
    
    // 1. View active alerts
    await waitFor(() => {
      expect(screen.getByText('High Heart Rate')).toBeInTheDocument()
    })
    
    // 2. Acknowledge an alert
    const acknowledgeButton = screen.getAllByTitle('Acknowledge')[0]
    await user.click(acknowledgeButton)
    
    // 3. Filter alerts
    const filterSelect = screen.getByDisplayValue('All')
    await user.click(filterSelect)
    await user.click(screen.getByText('Unread'))
    
    // 4. Search alerts
    const searchInput = screen.getByPlaceholderText('Search alerts...')
    await user.type(searchInput, 'heart rate')
    
    await waitFor(() => {
      expect(screen.getByText('High Heart Rate')).toBeInTheDocument()
    })
  })

  it('zone management workflow', async () => {
    const user = userEvent.setup()
    render(<Index />)
    
    await waitFor(() => {
      expect(screen.getByText('Zone Management')).toBeInTheDocument()
    })
    
    // 1. View zones tab
    const zonesTab = screen.getByRole('tab', { name: /zones/i })
    await user.click(zonesTab)
    
    // 2. Create new zone (admin only)
    const createButton = screen.getByText('Create Zone')
    await user.click(createButton)
    
    // 3. Fill zone form
    await waitFor(() => {
      expect(screen.getByLabelText('Zone Name')).toBeInTheDocument()
    })
    
    await user.type(screen.getByLabelText('Zone Name'), 'Test Zone')
    
    // 4. Select zone type
    const typeSelect = screen.getByDisplayValue('Select zone type')
    await user.click(typeSelect)
    await user.click(screen.getByText('Restricted'))
    
    // 5. Submit form
    const submitButton = screen.getByText('Create Zone')
    await user.click(submitButton)
  })

  it('floor plan interaction workflow', async () => {
    const user = userEvent.setup()
    render(<Index />)
    
    await waitFor(() => {
      expect(screen.getByText('Hospital Floor Plan - Ward A')).toBeInTheDocument()
    })
    
    // 1. Toggle view modes
    const roomsButton = screen.getByText('Rooms')
    await user.click(roomsButton)
    
    const patientsButton = screen.getByText('Patients')
    await user.click(patientsButton)
    
    const bothButton = screen.getByText('Both')
    await user.click(bothButton)
    
    // 2. Toggle zones
    const zonesButton = screen.getByText('Zones')
    await user.click(zonesButton)
    
    // 3. Zoom controls
    const zoomInButton = screen.getByLabelText('Zoom in') || screen.getAllByRole('button').find(btn => btn.textContent?.includes('Zoom'))
    if (zoomInButton) {
      await user.click(zoomInButton)
    }
  })

  it('system overview and statistics workflow', async () => {
    render(<Index />)
    
    // 1. Check system status
    await waitFor(() => {
      expect(screen.getByText('System Online')).toBeInTheDocument()
    })
    
    // 2. View statistics cards
    expect(screen.getByText('Active Patients')).toBeInTheDocument()
    expect(screen.getByText('Available Rooms')).toBeInTheDocument()
    expect(screen.getByText('Active Alerts')).toBeInTheDocument()
    expect(screen.getByText('System Uptime')).toBeInTheDocument()
    
    // 3. Check real-time data updates
    await waitFor(() => {
      const activePatients = screen.getByText('2') // From mock data
      expect(activePatients).toBeInTheDocument()
    })
  })

  it('user profile and logout workflow', async () => {
    const user = userEvent.setup()
    render(<Index />)
    
    await waitFor(() => {
      expect(screen.getByText('Admin User')).toBeInTheDocument()
    })
    
    // 1. Open user profile
    const userButton = screen.getByText('Admin User')
    await user.click(userButton)
    
    // 2. View profile information
    await waitFor(() => {
      expect(screen.getByText('User Profile')).toBeInTheDocument()
      expect(screen.getByDisplayValue('admin')).toBeInTheDocument()
    })
    
    // 3. Update profile (if form is visible)
    const fullNameInput = screen.getByLabelText('Full Name')
    await user.clear(fullNameInput)
    await user.type(fullNameInput, 'Updated Admin User')
    
    // 4. Save changes
    const saveButton = screen.getByText('Update Profile')
    await user.click(saveButton)
  })

  it('responsive design and mobile workflow', async () => {
    // Mock mobile viewport
    Object.defineProperty(window, 'innerWidth', {
      writable: true,
      configurable: true,
      value: 375,
    })
    
    Object.defineProperty(window, 'innerHeight', {
      writable: true,
      configurable: true,
      value: 667,
    })
    
    render(<Index />)
    
    await waitFor(() => {
      expect(screen.getByText('Aga Khan Hospital')).toBeInTheDocument()
    })
    
    // Check that components adapt to mobile layout
    // This would require more specific mobile-responsive design tests
    // based on the actual responsive breakpoints used
  })
})