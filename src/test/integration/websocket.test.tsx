import React from 'react'
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { screen, waitFor } from '@testing-library/react'
import { render } from '@/test/utils'
import { MockWebSocket, setupMockFetch } from '@/test/mocks'
import NotificationPanel from '@/components/dashboard/notification-panel'
import * as api from '@/lib/api'

// Mock WebSocket globally
global.WebSocket = MockWebSocket as any

describe('WebSocket Real-time Updates', () => {
  let mockWebSocket: MockWebSocket

  beforeEach(() => {
    setupMockFetch()
    
    // Mock createLiveWebSocket to return our mock
    vi.mock('@/lib/api', async () => {
      const actual = await vi.importActual('@/lib/api')
      return {
        ...actual,
        createLiveWebSocket: () => {
          mockWebSocket = new MockWebSocket('ws://localhost:8000/ws/live')
          return mockWebSocket
        },
      }
    })
  })

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('receives and displays real-time alert notifications', async () => {
    render(<NotificationPanel />)
    
    await waitFor(() => {
      expect(screen.getByText('Alert Management')).toBeInTheDocument()
    })
    
    // Simulate receiving a WebSocket message
    const alertMessage = {
      event: 'alert_created',
      data: {
        id: 'A003',
        type: 'alert',
        title: 'Critical Alert',
        message: 'Patient vitals critical',
        timestamp: new Date().toISOString(),
        patientId: 'P003',
        room: 'Room 103',
        isRead: false,
      },
    }
    
    // Wait for WebSocket to be ready
    await waitFor(() => {
      expect(mockWebSocket.readyState).toBe(MockWebSocket.OPEN)
    })
    
    // Simulate receiving the message
    mockWebSocket.simulateMessage(alertMessage)
    
    // Check if the alert appears in the UI
    await waitFor(() => {
      expect(screen.getByText('Critical Alert')).toBeInTheDocument()
      expect(screen.getByText('Patient vitals critical')).toBeInTheDocument()
    })
  })

  it('handles WebSocket connection errors gracefully', async () => {
    // Mock WebSocket that fails to connect
    vi.spyOn(api, 'createLiveWebSocket').mockReturnValue(null)
    
    render(<NotificationPanel />)
    
    // Should still render the component even if WebSocket fails
    await waitFor(() => {
      expect(screen.getByText('Alert Management')).toBeInTheDocument()
    })
  })

  it('updates alert counts in real-time', async () => {
    render(<NotificationPanel />)
    
    await waitFor(() => {
      expect(mockWebSocket.readyState).toBe(MockWebSocket.OPEN)
    })
    
    // Send multiple alerts
    const alerts = [
      {
        event: 'alert_created',
        data: {
          id: 'A004',
          type: 'alert',
          title: 'High Heart Rate',
          message: 'Patient heart rate elevated',
          timestamp: new Date().toISOString(),
          patientId: 'P004',
          isRead: false,
        },
      },
      {
        event: 'alert_created',
        data: {
          id: 'A005',
          type: 'warning',
          title: 'Zone Violation',
          message: 'Unauthorized zone entry',
          timestamp: new Date().toISOString(),
          patientId: 'P005',
          isRead: false,
        },
      },
    ]
    
    for (const alert of alerts) {
      mockWebSocket.simulateMessage(alert)
    }
    
    // Check if both alerts appear
    await waitFor(() => {
      expect(screen.getByText('High Heart Rate')).toBeInTheDocument()
      expect(screen.getByText('Zone Violation')).toBeInTheDocument()
    })
  })

  it('plays sound for critical alerts', async () => {
    // Mock audio element
    const mockPlay = vi.fn()
    const mockAudio = {
      play: mockPlay,
      volume: 0,
    }
    
    vi.spyOn(React, 'useRef').mockReturnValue({ current: mockAudio })
    
    render(<NotificationPanel />)
    
    await waitFor(() => {
      expect(mockWebSocket.readyState).toBe(MockWebSocket.OPEN)
    })
    
    // Send critical alert
    const criticalAlert = {
      event: 'alert_created',
      data: {
        id: 'A006',
        type: 'alert',
        title: 'Emergency Alert',
        message: 'Patient emergency',
        timestamp: new Date().toISOString(),
        patientId: 'P006',
        isRead: false,
      },
    }
    
    mockWebSocket.simulateMessage(criticalAlert)
    
    await waitFor(() => {
      expect(mockPlay).toHaveBeenCalled()
    })
  })
})