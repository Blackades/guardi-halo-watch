import { useState, useEffect } from "react";
import { useQuery } from "@tanstack/react-query";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { StatusBadge } from "@/components/ui/status-badge";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Separator } from "@/components/ui/separator";
import { 
  MapPin, 
  Activity, 
  Clock, 
  Search, 
  User, 
  Phone, 
  Calendar, 
  AlertTriangle, 
  Eye
} from "lucide-react";
import { fetchPatients, getAuthHeaders, apiConfig, type PatientSummary } from "@/lib/api";

interface PatientPanelProps {
  selectedPatientId?: string;
  onPatientClick?: (patientId: string) => void;
}

interface PatientDetails extends PatientSummary {
  medicalRecordNumber?: string;
  admissionDate?: string;
  emergencyContact?: {
    name: string;
    phone: string;
    relationship: string;
  };
  medicalNotes?: string;
  allergies?: string[];
  medications?: string[];
  movementPattern?: {
    timestamp: string;
    steps: number;
    location: string;
  }[];
}

export default function PatientPanel({ selectedPatientId, onPatientClick }: PatientPanelProps) {
  const [searchTerm, setSearchTerm] = useState("");
  const [activeTab, setActiveTab] = useState("list");
  const [selectedPatientDetails, setSelectedPatientDetails] = useState<PatientDetails | null>(null);
  
  const { data: patients, isLoading, isError } = useQuery({
    queryKey: ["patients"],
    queryFn: fetchPatients,
    refetchInterval: 20_000,
  });

  // Fetch detailed patient data when a patient is selected
  const { data: patientDetails } = useQuery({
    queryKey: ["patient-details", selectedPatientId],
    queryFn: async () => {
      if (!selectedPatientId) return null;
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/patients/${selectedPatientId}/details`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch patient details');
      return response.json() as PatientDetails;
    },
    enabled: !!selectedPatientId,
    refetchInterval: 30_000,
  });

  useEffect(() => {
    if (patientDetails) {
      setSelectedPatientDetails(patientDetails);
      setActiveTab("details");
    }
  }, [patientDetails]);
  
  const list: PatientSummary[] = (patients ?? []).filter((patient) => {
    if (!searchTerm) return true;
    const term = searchTerm.toLowerCase();
    return (
      patient.name.toLowerCase().includes(term) ||
      patient.id.toLowerCase().includes(term)
    );
  });


  const renderPatientList = () => (
    <ScrollArea className="h-96">
      {isError && !patients && (
        <div className="p-4 text-sm text-destructive">
          Unable to load patients from monitoring server.
        </div>
      )}
      {!isLoading && list.length === 0 && !isError && (
        <div className="p-4 text-sm text-muted-foreground">
          No patients are currently being tracked. Once a wristband sends data
          to the backend, they will appear here automatically.
        </div>
      )}
      {list.map((patient) => (
        <div
          key={patient.id}
          className={cn(
            "border-b p-4 cursor-pointer transition-all duration-200 hover:bg-accent/50",
            selectedPatientId === patient.id && "bg-accent border-l-4 border-l-primary"
          )}
          onClick={() => {
            onPatientClick?.(patient.id);
            setActiveTab("details");
          }}
        >
          <div className="flex items-start justify-between">
            <div className="flex-1">
              <div className="flex items-center gap-2 mb-1 flex-wrap">
                <h4 className="font-medium text-sm">{patient.name}</h4>
                <Badge variant="outline" className="text-xs">
                  {patient.id}
                </Badge>
                {patient.strap_intact === false && (
                  <Badge variant="destructive" className="animate-pulse bg-red-500/15 text-red-500 border-red-500/30 flex items-center gap-1 text-xs px-1.5 py-0">
                    <AlertTriangle className="h-3 w-3" />
                    Strap Compromised
                  </Badge>
                )}
                <span className="text-xs text-muted-foreground">
                  {patient.age ? `Age ${patient.age}` : "Age N/A"}
                </span>
              </div>
              
              <div className="flex items-center gap-4 mb-2 text-xs text-muted-foreground">
                <div className="flex items-center gap-1">
                  <MapPin className="h-3 w-3" />
                  <span>{patient.location ?? "Location unknown"}</span>
                </div>
                <div className="flex items-center gap-1">
                  <Clock className="h-3 w-3" />
                  <span>{patient.lastActivity ?? "Just now"}</span>
                </div>
                <div className="flex items-center gap-1">
                  <Activity className="h-3 w-3" />
                  <span>{patient.movementSteps ?? 0} steps</span>
                </div>
              </div>

            </div>
            
            <div className="ml-3 flex flex-col items-end gap-1">
              <StatusBadge variant={patient.status} size="sm">
                {patient.status}
              </StatusBadge>
              <Button
                variant="ghost"
                size="sm"
                  onClick={(e) => {
                    e.stopPropagation();
                    onPatientClick?.(patient.id);
                    setActiveTab("details");
                  }}
                className="h-6 w-6 p-0"
              >
                <Eye className="h-3 w-3" />
              </Button>
            </div>
          </div>
        </div>
      ))}
    </ScrollArea>
  );

  const renderPatientDetails = () => {
    if (!selectedPatientDetails) {
      return (
        <div className="p-8 text-center text-muted-foreground">
          <User className="h-12 w-12 mx-auto mb-4 opacity-50" />
          <p>Select a patient to view details</p>
        </div>
      );
    }

    const patient = selectedPatientDetails;

    return (
      <ScrollArea className="h-96">
        <div className="p-4 space-y-6">
          {/* Patient Header */}
          <div className="flex items-start justify-between">
            <div>
              <h3 className="text-lg font-semibold">{patient.name}</h3>
              <div className="flex items-center gap-2 mt-1">
                <Badge variant="outline">{patient.id}</Badge>
                {patient.medicalRecordNumber && (
                  <Badge variant="secondary">MRN: {patient.medicalRecordNumber}</Badge>
                )}
                <StatusBadge variant={patient.status} size="sm">
                  {patient.status}
                </StatusBadge>
              </div>
            </div>
            <Button
              variant="outline"
              size="sm"
              onClick={() => setActiveTab("list")}
            >
              Back to List
            </Button>
          </div>

          <Separator />

          {patient.strap_intact === false && (
            <div className="bg-destructive/15 border border-destructive/30 rounded-lg p-3 flex items-start gap-3 text-destructive animate-pulse">
              <AlertTriangle className="h-5 w-5 shrink-0 mt-0.5" />
              <div>
                <h5 className="font-semibold text-sm">Security Breach Detected</h5>
                <p className="text-xs text-destructive/90">
                  The wristband tamper sensor reports the wristband strap is compromised/cut! Check the patient immediately.
                </p>
              </div>
            </div>
          )}

          {/* Basic Information */}
          <div className="grid grid-cols-2 gap-4 text-sm">
            <div className="flex items-center gap-2">
              <User className="h-4 w-4 text-muted-foreground" />
              <span className="text-muted-foreground">Age:</span>
              <span className="font-medium">{patient.age || 'N/A'}</span>
            </div>
            <div className="flex items-center gap-2">
              <Calendar className="h-4 w-4 text-muted-foreground" />
              <span className="text-muted-foreground">Admitted:</span>
              <span className="font-medium">
                {patient.admissionDate ? new Date(patient.admissionDate).toLocaleDateString() : 'N/A'}
              </span>
            </div>
            <div className="flex items-center gap-2">
              <MapPin className="h-4 w-4 text-muted-foreground" />
              <span className="text-muted-foreground">Location:</span>
              <span className="font-medium">{patient.location || 'Unknown'}</span>
            </div>
            <div className="flex items-center gap-2">
              <Activity className="h-4 w-4 text-muted-foreground" />
              <span className="text-muted-foreground">Steps Today:</span>
              <span className="font-medium">{patient.movementSteps || 0}</span>
            </div>
          </div>

          {/* Emergency Contact */}
          {patient.emergencyContact && (
            <>
              <Separator />
              <div>
                <h4 className="font-medium mb-2 flex items-center gap-2">
                  <Phone className="h-4 w-4" />
                  Emergency Contact
                </h4>
                <div className="text-sm space-y-1">
                  <p><span className="text-muted-foreground">Name:</span> {patient.emergencyContact.name}</p>
                  <p><span className="text-muted-foreground">Phone:</span> {patient.emergencyContact.phone}</p>
                  <p><span className="text-muted-foreground">Relationship:</span> {patient.emergencyContact.relationship}</p>
                </div>
              </div>
            </>
          )}


          {/* Medical Information */}
          {(patient.allergies?.length || patient.medications?.length || patient.medicalNotes) && (
            <>
              <Separator />
              <div>
                <h4 className="font-medium mb-3 flex items-center gap-2">
                  <AlertTriangle className="h-4 w-4" />
                  Medical Information
                </h4>
                <div className="space-y-3 text-sm">
                  {patient.allergies?.length && (
                    <div>
                      <span className="text-muted-foreground font-medium">Allergies:</span>
                      <div className="flex flex-wrap gap-1 mt-1">
                        {patient.allergies.map((allergy, index) => (
                          <Badge key={index} variant="destructive" className="text-xs">
                            {allergy}
                          </Badge>
                        ))}
                      </div>
                    </div>
                  )}
                  {patient.medications?.length && (
                    <div>
                      <span className="text-muted-foreground font-medium">Medications:</span>
                      <div className="flex flex-wrap gap-1 mt-1">
                        {patient.medications.map((medication, index) => (
                          <Badge key={index} variant="outline" className="text-xs">
                            {medication}
                          </Badge>
                        ))}
                      </div>
                    </div>
                  )}
                  {patient.medicalNotes && (
                    <div>
                      <span className="text-muted-foreground font-medium">Notes:</span>
                      <p className="mt-1 text-sm">{patient.medicalNotes}</p>
                    </div>
                  )}
                </div>
              </div>
            </>
          )}
        </div>
      </ScrollArea>
    );
  };


  return (
    <Card className="h-full">
      <CardHeader className="pb-3">
        <div className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <User className="h-5 w-5" />
            Patient Monitoring
          </CardTitle>
          <div className="flex items-center gap-2">
            <Badge variant="secondary" className="bg-gradient-primary text-primary-foreground">
              Live
            </Badge>
            {isLoading ? (
              <Badge variant="outline">Loading...</Badge>
            ) : (
              <Badge variant="outline">{patients?.length ?? 0} Active</Badge>
            )}
          </div>
        </div>

        {activeTab === "list" && (
          <div className="relative">
            <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 h-4 w-4 text-muted-foreground" />
            <Input
              placeholder="Search patients..."
              value={searchTerm}
              onChange={(e) => setSearchTerm(e.target.value)}
              className="pl-10"
            />
          </div>
        )}
      </CardHeader>

      <CardContent className="p-0">
        <Tabs value={activeTab} onValueChange={setActiveTab}>
          <TabsList className="grid w-full grid-cols-2 mx-4 mb-4">
            <TabsTrigger value="list" className="text-xs">
              Patient List
            </TabsTrigger>
            <TabsTrigger value="details" className="text-xs" disabled={!selectedPatientId}>
              Details
            </TabsTrigger>
          </TabsList>

          <TabsContent value="list" className="mt-0">
            {renderPatientList()}
          </TabsContent>

          <TabsContent value="details" className="mt-0">
            {renderPatientDetails()}
          </TabsContent>
        </Tabs>
      </CardContent>
    </Card>
  );
}

function cn(...classes: (string | undefined | boolean)[]): string {
  return classes.filter(Boolean).join(' ');
}