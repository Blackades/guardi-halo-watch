import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { useNavigate } from "react-router-dom";
import { Activity, Shield, Users, Clock, User, ChevronDown, FileText, History } from "lucide-react";
import HospitalFloorPlan from "@/components/dashboard/hospital-floor-plan";
import PatientPanel from "@/components/dashboard/patient-panel";
import NotificationPanel from "@/components/dashboard/notification-panel";
import AccessLogs from "@/components/dashboard/access-logs";
import ZoneManagement from "@/components/dashboard/zone-management";
import UserProfile from "@/components/auth/UserProfile";
import RoleGuard from "@/components/auth/RoleGuard";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import { fetchOverview, type OverviewStats } from "@/lib/api";
import { useAuth } from "@/contexts/AuthContext";

const Index = () => {
  const [selectedPatientId, setSelectedPatientId] = useState<string>();
  const [showUserProfile, setShowUserProfile] = useState(false);
  const { user } = useAuth();
  const navigate = useNavigate();

  const { data: overview } = useQuery<OverviewStats>({
    queryKey: ["overview"],
    queryFn: fetchOverview,
    // This data changes slowly; keep it fresh but don't spam the server
    refetchInterval: 15_000,
  });

  const uptimeLabel = (() => {
    if (!overview) return "—";
    const minutesTotal = Math.floor(overview.systemUptimeSeconds / 60);
    const hours = Math.floor(minutesTotal / 60);
    const minutes = minutesTotal % 60;
    if (hours === 0) return `${minutes} min`;
    return `${hours}h ${minutes.toString().padStart(2, "0")}m`;
  })();

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
                <h1 className="text-2xl font-bold text-foreground">Aga Khan Hospital</h1>
                <p className="text-sm text-muted-foreground">Real-time Patient Monitoring System</p>
              </div>
            </div>
            
            <div className="flex items-center gap-4">
              <Badge variant="default" className="bg-gradient-success text-success-foreground gap-1">
                <div className="w-2 h-2 rounded-full bg-white animate-pulse"></div>
                System Online
              </Badge>
              
              <RoleGuard allowedRoles={['admin', 'doctor']}>
                <div className="flex gap-2">
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={() => navigate('/reports')}
                    className="flex items-center gap-2"
                  >
                    <FileText className="h-4 w-4" />
                    Reports
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={() => navigate('/audit-logs')}
                    className="flex items-center gap-2"
                  >
                    <History className="h-4 w-4" />
                    Audit Logs
                  </Button>
                </div>
              </RoleGuard>
              
              <Popover open={showUserProfile} onOpenChange={setShowUserProfile}>
                <PopoverTrigger asChild>
                  <Button variant="ghost" className="flex items-center gap-2 h-auto p-2">
                    <div className="w-8 h-8 rounded-full bg-gradient-primary flex items-center justify-center">
                      <User className="h-4 w-4 text-white" />
                    </div>
                    <div className="text-right">
                      <p className="text-sm font-medium">{user?.fullName || user?.username}</p>
                      <p className="text-xs text-muted-foreground capitalize">{user?.role}</p>
                    </div>
                    <ChevronDown className="h-4 w-4 text-muted-foreground" />
                  </Button>
                </PopoverTrigger>
                <PopoverContent className="w-auto p-0" align="end">
                  <UserProfile onClose={() => setShowUserProfile(false)} />
                </PopoverContent>
              </Popover>
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
                  <p className="text-2xl font-bold text-primary">
                    {overview?.activePatients ?? "—"}
                  </p>
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
                  <p className="text-2xl font-bold text-success">
                    {overview?.availableRooms ?? "—"}
                  </p>
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
                  <p className="text-2xl font-bold text-destructive">
                    {overview?.activeAlerts ?? "0"}
                  </p>
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
                  <p className="text-2xl font-bold text-info">
                    {uptimeLabel}
                  </p>
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
              onPatientClick={(patient) => {
                setSelectedPatientId(patient.id);
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
          <div className="col-span-12 lg:col-span-6">
            <NotificationPanel />
          </div>
          
          {/* Zone Management */}
          <div className="col-span-12 lg:col-span-6">
            <ZoneManagement />
          </div>
          
          {/* Access Logs */}
          <div className="col-span-12">
            <AccessLogs />
          </div>
        </div>
      </div>
    </div>
  );
};

export default Index;