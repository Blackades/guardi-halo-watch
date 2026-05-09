import Cookies from 'js-cookie';

const API_BASE_URL =
  import.meta.env.VITE_API_BASE_URL ?? "http://localhost:8000";

const WS_URL =
  import.meta.env.VITE_WS_URL ?? "ws://localhost:8000/ws/live";

export const apiConfig = {
  API_BASE_URL,
  WS_URL,
};

// Helper function to get authenticated headers
export function getAuthHeaders(): HeadersInit {
  const token = Cookies.get('access_token');
  return {
    'Content-Type': 'application/json',
    ...(token && { 'Authorization': `Bearer ${token}` }),
  };
}

// Types mirror backend/backend/schemas.py (dashboard-facing DTOs)

export type RoomStatus = "normal" | "occupied" | "anomaly" | "warning";

export type RoomKind =
  | "patient_room"
  | "isolation"
  | "restricted"
  | "nurses_station"
  | "control_room"
  | "waiting_room"
  | "corridor"
  | "entry"
  | "washroom";

export interface RoomSummary {
  id: string;
  name: string;
  status: RoomStatus;
  position: { x: number; y: number };
  kind: RoomKind;
  patientId?: string;
}

export interface PatientSummary {
  id: string;
  name: string;
  age?: number;
  room?: string;
  status: RoomStatus;
  location?: string;
  lastActivity?: string;
  movementSteps?: number;
  strap_intact?: boolean;
}

export type NotificationType = "alert" | "warning" | "info" | "success";

export interface NotificationDto {
  id: string;
  type: NotificationType;
  title: string;
  message: string;
  timestamp: string; // ISO string from backend
  patientId?: string;
  room?: string;
  isRead: boolean;
}

export type AccessAction = "entry" | "exit" | "denied";

export interface AccessLogDto {
  id: string;
  patientId: string;
  patientName: string;
  room: string;
  action: AccessAction;
  timestamp: string; // ISO string from backend
  rfidId: string;
  duration?: string | null;
}

export interface OverviewStats {
  activePatients: number;
  availableRooms: number;
  activeAlerts: number;
  systemUptimeSeconds: number;
}

async function handleResponse<T>(res: Response): Promise<T> {
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    
    // Handle authentication errors
    if (res.status === 401) {
      // Clear tokens and redirect to login
      Cookies.remove('access_token');
      Cookies.remove('refresh_token');
      window.location.href = '/login';
      throw new Error('Authentication required');
    }
    
    throw new Error(
      `API error ${res.status}: ${res.statusText}${
        text ? ` - ${text}` : ""
      }`,
    );
  }
  return res.json() as Promise<T>;
}

export async function fetchOverview(): Promise<OverviewStats> {
  return handleResponse<OverviewStats>(
    await fetch(`${API_BASE_URL}/api/v1/overview`, {
      headers: getAuthHeaders(),
    }),
  );
}

export async function fetchRooms(): Promise<RoomSummary[]> {
  return handleResponse<RoomSummary[]>(
    await fetch(`${API_BASE_URL}/api/v1/rooms`, {
      headers: getAuthHeaders(),
    }),
  );
}

export async function fetchPatients(): Promise<PatientSummary[]> {
  return handleResponse<PatientSummary[]>(
    await fetch(`${API_BASE_URL}/api/v1/patients`, {
      headers: getAuthHeaders(),
    }),
  );
}

export async function fetchAlerts(
  status?: string,
): Promise<NotificationDto[]> {
  const url = new URL(`${API_BASE_URL}/api/v1/alerts`);
  if (status) url.searchParams.set("status", status);
  return handleResponse<NotificationDto[]>(
    await fetch(url.toString(), {
      headers: getAuthHeaders(),
    })
  );
}

export async function acknowledgeAlert(alertId: string): Promise<void> {
  await handleResponse(
    await fetch(
      `${API_BASE_URL}/api/v1/alerts/${encodeURIComponent(alertId)}/acknowledge`,
      { 
        method: "POST",
        headers: getAuthHeaders(),
      },
    ),
  );
}

export async function fetchAccessLogs(): Promise<AccessLogDto[]> {
  return handleResponse<AccessLogDto[]>(
    await fetch(`${API_BASE_URL}/api/v1/access-logs`, {
      headers: getAuthHeaders(),
    }),
  );
}

export async function deletePatient(patientId: string): Promise<void> {
  await handleResponse(
    await fetch(`${API_BASE_URL}/api/v1/patients/${encodeURIComponent(patientId)}`, {
      method: "DELETE",
      headers: getAuthHeaders(),
    }),
  );
}

export function createLiveWebSocket(): WebSocket | null {
  try {
    return new WebSocket(WS_URL);
  } catch {
    return null;
  }
}

export interface PatientAssignmentData {
  patient_id: string;
  name: string;
  ward?: string;
  ble_minor: number;
  rfid_uid: string;
}

export async function assignPatient(data: PatientAssignmentData): Promise<any> {
  return handleResponse(
    await fetch(`${API_BASE_URL}/api/v1/patients/assign`, {
      method: "POST",
      headers: getAuthHeaders(),
      body: JSON.stringify(data),
    }),
  );
}
