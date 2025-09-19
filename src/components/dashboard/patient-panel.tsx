import { useState } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { StatusBadge } from "@/components/ui/status-badge";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { MapPin, Activity, Clock, Search } from "lucide-react";

interface Patient {
  id: string;
  name: string;
  age: number;
  room?: string;
  status: 'normal' | 'occupied' | 'anomaly' | 'warning';
  location: string;
  lastActivity: string;
  movementSteps: number;
  vitals?: {
    heartRate: number;
    temperature: number;
  };
}

const mockPatients: Patient[] = [
  {
    id: 'P001',
    name: 'John Smith',
    age: 45,
    room: 'R101',
    status: 'occupied',
    location: 'Room 101',
    lastActivity: '2 minutes ago',
    movementSteps: 1250,
    vitals: { heartRate: 72, temperature: 98.6 }
  },
  {
    id: 'P002',
    name: 'Sarah Johnson',
    age: 32,
    room: 'R104',
    status: 'occupied',
    location: 'Room 104',
    lastActivity: '5 minutes ago',
    movementSteps: 890,
    vitals: { heartRate: 68, temperature: 99.1 }
  },
  {
    id: 'P003',
    name: 'Michael Davis',
    age: 67,
    room: 'R103',
    status: 'anomaly',
    location: 'Room 103',
    lastActivity: '15 minutes ago',
    movementSteps: 45,
    vitals: { heartRate: 95, temperature: 100.2 }
  },
  {
    id: 'P004',
    name: 'Emily Wilson',
    age: 28,
    room: 'R202',
    status: 'warning',
    location: 'Room 202',
    lastActivity: '8 minutes ago',
    movementSteps: 2100,
    vitals: { heartRate: 88, temperature: 98.9 }
  },
  {
    id: 'P005',
    name: 'Robert Brown',
    age: 55,
    room: 'R203',
    status: 'occupied',
    location: 'Room 203',
    lastActivity: '1 minute ago',
    movementSteps: 650,
    vitals: { heartRate: 75, temperature: 98.4 }
  }
];

interface PatientPanelProps {
  selectedPatientId?: string;
  onPatientClick?: (patientId: string) => void;
}

export default function PatientPanel({ selectedPatientId, onPatientClick }: PatientPanelProps) {
  const [searchTerm, setSearchTerm] = useState("");
  
  const filteredPatients = mockPatients.filter(patient =>
    patient.name.toLowerCase().includes(searchTerm.toLowerCase()) ||
    patient.id.toLowerCase().includes(searchTerm.toLowerCase())
  );

  return (
    <Card className="h-full">
      <CardHeader>
        <CardTitle className="flex items-center justify-between">
          <span>Active Patients ({mockPatients.length})</span>
          <Badge variant="secondary" className="bg-gradient-primary text-primary-foreground">
            Live
          </Badge>
        </CardTitle>
        <div className="relative">
          <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 h-4 w-4 text-muted-foreground" />
          <Input
            placeholder="Search patients..."
            value={searchTerm}
            onChange={(e) => setSearchTerm(e.target.value)}
            className="pl-10"
          />
        </div>
      </CardHeader>
      <CardContent className="p-0">
        <div className="max-h-96 overflow-y-auto">
          {filteredPatients.map((patient) => (
            <div
              key={patient.id}
              className={cn(
                "border-b p-4 cursor-pointer transition-all duration-200 hover:bg-accent/50",
                selectedPatientId === patient.id && "bg-accent border-l-4 border-l-primary"
              )}
              onClick={() => onPatientClick?.(patient.id)}
            >
              <div className="flex items-start justify-between">
                <div className="flex-1">
                  <div className="flex items-center gap-2 mb-1">
                    <h4 className="font-medium text-sm">{patient.name}</h4>
                    <Badge variant="outline" className="text-xs">
                      {patient.id}
                    </Badge>
                    <span className="text-xs text-muted-foreground">
                      Age {patient.age}
                    </span>
                  </div>
                  
                  <div className="flex items-center gap-4 mb-2 text-xs text-muted-foreground">
                    <div className="flex items-center gap-1">
                      <MapPin className="h-3 w-3" />
                      <span>{patient.location}</span>
                    </div>
                    <div className="flex items-center gap-1">
                      <Clock className="h-3 w-3" />
                      <span>{patient.lastActivity}</span>
                    </div>
                    <div className="flex items-center gap-1">
                      <Activity className="h-3 w-3" />
                      <span>{patient.movementSteps} steps</span>
                    </div>
                  </div>

                  {patient.vitals && (
                    <div className="flex items-center gap-3 mb-2 text-xs">
                      <span className="text-muted-foreground">
                        HR: <span className="font-medium text-foreground">{patient.vitals.heartRate} bpm</span>
                      </span>
                      <span className="text-muted-foreground">
                        Temp: <span className="font-medium text-foreground">{patient.vitals.temperature}°F</span>
                      </span>
                    </div>
                  )}
                </div>
                
                <div className="ml-3">
                  <StatusBadge variant={patient.status} size="sm">
                    {patient.status}
                  </StatusBadge>
                </div>
              </div>
            </div>
          ))}
        </div>
      </CardContent>
    </Card>
  );
}

function cn(...classes: (string | undefined | boolean)[]): string {
  return classes.filter(Boolean).join(' ');
}