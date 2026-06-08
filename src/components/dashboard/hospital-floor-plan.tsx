import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { ZoomIn, ZoomOut, RotateCcw, MapPin, Users } from "lucide-react";
import {
  fetchRooms,
  fetchPatients,
  type RoomSummary,
  type PatientSummary,
} from "@/lib/api";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function cn(...classes: (string | undefined | boolean | null)[]): string {
  return classes.filter(Boolean).join(" ");
}

// Matches room data to patients using room id, name, or location fields
function getPatientsInRoom(
  room: RoomSummary,
  patients: PatientSummary[]
): PatientSummary[] {
  const roomId = room.id.toLowerCase();
  const roomName = room.name.toLowerCase();
  return patients.filter((p) => {
    const pRoom = (p.room ?? "").toLowerCase();
    const pLoc = (p.location ?? "").toLowerCase();
    return (
      (pRoom && (pRoom === roomId || pRoom === roomName)) ||
      (pLoc &&
        (pLoc === roomName ||
          roomName.includes(pLoc) ||
          pLoc.includes(roomName)))
    );
  });
}

function deriveStatus(
  room: RoomSummary,
  patients: PatientSummary[]
): RoomSummary["status"] {
  const here = getPatientsInRoom(room, patients);
  if (here.length === 0) return "normal";
  if (here.some((p) => p.status === "anomaly" || p.strap_intact === false)) return "anomaly";
  if (here.some((p) => p.status === "warning")) return "warning";
  return "occupied";
}

function getLabel(room: RoomSummary): string {
  switch (room.kind) {
    case "patient_room":
      return room.name.replace(/[^0-9]/g, "") || room.id.slice(-2);
    case "isolation":
      return "ISO";
    case "washroom":
      return "WC";
    case "restricted":
      return "R";
    case "nurses_station":
      return "NS";
    case "control_room":
      return "CR";
    case "waiting_room":
      return "W";
    case "entry":
      return "E";
    case "corridor":
      return "C";
    default:
      return room.id.slice(-2).toUpperCase();
  }
}

// ---------------------------------------------------------------------------
// Floor plan geometry
//
// Derived from the physical wooden model:
//   - Two columns of 3 rooms each (left wing, right wing)
//   - A full-height central corridor between them (no subdivisions)
//   - Entry portal cut into the bottom-center of the outer wall
//
// SVG viewBox: 0 0 520 380
// Left wing:   x=20,  width=156  (rooms stacked at y=20,127,240)
// Corridor:    x=176, width=168  (full height, y=20..360)
// Right wing:  x=344, width=156  (rooms stacked at y=20,127,240)
// ---------------------------------------------------------------------------

interface RoomSlot {
  /** Unique key used to find the matching RoomSummary from the API */
  matchKey: string;
  /** Fallback label when no API room matches */
  fallbackLabel: string;
  x: number;
  y: number;
  width: number;
  height: number;
  /** Which side of the corridor this room is on */
  wing: "left" | "right";
  /** Door gap on the corridor wall: corridor-side x, and y range */
  doorY: number;
}

// Static layout — 3 left + 3 right, matches the physical model exactly
const ROOM_SLOTS: RoomSlot[] = [
  // Left wing
  { matchKey: "REST", fallbackLabel: "REST", x: 20, y: 20, width: 156, height: 107, wing: "left", doorY: 55 },
  { matchKey: "R01", fallbackLabel: "R01", x: 20, y: 127, width: 156, height: 113, wing: "left", doorY: 162 },
  { matchKey: "R02", fallbackLabel: "R02", x: 20, y: 240, width: 156, height: 120, wing: "left", doorY: 275 },
  // Right wing
  { matchKey: "NURSE", fallbackLabel: "NURSE", x: 344, y: 20, width: 156, height: 107, wing: "right", doorY: 55 },
  { matchKey: "ISO", fallbackLabel: "ISO", x: 344, y: 127, width: 156, height: 113, wing: "right", doorY: 162 },
  { matchKey: "R03", fallbackLabel: "R03", x: 344, y: 240, width: 156, height: 120, wing: "right", doorY: 275 },
];

// Map slot index to the nth room of each wing from the API results
function assignRoomsToSlots(
  rooms: RoomSummary[]
): Map<string, RoomSummary | undefined> {
  const map = new Map<string, RoomSummary | undefined>();
  rooms.forEach((r) => {
    map.set(r.id, r);
  });
  return map;
}

// ---------------------------------------------------------------------------
// Status → visual colours (Tailwind utility classes + inline fills for SVG)
// ---------------------------------------------------------------------------

type StatusKey = RoomSummary["status"] | "restricted" | "isolation";

const STATUS_FILL: Record<StatusKey, string> = {
  normal: "#dcfce7",
  occupied: "#dbeafe",
  warning: "#fef9c3",
  anomaly: "#fee2e2",
  restricted: "#fee2e2",
  isolation: "#fef9c3",
};

const STATUS_TEXT: Record<StatusKey, string> = {
  normal: "#15803d",
  occupied: "#1e40af",
  warning: "#854d0e",
  anomaly: "#991b1b",
  restricted: "#991b1b",
  isolation: "#854d0e",
};

const STATUS_DOT: Record<StatusKey, string> = {
  normal: "#22c55e",
  occupied: "#3b82f6",
  warning: "#f59e0b",
  anomaly: "#ef4444",
  restricted: "#ef4444",
  isolation: "#f59e0b",
};

const STATUS_LABEL: Record<StatusKey, string> = {
  normal: "Available",
  occupied: "Occupied",
  warning: "Warning",
  anomaly: "Alert",
  restricted: "Restricted",
  isolation: "Isolation",
};

function resolveStatusKey(room: RoomSummary, derived: RoomSummary["status"]): StatusKey {
  if (room.kind === "restricted") return "restricted";
  if (room.kind === "isolation") return "isolation";
  return derived as StatusKey;
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

interface HospitalFloorPlanProps {
  selectedPatientId?: string;
  onRoomClick?: (room: RoomSummary) => void;
  onPatientClick?: (patient: PatientSummary) => void;
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export default function HospitalFloorPlan({
  selectedPatientId,
  onRoomClick,
  onPatientClick,
}: HospitalFloorPlanProps) {
  const [hoveredSlot, setHoveredSlot] = useState<string | null>(null);
  const [hoveredPatient, setHoveredPatient] = useState<string | null>(null);
  const [zoomLevel, setZoomLevel] = useState(1);
  const [viewMode, setViewMode] = useState<"rooms" | "patients" | "both">("both");
  const [pulseFrame, setPulseFrame] = useState(0);

  const { data: rooms = [], isLoading, isError } = useQuery<RoomSummary[]>({
    queryKey: ["rooms"],
    queryFn: fetchRooms,
  });

  const { data: patients = [] } = useQuery<PatientSummary[]>({
    queryKey: ["patients"],
    queryFn: fetchPatients,
  });

  // Animate alert pulse every 600 ms
  useState(() => {
    const id = setInterval(() => setPulseFrame((f) => f + 1), 600);
    return () => clearInterval(id);
  });

  const slotRoomMap = assignRoomsToSlots(rooms);

  const handleZoomIn = () => setZoomLevel((z) => Math.min(z + 0.2, 2.5));
  const handleZoomOut = () => setZoomLevel((z) => Math.max(z - 0.2, 0.4));
  const handleResetZoom = () => setZoomLevel(1);

  // -------------------------------------------------------------------------
  // Render helpers
  // -------------------------------------------------------------------------

  const renderDoor = (slot: RoomSlot) => {
    const corridorX = slot.wing === "left" ? 173 : 341;
    const innerX = slot.wing === "left" ? 173 : 344;
    return (
      <g key={`door-${slot.matchKey}`}>
        <rect x={corridorX} y={slot.doorY} width={6} height={28} fill="#e2e8f0" />
        <rect x={innerX} y={slot.doorY} width={3} height={28} fill="#f8fafc" />
      </g>
    );
  };

  const renderRoom = (slot: RoomSlot) => {
    const room = slotRoomMap.get(slot.matchKey);
    const patientsHere = room ? getPatientsInRoom(room, patients) : [];
    const derived = room ? deriveStatus(room, patients) : "normal";
    const sk = room ? resolveStatusKey(room, derived) : "normal";
    const isHovered = hoveredSlot === slot.matchKey;
    const hasSelectedPatient =
      !!selectedPatientId && patientsHere.some((p) => p.id === selectedPatientId);
    
    // Check if room is occupied by a foreign patient
    const hasForeignPatient = room ? patientsHere.some((p) => p.id !== room.patientId) : false;
    const isAlert = sk === "anomaly" || sk === "restricted";
    const pulseOn = isAlert && pulseFrame % 2 === 0;

    const roomFill = hasForeignPatient ? "#f5d0fe" : (pulseOn ? "#fca5a5" : STATUS_FILL[sk]);
    const roomStroke = hasForeignPatient ? "#d946ef" : (hasSelectedPatient ? "#6366f1" : "#334155");
    const roomStrokeWidth = hasSelectedPatient ? 3 : (isHovered || hasForeignPatient ? 2.5 : 2);
    const roomStrokeDash = hasForeignPatient ? "4 2" : (hasSelectedPatient ? "6 3" : undefined);
    const textFill = hasForeignPatient ? "#86198f" : STATUS_TEXT[sk];
    const dotFill = hasForeignPatient ? "#d946ef" : STATUS_DOT[sk];
    const labelText = hasForeignPatient ? "Foreign Occupant" : STATUS_LABEL[sk];

    const cx = slot.x + slot.width / 2;
    const cy = slot.y + slot.height / 2;

    // Tooltip position: flip left for right-wing rooms
    const ttX = slot.wing === "left" ? slot.x + slot.width + 4 : slot.x - 4;
    const ttAnchor = slot.wing === "left" ? "start" : "end";

    return (
      <g
        key={slot.matchKey}
        className="cursor-pointer"
        role="button"
        tabIndex={0}
        aria-label={room?.name ?? slot.fallbackLabel}
        onClick={() => room && onRoomClick?.(room)}
        onMouseEnter={() => setHoveredSlot(slot.matchKey)}
        onMouseLeave={() => setHoveredSlot(null)}
        onKeyDown={(e) => {
          if ((e.key === "Enter" || e.key === " ") && room) onRoomClick?.(room);
        }}
      >
        {/* Room body */}
        <rect
          x={slot.x}
          y={slot.y}
          width={slot.width}
          height={slot.height}
          fill={roomFill}
          stroke={roomStroke}
          strokeWidth={roomStrokeWidth}
          strokeDasharray={roomStrokeDash}
          style={{ transition: "fill 0.3s" }}
        />

        {/* Status dot */}
        <circle
          cx={cx - 18}
          cy={cy + slot.height * 0.18}
          r={5}
          fill={dotFill}
        />
        {/* Alert pulse ring */}
        {isAlert && !hasForeignPatient && (
          <circle
            cx={cx - 18}
            cy={cy + slot.height * 0.18}
            r={pulseOn ? 9 : 5}
            fill="none"
            stroke={STATUS_DOT[sk]}
            strokeWidth={1}
            strokeOpacity={pulseOn ? 0.3 : 0}
            style={{ transition: "r 0.3s, stroke-opacity 0.3s" }}
          />
        )}

        {/* Room name */}
        <text
          x={cx}
          y={cy - 8}
          textAnchor="middle"
          fontSize={13}
          fontWeight={500}
          fill={textFill}
        >
          {room?.name ?? slot.fallbackLabel}
        </text>

        {/* Status label */}
        <text
          x={cx}
          y={cy + 8}
          textAnchor="middle"
          fontSize={10}
          fill={textFill}
        >
          {labelText}
          {patientsHere.length > 0 ? ` · ${patientsHere.length}pt` : ""}
        </text>

        {/* Hover tooltip */}
        {isHovered && room && (
          <g>
            <rect
              x={ttAnchor === "start" ? ttX : ttX - 130}
              y={cy - 22}
              width={130}
              height={42}
              fill="white"
              stroke={hasForeignPatient ? "#d946ef" : "#e2e8f0"}
              strokeWidth={0.8}
              rx={3}
              filter="url(#shadow)"
            />
            <text
              x={ttAnchor === "start" ? ttX + 6 : ttX - 6}
              y={cy - 6}
              textAnchor={ttAnchor}
              fontSize={11}
              fontWeight={500}
              fill="#111827"
            >
              {room.name}
            </text>
            <text
              x={ttAnchor === "start" ? ttX + 6 : ttX - 6}
              y={cy + 8}
              textAnchor={ttAnchor}
              fontSize={10}
              fill={hasForeignPatient ? "#86198f" : "#6b7280"}
            >
              {labelText} · {patientsHere.length} patient
              {patientsHere.length !== 1 ? "s" : ""}
            </text>
          </g>
        )}

        {/* Patient markers (dots inside room) */}
        {viewMode !== "rooms" &&
          patientsHere.map((patient, idx) => {
            const px = slot.x + 14 + (idx % 4) * 14;
            const py = slot.y + slot.height - 20;
            const isSelPat = selectedPatientId === patient.id;
            const isHovPat = hoveredPatient === patient.id;
            
            // Check if patient is foreign to this room
            const isForeign = room ? patient.id !== room.patientId : false;
            const pColor = isForeign
              ? "#ef4444"
              : patient.status === "anomaly"
                ? "#ef4444"
                : patient.status === "warning"
                  ? "#f59e0b"
                  : "#3b82f6";
            
            const isRightWing = slot.wing === "right";
            const ttPatientX = isRightWing ? px - 180 : px + 10;

            return (
              <g
                key={patient.id}
                onClick={(e) => {
                  e.stopPropagation();
                  onPatientClick?.(patient);
                }}
                onMouseEnter={() => setHoveredPatient(patient.id)}
                onMouseLeave={() => setHoveredPatient(null)}
                className="cursor-pointer"
              >
                {/* Outer pulsing ring for foreign patient */}
                {isForeign && (
                  <circle
                    cx={px}
                    cy={py}
                    r={pulseFrame % 2 === 0 ? 9 : 5}
                    fill="none"
                    stroke="#ef4444"
                    strokeWidth={1.5}
                    strokeOpacity={pulseFrame % 2 === 0 ? 0.6 : 0.1}
                    style={{ transition: "all 0.3s" }}
                  />
                )}
                <circle
                  cx={px}
                  cy={py}
                  r={isSelPat ? 5 : isHovPat ? 4.5 : 4}
                  fill={pColor}
                  stroke="white"
                  strokeWidth={1}
                />
                
                {/* Full detailed patient tooltip on hover */}
                {isHovPat && (
                  <g>
                    <rect
                      x={ttPatientX}
                      y={py - 35}
                      width={170}
                      height={75}
                      fill="white"
                      stroke={pColor}
                      strokeWidth={1}
                      rx={4}
                      filter="url(#shadow)"
                    />
                    <text x={ttPatientX + 8} y={py - 20} fontSize={11} fontWeight={600} fill="#111827">
                      {patient.name}
                    </text>
                    <text x={ttPatientX + 8} y={py - 7} fontSize={9} fill="#4b5563">
                      ID: {patient.id}
                    </text>
                    <text x={ttPatientX + 8} y={py + 6} fontSize={9} fill="#4b5563">
                      Assigned Room: {rooms.find((r) => r.patientId === patient.id)?.name || "None"}
                    </text>
                    <text x={ttPatientX + 8} y={py + 19} fontSize={9} fill={isForeign ? "#ef4444" : "#6b7280"} fontWeight={isForeign ? 500 : 400}>
                      {isForeign ? "Unauthorized Entry" : `Last Activity: ${patient.lastActivity || "Just now"}`}
                    </text>
                  </g>
                )}
              </g>
            );
          })}
      </g>
    );
  };

  // -------------------------------------------------------------------------
  // Counts for legend strip
  // -------------------------------------------------------------------------

  const counts = {
    available: rooms.filter((r) => deriveStatus(r, patients) === "normal").length,
    occupied: rooms.filter((r) => deriveStatus(r, patients) === "occupied").length,
    warning: rooms.filter((r) => deriveStatus(r, patients) === "warning").length,
    alert: rooms.filter((r) => deriveStatus(r, patients) === "anomaly").length,
  };

  // -------------------------------------------------------------------------
  // JSX
  // -------------------------------------------------------------------------

  return (
    <Card className="h-full">
      <CardHeader className="pb-2">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <CardTitle className="flex items-center gap-2 text-base">
            <MapPin className="h-4 w-4" />
            Ward A — Floor Plan
          </CardTitle>

          <div className="flex items-center gap-2">
            {/* View mode */}
            <div className="flex gap-1">
              {(["rooms", "patients", "both"] as const).map((mode) => (
                <Button
                  key={mode}
                  variant={viewMode === mode ? "default" : "outline"}
                  size="sm"
                  onClick={() => setViewMode(mode)}
                  className="capitalize"
                >
                  {mode === "patients" && <Users className="h-3 w-3 mr-1" />}
                  {mode}
                </Button>
              ))}
            </div>

            {/* Zoom controls */}
            <div className="flex gap-1">
              <Button variant="outline" size="sm" onClick={handleZoomOut} aria-label="Zoom out">
                <ZoomOut className="h-4 w-4" />
              </Button>
              <Button variant="outline" size="sm" onClick={handleResetZoom} aria-label="Reset zoom">
                <RotateCcw className="h-4 w-4" />
              </Button>
              <Button variant="outline" size="sm" onClick={handleZoomIn} aria-label="Zoom in">
                <ZoomIn className="h-4 w-4" />
              </Button>
            </div>
          </div>
        </div>

        {/* Legend */}
        <div className="flex flex-wrap gap-3 text-xs text-muted-foreground pt-1">
          {[
            { label: "Available", color: "#22c55e", count: counts.available },
            { label: "Occupied", color: "#3b82f6", count: counts.occupied },
            { label: "Warning", color: "#f59e0b", count: counts.warning },
            { label: "Alert", color: "#ef4444", count: counts.alert },
          ].map(({ label, color, count }) => (
            <span key={label} className="flex items-center gap-1">
              <span
                className="inline-block w-2.5 h-2.5 rounded-full"
                style={{ background: color }}
              />
              {label}
              {count > 0 && (
                <Badge variant="secondary" className="text-[10px] px-1 py-0 h-4">
                  {count}
                </Badge>
              )}
            </span>
          ))}
        </div>
      </CardHeader>

      <CardContent className="p-3">
        <div className="relative w-full rounded-lg overflow-hidden bg-slate-50 border border-border shadow-sm">
          {isLoading && (
            <div className="absolute inset-0 flex items-center justify-center z-10 bg-slate-50/80">
              <div className="w-6 h-6 border-2 border-primary border-t-transparent rounded-full animate-spin mr-2" />
              <span className="text-sm text-muted-foreground">Loading ward layout…</span>
            </div>
          )}
          {isError && (
            <div className="absolute inset-0 flex items-center justify-center z-10">
              <span className="text-sm text-destructive">Unable to load ward layout</span>
            </div>
          )}

          {/* ----------------------------------------------------------------
              SVG Floor Plan
              ViewBox: 520 × 380
              Left wing  x=20..176   (width 156)
              Corridor   x=176..344  (width 168, full height)
              Right wing x=344..500  (width 156)
              ---------------------------------------------------------------- */}
          <svg
            viewBox="0 0 520 380"
            className="w-full h-auto"
            style={{ transform: `scale(${zoomLevel})`, transformOrigin: "top left", transition: "transform 0.2s" }}
            aria-label="Hospital Ward A floor plan"
            role="img"
          >
            <defs>
              {/* Subtle hatch for corridor */}
              <pattern id="hatch" patternUnits="userSpaceOnUse" width="6" height="6" patternTransform="rotate(45)">
                <line x1="0" y1="0" x2="0" y2="6" stroke="#94a3b8" strokeWidth={0.6} strokeOpacity={0.3} />
              </pattern>
              <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
                <feDropShadow dx="0" dy="2" stdDeviation="3" floodOpacity="0.15" />
              </filter>
            </defs>

            {/* Outer building shell */}
            <rect x={20} y={20} width={480} height={340} rx={3} fill="#f8fafc" stroke="#334155" strokeWidth={3} />

            {/* Corridor fill */}
            <rect x={176} y={20} width={168} height={340} fill="url(#hatch)" />
            <rect x={176} y={20} width={168} height={340} fill="#e2e8f0" fillOpacity={0.45} />

            {/* Corridor label */}
            <text x={260} y={200} textAnchor="middle" fontSize={11} fill="#64748b" fontWeight={500} letterSpacing="0.08em">
              CORRIDOR
            </text>
            <line x1={210} y1={204} x2={240} y2={204} stroke="#94a3b8" strokeWidth={0.8} strokeDasharray="3 2" />
            <line x1={280} y1={204} x2={310} y2={204} stroke="#94a3b8" strokeWidth={0.8} strokeDasharray="3 2" />
            {/* Rooms */}
            {viewMode !== "patients" && ROOM_SLOTS.map(renderRoom)}

            {/* Structural walls — corridor dividers */}
            <line x1={176} y1={20} x2={176} y2={360} stroke="#334155" strokeWidth={2.5} />
            <line x1={344} y1={20} x2={344} y2={360} stroke="#334155" strokeWidth={2.5} />

            {/* Horizontal dividers — left wing */}
            <line x1={20} y1={127} x2={176} y2={127} stroke="#334155" strokeWidth={2} />
            <line x1={20} y1={240} x2={176} y2={240} stroke="#334155" strokeWidth={2} />

            {/* Horizontal dividers — right wing */}
            <line x1={344} y1={127} x2={500} y2={127} stroke="#334155" strokeWidth={2} />
            <line x1={344} y1={240} x2={500} y2={240} stroke="#334155" strokeWidth={2} />

            {/* Doorways */}
            {ROOM_SLOTS.map(renderDoor)}

            {/* Entry portal — bottom-centre opening (matches Image 2 front view) */}
            {/* Break bottom wall */}
            <rect x={214} y={353} width={92} height={10} fill="#f8fafc" />
            {/* Portal frame jambs */}
            <rect x={214} y={348} width={4} height={14} fill="#334155" />
            <rect x={302} y={348} width={4} height={14} fill="#334155" />
            {/* Portal lintel */}
            <rect x={214} y={348} width={92} height={4} fill="#334155" />
            {/* Entry label */}
            <text x={260} y={374} textAnchor="middle" fontSize={10} fill="#64748b">
              ENTRY / EXIT
            </text>
            <path d="M 235 369 L 260 375 L 285 369" fill="none" stroke="#94a3b8" strokeWidth={1} strokeLinejoin="round" />

            {/* North indicator */}
            <g transform="translate(490, 38)">
              <circle cx={0} cy={0} r={14} fill="#f1f5f9" stroke="#cbd5e1" strokeWidth={0.8} />
              <text x={0} y={4} textAnchor="middle" fontSize={9} fill="#475569" fontWeight={500}>N</text>
              <path d="M 0 -12 L 3 -4 L 0 -7 L -3 -4 Z" fill="#334155" />
            </g>

            {/* Corridor Patients (pulsing red dots) — rendered at the very end to stay on top of right wing */}
            {viewMode !== "rooms" &&
              patients
                .filter((p) => (p.room ?? "").toLowerCase() === "corr" || (p.location ?? "").toLowerCase() === "corridor")
                .map((patient, idx) => {
                  const cx = 260;
                  const cy = 80 + idx * 45;
                  const isSelPat = selectedPatientId === patient.id;
                  const isHovPat = hoveredPatient === patient.id;

                  // Find assigned room name
                  const assignedRoom = rooms.find((r) => r.patientId === patient.id);
                  const assignedRoomName = assignedRoom ? assignedRoom.name : "None";

                  return (
                    <g
                      key={patient.id}
                      onClick={(e) => {
                        e.stopPropagation();
                        onPatientClick?.(patient);
                      }}
                      onMouseEnter={() => setHoveredPatient(patient.id)}
                      onMouseLeave={() => setHoveredPatient(null)}
                      className="cursor-pointer"
                    >
                      {/* Outer pulsing ring */}
                      <circle
                        cx={cx}
                        cy={cy}
                        r={pulseFrame % 2 === 0 ? 11 : 6}
                        fill="none"
                        stroke="#ef4444"
                        strokeWidth={1.5}
                        strokeOpacity={pulseFrame % 2 === 0 ? 0.6 : 0.1}
                        style={{ transition: "all 0.3s" }}
                      />
                      {/* Inner dot */}
                      <circle
                        cx={cx}
                        cy={cy}
                        r={isSelPat ? 6 : isHovPat ? 5.5 : 5}
                        fill="#ef4444"
                        stroke="white"
                        strokeWidth={1}
                      />

                      {/* Tooltip for corridor patient details */}
                      {isHovPat && (
                        <g>
                          <rect
                            x={cx + 12}
                            y={cy - 35}
                            width={170}
                            height={75}
                            fill="white"
                            stroke="#ef4444"
                            strokeWidth={1}
                            rx={4}
                            filter="url(#shadow)"
                          />
                          <text x={cx + 20} y={cy - 20} fontSize={11} fontWeight={600} fill="#111827">
                            {patient.name}
                          </text>
                          <text x={cx + 20} y={cy - 7} fontSize={9} fill="#4b5563">
                            ID: {patient.id}
                          </text>
                          <text x={cx + 20} y={cy + 6} fontSize={9} fill="#4b5563">
                            Assigned Room: {assignedRoomName}
                          </text>
                          <text x={cx + 20} y={cy + 19} fontSize={9} fill="#ef4444" fontWeight={500}>
                            Exited: {patient.lastActivity || "Just now"}
                          </text>
                        </g>
                      )}
                    </g>
                  );
                })}
          </svg>
        </div>

        {/* Stats footer */}
        <div className="mt-3 flex flex-wrap justify-between items-center text-xs text-muted-foreground gap-2">
          <div className="flex gap-4">
            <span>Rooms: {rooms.length || 6}</span>
            <span>Patients: {patients.length}</span>
            <span>Zoom: {Math.round(zoomLevel * 100)}%</span>
          </div>
          <div className="flex gap-1 flex-wrap">
            {(["Left Wing", "Corridor", "Right Wing"] as const).map((z) => (
              <Badge key={z} variant="outline" className="text-[10px]">
                {z}
              </Badge>
            ))}
          </div>
        </div>
      </CardContent>
    </Card>
  );
}