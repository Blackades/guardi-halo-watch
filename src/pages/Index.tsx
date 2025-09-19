import { useState } from "react";
import { Activity, Shield, Users, Clock } from "lucide-react";
import HospitalFloorPlan from "@/components/dashboard/hospital-floor-plan";
import PatientPanel from "@/components/dashboard/patient-panel";
import NotificationPanel from "@/components/dashboard/notification-panel";
import AccessLogs from "@/components/dashboard/access-logs";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";

const Index = () => {
  const [selectedPatientId, setSelectedPatientId] = useState<string>();

  return (
    <div className="min-h-screen bg-gradient-subtle">
      {/* Header */}
      <header className="bg-card border-b shadow-medical sticky top-0 z-50">
        <div className="container mx-auto px-6 py-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-lg bg-gradient-primary flex items-center justify-center">
                <Activity className="h-6 w-6 text-white" />
              </div>
              <div>
                <h1 className="text-2xl font-bold text-foreground">MedWatch Hospital</h1>
                <p className="text-sm text-muted-foreground">Real-time Patient Monitoring System</p>
              </div>
            </div>
            
            <div className="flex items-center gap-4">
              <Badge variant="default" className="bg-gradient-success text-success-foreground gap-1">
                <div className="w-2 h-2 rounded-full bg-white animate-pulse"></div>
                System Online
              </Badge>
              <div className="text-right">
                <p className="text-sm font-medium">Dr. Sarah Mitchell</p>
                <p className="text-xs text-muted-foreground">Chief of Staff</p>
              </div>
            </div>
          </div>
        </div>
      </header>

      {/* Stats Bar */}
      <div className="container mx-auto px-6 py-4">
        <div className="grid grid-cols-1 md:grid-cols-4 gap-4 mb-6">
          <Card className="shadow-medical">
            <CardContent className="p-4">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm text-muted-foreground">Active Patients</p>
                  <p className="text-2xl font-bold text-primary">5</p>
                </div>
                <Users className="h-8 w-8 text-primary/60" />
              </div>
            </CardContent>
          </Card>
          
          <Card className="shadow-medical">
            <CardContent className="p-4">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm text-muted-foreground">Available Rooms</p>
                  <p className="text-2xl font-bold text-success">3</p>
                </div>
                <Shield className="h-8 w-8 text-success/60" />
              </div>
            </CardContent>
          </Card>
          
          <Card className="shadow-alert">
            <CardContent className="p-4">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm text-muted-foreground">Active Alerts</p>
                  <p className="text-2xl font-bold text-destructive">2</p>
                </div>
                <Activity className="h-8 w-8 text-destructive/60" />
              </div>
            </CardContent>
          </Card>
          
          <Card className="shadow-medical">
            <CardContent className="p-4">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm text-muted-foreground">System Uptime</p>
                  <p className="text-2xl font-bold text-info">99.9%</p>
                </div>
                <Clock className="h-8 w-8 text-info/60" />
              </div>
            </CardContent>
          </Card>
        </div>

        {/* Main Dashboard Grid */}
        <div className="grid grid-cols-12 gap-6">
          {/* Floor Plan - Large */}
          <div className="col-span-12 lg:col-span-8">
            <HospitalFloorPlan 
              selectedPatientId={selectedPatientId}
              onRoomClick={(room) => {
                if (room.patientId) {
                  setSelectedPatientId(room.patientId);
                }
              }}
            />
          </div>
          
          {/* Patient Panel */}
          <div className="col-span-12 lg:col-span-4">
            <PatientPanel 
              selectedPatientId={selectedPatientId}
              onPatientClick={setSelectedPatientId}
            />
          </div>
          
          {/* Notifications */}
          <div className="col-span-12 lg:col-span-4">
            <NotificationPanel />
          </div>
          
          {/* Access Logs */}
          <div className="col-span-12 lg:col-span-8">
            <AccessLogs />
          </div>
        </div>
      </div>
    </div>
  );
};

export default Index;