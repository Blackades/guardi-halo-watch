import { vi } from 'vitest'

// Mock API responses
export const mockPatients = [
  {
    id: 'P001',
    name: 'John Doe',
    age: 45,
    room: 'Room 101',
    status: 'normal' as const,
    location: 'Ward A',
    lastActivity: '2 minutes ago',
    movementSteps: 1250,
    vitals: {
      heartRate: 72,
      temperature: 36.8,
    },
  },
  {
    id: 'P002',
    name: 'Jane Smith',
    age: 32,
    room: 'Room 102',
    status: 'warning' as const,
    location: 'Ward A',
    lastActivity: '5 minutes ago',
    movementSteps: 890,
    vitals: {
      heartRate: 95,
      temperature: 37.2,
    },
  },
]

export const mockRooms = [
  {
    id: 'R001',
    name: 'Room 101',
    status: 'occupied' as const,
    position: { x: 25, y: 30 },
    kind: 'patient_room' as const,
    patientId: 'P001',
  },
  {
    id: 'R002',
    name: 'Room 102',
    status: 'warning' as const,
    position: { x: 75, y: 30 },
    kind: 'patient_room' as const,
    patientId: 'P002',
  },
]

export const mockAlerts = [
  {
    id: 'A001',
    type: 'alert' as const,
    title: 'High Heart Rate',
    message: 'Patient P002 heart rate exceeded 90 BPM',
    timestamp: new Date().toISOString(),
    patientId: 'P002',
    room: 'Room 102',
    isRead: false,
  },
  {
    id: 'A002',
    type: 'warning' as const,
    title: 'Zone Violation',
    message: 'Patient P001 entered restricted area',
    timestamp: new Date(Date.now() - 300000).toISOString(),
    patientId: 'P001',
    room: 'Restricted Area',
    isRead: true,
  },
]

export const mockOverview = {
  activePatients: 2,
  availableRooms: 8,
  activeAlerts: 1,
  systemUptimeSeconds: 86400,
}

// Mock fetch responses
export const mockFetch = vi.fn()

global.fetch = mockFetch

export const setupMockFetch = () => {
  mockFetch.mockImplementation((url: string) => {
    if (url.includes('/api/v1/patients')) {
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve(mockPatients),
      })
    }
    
    if (url.includes('/api/v1/rooms')) {
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve(mockRooms),
      })
    }
    
    if (url.includes('/api/v1/alerts')) {
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve(mockAlerts),
      })
    }
    
    if (url.includes('/api/v1/overview')) {
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve(mockOverview),
      })
    }
    
    return Promise.resolve({
      ok: false,
      status: 404,
      json: () => Promise.resolve({ error: 'Not found' }),
    })
  })
}

// Mock WebSocket
export class MockWebSocket {
  static CONNECTING = 0
  static OPEN = 1
  static CLOSING = 2
  static CLOSED = 3

  readyState = MockWebSocket.CONNECTING
  onopen: ((event: Event) => void) | null = null
  onmessage: ((event: MessageEvent) => void) | null = null
  onclose: ((event: CloseEvent) => void) | null = null
  onerror: ((event: Event) => void) | null = null

  constructor(url: string) {
    setTimeout(() => {
      this.readyState = MockWebSocket.OPEN
      if (this.onopen) {
        this.onopen(new Event('open'))
      }
    }, 0)
  }

  send(data: string) {
    // Mock sending data
  }

  close() {
    this.readyState = MockWebSocket.CLOSED
    if (this.onclose) {
      this.onclose(new CloseEvent('close'))
    }
  }

  // Helper method to simulate receiving messages
  simulateMessage(data: any) {
    if (this.onmessage && this.readyState === MockWebSocket.OPEN) {
      this.onmessage(new MessageEvent('message', { data: JSON.stringify(data) }))
    }
  }
}

// Mock localStorage
export const mockLocalStorage = {
  getItem: vi.fn(),
  setItem: vi.fn(),
  removeItem: vi.fn(),
  clear: vi.fn(),
}

Object.defineProperty(window, 'localStorage', {
  value: mockLocalStorage,
})

// Mock matchMedia
Object.defineProperty(window, 'matchMedia', {
  writable: true,
  value: vi.fn().mockImplementation(query => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: vi.fn(),
    removeListener: vi.fn(),
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
    dispatchEvent: vi.fn(),
  })),
})