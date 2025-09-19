import { useState } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { StatusBadge } from "@/components/ui/status-badge";
import hospitalFloorPlan from "@/assets/hospital-floor-plan.png";

interface Room {
  id: string;
  name: string;
  status: 'normal' | 'occupied' | 'anomaly' | 'warning';
  position: { x: number; y: number };
  patientId?: string;
}

const mockRooms: Room[] = [
  { id: 'R101', name: 'Room 101', status: 'occupied', position: { x: 15, y: 25 }, patientId: 'P001' },
  { id: 'R102', name: 'Room 102', status: 'normal', position: { x: 35, y: 25 } },
  { id: 'R103', name: 'Room 103', status: 'anomaly', position: { x: 55, y: 25 }, patientId: 'P003' },
  { id: 'R104', name: 'Room 104', status: 'occupied', position: { x: 75, y: 25 }, patientId: 'P002' },
  { id: 'R201', name: 'Room 201', status: 'normal', position: { x: 15, y: 65 } },
  { id: 'R202', name: 'Room 202', status: 'warning', position: { x: 35, y: 65 }, patientId: 'P004' },
  { id: 'R203', name: 'Room 203', status: 'occupied', position: { x: 55, y: 65 }, patientId: 'P005' },
  { id: 'R204', name: 'Room 204', status: 'normal', position: { x: 75, y: 65 } },
];

interface HospitalFloorPlanProps {
  selectedPatientId?: string;
  onRoomClick?: (room: Room) => void;
}

export default function HospitalFloorPlan({ selectedPatientId, onRoomClick }: HospitalFloorPlanProps) {
  const [hoveredRoom, setHoveredRoom] = useState<string | null>(null);

  const getRoomStatusColor = (status: Room['status']) => {
    switch (status) {
      case 'normal':
        return 'bg-success hover:bg-success/80 border-success';
      case 'occupied':
        return 'bg-info hover:bg-info/80 border-info';
      case 'anomaly':
        return 'bg-destructive hover:bg-destructive/80 border-destructive animate-pulse';
      case 'warning':
        return 'bg-warning hover:bg-warning/80 border-warning';
      default:
        return 'bg-muted hover:bg-muted/80 border-border';
    }
  };

  return (
    <Card className="h-full">
      <CardHeader>
        <CardTitle className="flex items-center justify-between">
          <span>Hospital Floor Plan - Ward A</span>
          <div className="flex gap-2 text-sm">
            <div className="flex items-center gap-1">
              <div className="w-3 h-3 rounded-full bg-success"></div>
              <span>Available</span>
            </div>
            <div className="flex items-center gap-1">
              <div className="w-3 h-3 rounded-full bg-info"></div>
              <span>Occupied</span>
            </div>
            <div className="flex items-center gap-1">
              <div className="w-3 h-3 rounded-full bg-warning"></div>
              <span>Warning</span>
            </div>
            <div className="flex items-center gap-1">
              <div className="w-3 h-3 rounded-full bg-destructive animate-pulse"></div>
              <span>Alert</span>
            </div>
          </div>
        </CardTitle>
      </CardHeader>
      <CardContent className="p-4">
        <div className="relative w-full h-96 bg-gradient-subtle rounded-lg overflow-hidden shadow-medical">
          <img 
            src={hospitalFloorPlan} 
            alt="Hospital Floor Plan"
            className="w-full h-full object-cover opacity-20"
          />
          <div className="absolute inset-0">
            {mockRooms.map((room) => (
              <div
                key={room.id}
                className="absolute cursor-pointer transform -translate-x-1/2 -translate-y-1/2"
                style={{
                  left: `${room.position.x}%`,
                  top: `${room.position.y}%`,
                }}
                onClick={() => onRoomClick?.(room)}
                onMouseEnter={() => setHoveredRoom(room.id)}
                onMouseLeave={() => setHoveredRoom(null)}
              >
                <div
                  className={cn(
                    "w-8 h-8 rounded-lg border-2 transition-all duration-200 flex items-center justify-center text-white text-xs font-bold shadow-lg",
                    getRoomStatusColor(room.status),
                    selectedPatientId && room.patientId === selectedPatientId && "ring-4 ring-primary/50 scale-110",
                    hoveredRoom === room.id && "scale-110 shadow-xl"
                  )}
                >
                  {room.id.slice(-2)}
                </div>
                {hoveredRoom === room.id && (
                  <div className="absolute top-full left-1/2 transform -translate-x-1/2 mt-2 bg-card border rounded-lg shadow-lg p-2 min-w-max z-10">
                    <p className="font-medium text-sm">{room.name}</p>
                    <div className="flex items-center gap-2 mt-1">
                      <StatusBadge variant={room.status} size="sm">
                        {room.status}
                      </StatusBadge>
                      {room.patientId && (
                        <span className="text-xs text-muted-foreground">
                          Patient: {room.patientId}
                        </span>
                      )}
                    </div>
                  </div>
                )}
              </div>
            ))}
          </div>
        </div>
      </CardContent>
    </Card>
  );
}

function cn(...classes: (string | undefined | boolean)[]): string {
  return classes.filter(Boolean).join(' ');
}