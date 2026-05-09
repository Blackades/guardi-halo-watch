import React, { useState, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useForm } from 'react-hook-form';
import { zodResolver } from '@hookform/resolvers/zod';
import { z } from 'zod';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Badge } from '@/components/ui/badge';
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs';
import { ScrollArea } from '@/components/ui/scroll-area';
import { Separator } from '@/components/ui/separator';
import { Alert, AlertDescription } from '@/components/ui/alert';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Checkbox } from '@/components/ui/checkbox';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from '@/components/ui/dialog';
import {
  MapPin,
  Plus,
  Edit,
  Trash2,
  Shield,
  AlertTriangle,
  Users,
  Clock,
  Settings,
  Save,
  X,
  Eye,
  BarChart3,
  Activity,
  Lock,
  Unlock,
  Cpu,
  Radio,
} from 'lucide-react';
import { apiConfig, getAuthHeaders, type PatientSummary } from '@/lib/api';
import { useAuth } from '@/contexts/AuthContext';
import RoleGuard from '@/components/auth/RoleGuard';

interface RFIDReader {
  reader_id: string;
  node_id: string;
  gpio_pin: number;
  door_name: string;
  status: string;
  last_seen?: string;
}

interface Zone {
  id: string;
  name: string;
  type: 'normal' | 'restricted' | 'isolation' | 'exit';
  description?: string;
  maxOccupancy?: number;
  requireAuthorization: boolean;
  authorizedPatients: string[];
  authorizedRoles: string[];
  alertLevel: 'info' | 'warning' | 'critical';
  isActive: boolean;
  coordinates?: { x: number; y: number }[];
  createdAt: string;
  updatedAt: string;
}

interface ZoneViolation {
  id: string;
  zoneId: string;
  zoneName: string;
  patientId: string;
  patientName: string;
  violationType: 'unauthorized_entry' | 'overstay' | 'capacity_exceeded';
  timestamp: string;
  resolved: boolean;
  resolvedAt?: string;
  resolvedBy?: string;
}

interface ZoneOccupancy {
  zoneId: string;
  zoneName: string;
  currentOccupancy: number;
  maxOccupancy?: number;
  patients: {
    id: string;
    name: string;
    entryTime: string;
    authorized: boolean;
  }[];
}

const zoneSchema = z.object({
  name: z.string().min(1, 'Zone name is required'),
  type: z.enum(['normal', 'restricted', 'isolation', 'exit']),
  description: z.string().optional(),
  maxOccupancy: z.number().min(1).optional(),
  requireAuthorization: z.boolean(),
  alertLevel: z.enum(['info', 'warning', 'critical']),
  isActive: z.boolean(),
});

type ZoneFormData = z.infer<typeof zoneSchema>;

const ZoneManagement: React.FC = () => {
  const { user } = useAuth();
  const queryClient = useQueryClient();
  const [selectedZone, setSelectedZone] = useState<Zone | null>(null);
  const [isCreateDialogOpen, setIsCreateDialogOpen] = useState(false);
  const [isEditDialogOpen, setIsEditDialogOpen] = useState(false);
  const [activeTab, setActiveTab] = useState('zones');

  // Fetch zones
  const { data: zones, isLoading: zonesLoading } = useQuery({
    queryKey: ['zones'],
    queryFn: async () => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch zones');
      return response.json() as Zone[];
    },
    refetchInterval: 30000,
  });

  // Fetch patients for assignment
  const { data: patients } = useQuery({
    queryKey: ['patients-for-zones'],
    queryFn: async () => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/patients`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch patients');
      return response.json() as PatientSummary[];
    },
  });

  // Fetch zone violations
  const { data: violations } = useQuery({
    queryKey: ['zone-violations'],
    queryFn: async () => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones/violations`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch violations');
      return response.json() as ZoneViolation[];
    },
    refetchInterval: 15000,
  });

  // Fetch zone occupancy
  const { data: occupancy } = useQuery({
    queryKey: ['zone-occupancy'],
    queryFn: async () => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones/occupancy`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch occupancy');
      return response.json() as ZoneOccupancy[];
    },
    refetchInterval: 10000,
  });

  // Fetch RFID Readers
  const { data: readers, isLoading: readersLoading } = useQuery({
    queryKey: ['rfid-readers'],
    queryFn: async () => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/readers`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch RFID readers');
      return response.json() as RFIDReader[];
    },
    refetchInterval: 10000,
  });

  // Form for zone creation/editing
  const {
    register,
    handleSubmit,
    reset,
    setValue,
    watch,
    formState: { errors },
  } = useForm<ZoneFormData>({
    resolver: zodResolver(zoneSchema),
    defaultValues: {
      requireAuthorization: false,
      alertLevel: 'info',
      isActive: true,
    },
  });

  // Mutations
  const createZoneMutation = useMutation({
    mutationFn: async (data: ZoneFormData) => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones`, {
        method: 'POST',
        headers: getAuthHeaders(),
        body: JSON.stringify(data),
      });
      if (!response.ok) throw new Error('Failed to create zone');
      return response.json();
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['zones'] });
      setIsCreateDialogOpen(false);
      reset();
    },
  });

  const updateZoneMutation = useMutation({
    mutationFn: async ({ id, data }: { id: string; data: ZoneFormData }) => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones/${id}`, {
        method: 'PUT',
        headers: getAuthHeaders(),
        body: JSON.stringify(data),
      });
      if (!response.ok) throw new Error('Failed to update zone');
      return response.json();
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['zones'] });
      setIsEditDialogOpen(false);
      setSelectedZone(null);
      reset();
    },
  });

  const deleteZoneMutation = useMutation({
    mutationFn: async (id: string) => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones/${id}`, {
        method: 'DELETE',
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to delete zone');
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['zones'] });
      setSelectedZone(null);
    },
  });

  const assignPatientMutation = useMutation({
    mutationFn: async ({ zoneId, patientId, authorized }: { zoneId: string; patientId: string; authorized: boolean }) => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones/${zoneId}/patients/${patientId}`, {
        method: authorized ? 'POST' : 'DELETE',
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to update patient assignment');
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['zones'] });
    },
  });

  const resolveViolationMutation = useMutation({
    mutationFn: async (violationId: string) => {
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/zones/violations/${violationId}/resolve`, {
        method: 'POST',
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to resolve violation');
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['zone-violations'] });
    },
  });

  // Helper functions
  const getZoneTypeColor = (type: Zone['type']) => {
    switch (type) {
      case 'normal': return 'bg-green-100 text-green-800 border-green-200';
      case 'restricted': return 'bg-red-100 text-red-800 border-red-200';
      case 'isolation': return 'bg-yellow-100 text-yellow-800 border-yellow-200';
      case 'exit': return 'bg-purple-100 text-purple-800 border-purple-200';
      default: return 'bg-gray-100 text-gray-800 border-gray-200';
    }
  };

  const getAlertLevelColor = (level: Zone['alertLevel']) => {
    switch (level) {
      case 'info': return 'text-blue-600';
      case 'warning': return 'text-yellow-600';
      case 'critical': return 'text-red-600';
      default: return 'text-gray-600';
    }
  };

  const handleEditZone = (zone: Zone) => {
    setSelectedZone(zone);
    setValue('name', zone.name);
    setValue('type', zone.type);
    setValue('description', zone.description || '');
    setValue('maxOccupancy', zone.maxOccupancy);
    setValue('requireAuthorization', zone.requireAuthorization);
    setValue('alertLevel', zone.alertLevel);
    setValue('isActive', zone.isActive);
    setIsEditDialogOpen(true);
  };

  const onSubmit = (data: ZoneFormData) => {
    if (selectedZone) {
      updateZoneMutation.mutate({ id: selectedZone.id, data });
    } else {
      createZoneMutation.mutate(data);
    }
  };

  const renderZoneList = () => (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold">Zone Configuration</h3>
        <RoleGuard allowedRoles={['admin', 'doctor']}>
          <Dialog open={isCreateDialogOpen} onOpenChange={setIsCreateDialogOpen}>
            <DialogTrigger asChild>
              <Button>
                <Plus className="h-4 w-4 mr-2" />
                Create Zone
              </Button>
            </DialogTrigger>
            <DialogContent className="max-w-md">
              <DialogHeader>
                <DialogTitle>Create New Zone</DialogTitle>
                <DialogDescription>
                  Configure a new monitoring zone for the hospital ward.
                </DialogDescription>
              </DialogHeader>
              <form onSubmit={handleSubmit(onSubmit)} className="space-y-4">
                <div>
                  <Label htmlFor="name">Zone Name</Label>
                  <Input
                    id="name"
                    {...register('name')}
                    placeholder="e.g., ICU Ward A"
                  />
                  {errors.name && (
                    <p className="text-sm text-destructive mt-1">{errors.name.message}</p>
                  )}
                </div>

                <div>
                  <Label htmlFor="type">Zone Type</Label>
                  <Select onValueChange={(value: Zone['type']) => setValue('type', value)}>
                    <SelectTrigger>
                      <SelectValue placeholder="Select zone type" />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="normal">Normal</SelectItem>
                      <SelectItem value="restricted">Restricted</SelectItem>
                      <SelectItem value="isolation">Isolation</SelectItem>
                      <SelectItem value="exit">Exit</SelectItem>
                    </SelectContent>
                  </Select>
                </div>

                <div>
                  <Label htmlFor="description">Description (Optional)</Label>
                  <Input
                    id="description"
                    {...register('description')}
                    placeholder="Zone description"
                  />
                </div>

                <div>
                  <Label htmlFor="maxOccupancy">Max Occupancy (Optional)</Label>
                  <Input
                    id="maxOccupancy"
                    type="number"
                    {...register('maxOccupancy', { valueAsNumber: true })}
                    placeholder="Maximum number of patients"
                  />
                </div>

                <div className="flex items-center space-x-2">
                  <Checkbox
                    id="requireAuthorization"
                    {...register('requireAuthorization')}
                  />
                  <Label htmlFor="requireAuthorization">Require Authorization</Label>
                </div>

                <div>
                  <Label htmlFor="alertLevel">Alert Level</Label>
                  <Select onValueChange={(value: Zone['alertLevel']) => setValue('alertLevel', value)}>
                    <SelectTrigger>
                      <SelectValue placeholder="Select alert level" />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="info">Info</SelectItem>
                      <SelectItem value="warning">Warning</SelectItem>
                      <SelectItem value="critical">Critical</SelectItem>
                    </SelectContent>
                  </Select>
                </div>

                <div className="flex items-center space-x-2">
                  <Checkbox
                    id="isActive"
                    {...register('isActive')}
                  />
                  <Label htmlFor="isActive">Active</Label>
                </div>

                <DialogFooter>
                  <Button
                    type="button"
                    variant="outline"
                    onClick={() => setIsCreateDialogOpen(false)}
                  >
                    Cancel
                  </Button>
                  <Button type="submit" disabled={createZoneMutation.isPending}>
                    {createZoneMutation.isPending ? 'Creating...' : 'Create Zone'}
                  </Button>
                </DialogFooter>
              </form>
            </DialogContent>
          </Dialog>
        </RoleGuard>
      </div>

      <ScrollArea className="h-96">
        <div className="space-y-3">
          {zones?.map((zone) => (
            <Card key={zone.id} className="p-4">
              <div className="flex items-start justify-between">
                <div className="flex-1">
                  <div className="flex items-center gap-2 mb-2">
                    <h4 className="font-medium">{zone.name}</h4>
                    <Badge className={getZoneTypeColor(zone.type)}>
                      {zone.type}
                    </Badge>
                    {!zone.isActive && (
                      <Badge variant="outline">Inactive</Badge>
                    )}
                  </div>
                  
                  {zone.description && (
                    <p className="text-sm text-muted-foreground mb-2">
                      {zone.description}
                    </p>
                  )}

                  <div className="flex items-center gap-4 text-xs text-muted-foreground">
                    <div className="flex items-center gap-1">
                      <Users className="h-3 w-3" />
                      <span>
                        {zone.authorizedPatients.length} authorized
                        {zone.maxOccupancy && ` / ${zone.maxOccupancy} max`}
                      </span>
                    </div>
                    <div className="flex items-center gap-1">
                      <AlertTriangle className={`h-3 w-3 ${getAlertLevelColor(zone.alertLevel)}`} />
                      <span>{zone.alertLevel} alerts</span>
                    </div>
                    {zone.requireAuthorization && (
                      <div className="flex items-center gap-1">
                        <Lock className="h-3 w-3" />
                        <span>Auth required</span>
                      </div>
                    )}
                  </div>
                </div>

                <div className="flex items-center gap-1">
                  <Button
                    variant="ghost"
                    size="sm"
                    onClick={() => setSelectedZone(zone)}
                  >
                    <Eye className="h-4 w-4" />
                  </Button>
                  <RoleGuard allowedRoles={['admin', 'doctor']}>
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => handleEditZone(zone)}
                    >
                      <Edit className="h-4 w-4" />
                    </Button>
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => deleteZoneMutation.mutate(zone.id)}
                      disabled={deleteZoneMutation.isPending}
                    >
                      <Trash2 className="h-4 w-4" />
                    </Button>
                  </RoleGuard>
                </div>
              </div>
            </Card>
          ))}
        </div>
      </ScrollArea>
    </div>
  );

  const renderPatientAssignment = () => (
    <div className="space-y-4">
      <h3 className="text-lg font-semibold">Patient Zone Assignments</h3>
      
      {selectedZone ? (
        <div className="space-y-4">
          <div className="flex items-center justify-between">
            <div>
              <h4 className="font-medium">{selectedZone.name}</h4>
              <p className="text-sm text-muted-foreground">
                Manage patient access to this zone
              </p>
            </div>
            <Button
              variant="outline"
              size="sm"
              onClick={() => setSelectedZone(null)}
            >
              <X className="h-4 w-4 mr-2" />
              Close
            </Button>
          </div>

          <ScrollArea className="h-80">
            <div className="space-y-2">
              {patients?.map((patient) => {
                const isAuthorized = selectedZone.authorizedPatients.includes(patient.id);
                return (
                  <div
                    key={patient.id}
                    className="flex items-center justify-between p-3 border rounded-lg"
                  >
                    <div className="flex items-center gap-3">
                      <div className="flex items-center gap-2">
                        <span className="font-medium">{patient.name}</span>
                        <Badge variant="outline">{patient.id}</Badge>
                      </div>
                      <div className="text-sm text-muted-foreground">
                        {patient.location || 'Location unknown'}
                      </div>
                    </div>
                    
                    <RoleGuard allowedRoles={['admin', 'doctor', 'nurse']}>
                      <Button
                        variant={isAuthorized ? "destructive" : "default"}
                        size="sm"
                        onClick={() => assignPatientMutation.mutate({
                          zoneId: selectedZone.id,
                          patientId: patient.id,
                          authorized: !isAuthorized,
                        })}
                        disabled={assignPatientMutation.isPending}
                      >
                        {isAuthorized ? (
                          <>
                            <Unlock className="h-4 w-4 mr-2" />
                            Revoke Access
                          </>
                        ) : (
                          <>
                            <Lock className="h-4 w-4 mr-2" />
                            Grant Access
                          </>
                        )}
                      </Button>
                    </RoleGuard>
                  </div>
                );
              })}
            </div>
          </ScrollArea>
        </div>
      ) : (
        <div className="text-center py-8 text-muted-foreground">
          <Users className="h-12 w-12 mx-auto mb-4 opacity-50" />
          <p>Select a zone to manage patient assignments</p>
        </div>
      )}
    </div>
  );

  const renderOccupancyMonitoring = () => (
    <div className="space-y-4">
      <h3 className="text-lg font-semibold">Zone Occupancy Monitoring</h3>
      
      <ScrollArea className="h-96">
        <div className="space-y-4">
          {occupancy?.map((zone) => (
            <Card key={zone.zoneId} className="p-4">
              <div className="flex items-center justify-between mb-3">
                <h4 className="font-medium">{zone.zoneName}</h4>
                <div className="flex items-center gap-2">
                  <Badge variant={zone.currentOccupancy === 0 ? "outline" : "default"}>
                    {zone.currentOccupancy} / {zone.maxOccupancy || '∞'}
                  </Badge>
                  {zone.maxOccupancy && zone.currentOccupancy > zone.maxOccupancy && (
                    <Badge variant="destructive">Over Capacity</Badge>
                  )}
                </div>
              </div>

              {zone.patients.length > 0 ? (
                <div className="space-y-2">
                  {zone.patients.map((patient) => (
                    <div
                      key={patient.id}
                      className="flex items-center justify-between p-2 bg-accent/30 rounded"
                    >
                      <div className="flex items-center gap-2">
                        <span className="font-medium text-sm">{patient.name}</span>
                        <Badge variant="outline" className="text-xs">
                          {patient.id}
                        </Badge>
                      </div>
                      <div className="flex items-center gap-2 text-xs text-muted-foreground">
                        <Clock className="h-3 w-3" />
                        <span>
                          {new Date(patient.entryTime).toLocaleTimeString()}
                        </span>
                        {!patient.authorized && (
                          <Badge variant="destructive" className="text-xs">
                            Unauthorized
                          </Badge>
                        )}
                      </div>
                    </div>
                  ))}
                </div>
              ) : (
                <p className="text-sm text-muted-foreground">No patients currently in zone</p>
              )}
            </Card>
          ))}
        </div>
      </ScrollArea>
    </div>
  );

  const renderViolationHistory = () => (
    <div className="space-y-4">
      <h3 className="text-lg font-semibold">Zone Violation History</h3>
      
      <ScrollArea className="h-96">
        <div className="space-y-3">
          {violations?.map((violation) => (
            <Card key={violation.id} className="p-4">
              <div className="flex items-start justify-between">
                <div className="flex-1">
                  <div className="flex items-center gap-2 mb-2">
                    <AlertTriangle className="h-4 w-4 text-destructive" />
                    <span className="font-medium">{violation.zoneName}</span>
                    <Badge variant="destructive" className="text-xs">
                      {violation.violationType.replace('_', ' ')}
                    </Badge>
                    {violation.resolved && (
                      <Badge variant="outline" className="text-xs">
                        Resolved
                      </Badge>
                    )}
                  </div>
                  
                  <p className="text-sm mb-2">
                    Patient <strong>{violation.patientName}</strong> ({violation.patientId})
                  </p>
                  
                  <div className="flex items-center gap-4 text-xs text-muted-foreground">
                    <div className="flex items-center gap-1">
                      <Clock className="h-3 w-3" />
                      <span>{new Date(violation.timestamp).toLocaleString()}</span>
                    </div>
                    {violation.resolved && violation.resolvedBy && (
                      <div>
                        Resolved by {violation.resolvedBy}
                      </div>
                    )}
                  </div>
                </div>

                {!violation.resolved && (
                  <RoleGuard allowedRoles={['admin', 'doctor', 'nurse']}>
                    <Button
                      variant="outline"
                      size="sm"
                      onClick={() => resolveViolationMutation.mutate(violation.id)}
                      disabled={resolveViolationMutation.isPending}
                    >
                      <Shield className="h-4 w-4 mr-2" />
                      Resolve
                    </Button>
                  </RoleGuard>
                )}
              </div>
            </Card>
          ))}
          
          {violations?.length === 0 && (
            <div className="text-center py-8 text-muted-foreground">
              <Shield className="h-12 w-12 mx-auto mb-4 opacity-50" />
              <p>No zone violations recorded</p>
            </div>
          )}
        </div>
      </ScrollArea>
    </div>
  );

  const renderRFIDReaders = () => (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <div>
          <h3 className="text-lg font-semibold">Doors & RFID Readers</h3>
          <p className="text-sm text-muted-foreground">
            Read-only configuration and hardware connection status for multi-reader doors
          </p>
        </div>
      </div>

      <ScrollArea className="h-96">
        {readersLoading ? (
          <div className="text-center py-8 text-muted-foreground">
            <p>Loading readers configuration...</p>
          </div>
        ) : readers && readers.length > 0 ? (
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            {readers.map((reader) => (
              <Card key={`${reader.node_id}-${reader.reader_id}`} className="p-4 border border-border bg-card/50 hover:bg-card/80 transition-colors shadow-sm">
                <div className="flex items-start justify-between">
                  <div className="space-y-2">
                    <div className="flex items-center gap-2">
                      <span className="font-semibold text-base text-foreground">{reader.door_name}</span>
                      <Badge variant={reader.status === 'active' ? 'default' : 'secondary'} className={reader.status === 'active' ? 'bg-emerald-500/10 text-emerald-500 hover:bg-emerald-500/20 border-emerald-500/20' : ''}>
                        {reader.status}
                      </Badge>
                    </div>
                    
                    <div className="space-y-1.5 text-sm text-muted-foreground">
                      <div className="flex items-center gap-1.5">
                        <Settings className="h-3.5 w-3.5 text-muted-foreground/70" />
                        <span>Unified Node: <code className="text-xs bg-muted px-1.5 py-0.5 rounded text-foreground font-mono">{reader.node_id}</code></span>
                      </div>
                      <div className="flex items-center gap-1.5">
                        <Cpu className="h-3.5 w-3.5 text-muted-foreground/70" />
                        <span>Reader ID: <code className="text-xs bg-muted px-1.5 py-0.5 rounded text-foreground font-mono">{reader.reader_id}</code></span>
                      </div>
                      <div className="flex items-center gap-1.5">
                        <Activity className="h-3.5 w-3.5 text-muted-foreground/70" />
                        <span>Hardware Pin: <Badge variant="outline" className="font-mono text-xs">{reader.gpio_pin}</Badge></span>
                      </div>
                    </div>
                  </div>

                  {reader.last_seen && (
                    <div className="text-right flex flex-col justify-between h-full items-end">
                      <div className="flex items-center gap-1 text-xs text-muted-foreground">
                        <Clock className="h-3 w-3" />
                        <span>Last scan:</span>
                      </div>
                      <span className="text-xs font-medium text-foreground mt-1">
                        {new Date(reader.last_seen).toLocaleTimeString()}
                      </span>
                    </div>
                  )}
                </div>
              </Card>
            ))}
          </div>
        ) : (
          <div className="text-center py-8 text-muted-foreground border border-dashed rounded-lg">
            <Radio className="h-12 w-12 mx-auto mb-3 opacity-30 animate-pulse text-muted-foreground" />
            <p className="font-medium">No doors or RFID readers configured yet</p>
            <p className="text-xs mt-1">Make sure the ESP32 Unified Node is connected and has run its startup sync.</p>
          </div>
        )}
      </ScrollArea>
    </div>
  );

  return (
    <Card className="h-full">
      <CardHeader>
        <CardTitle className="flex items-center gap-2">
          <MapPin className="h-5 w-5" />
          Zone Management
        </CardTitle>
        <CardDescription>
          Configure zones, manage patient access, and monitor compliance
        </CardDescription>
      </CardHeader>

      <CardContent>
        <Tabs value={activeTab} onValueChange={setActiveTab}>
          <TabsList className="grid w-full grid-cols-5">
            <TabsTrigger value="zones">Zones</TabsTrigger>
            <TabsTrigger value="assignments">Assignments</TabsTrigger>
            <TabsTrigger value="occupancy">Occupancy</TabsTrigger>
            <TabsTrigger value="violations">Violations</TabsTrigger>
            <TabsTrigger value="readers">Doors / Readers</TabsTrigger>
          </TabsList>

          <TabsContent value="zones" className="mt-4">
            {renderZoneList()}
          </TabsContent>

          <TabsContent value="assignments" className="mt-4">
            {renderPatientAssignment()}
          </TabsContent>

          <TabsContent value="occupancy" className="mt-4">
            {renderOccupancyMonitoring()}
          </TabsContent>

          <TabsContent value="violations" className="mt-4">
            {renderViolationHistory()}
          </TabsContent>

          <TabsContent value="readers" className="mt-4">
            {renderRFIDReaders()}
          </TabsContent>
        </Tabs>
      </CardContent>

      {/* Edit Zone Dialog */}
      <Dialog open={isEditDialogOpen} onOpenChange={setIsEditDialogOpen}>
        <DialogContent className="max-w-md">
          <DialogHeader>
            <DialogTitle>Edit Zone</DialogTitle>
            <DialogDescription>
              Update zone configuration and settings.
            </DialogDescription>
          </DialogHeader>
          <form onSubmit={handleSubmit(onSubmit)} className="space-y-4">
            {/* Same form fields as create dialog */}
            <div>
              <Label htmlFor="edit-name">Zone Name</Label>
              <Input
                id="edit-name"
                {...register('name')}
                placeholder="e.g., ICU Ward A"
              />
              {errors.name && (
                <p className="text-sm text-destructive mt-1">{errors.name.message}</p>
              )}
            </div>

            <div>
              <Label htmlFor="edit-type">Zone Type</Label>
              <Select 
                value={watch('type')} 
                onValueChange={(value: Zone['type']) => setValue('type', value)}
              >
                <SelectTrigger>
                  <SelectValue placeholder="Select zone type" />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="normal">Normal</SelectItem>
                  <SelectItem value="restricted">Restricted</SelectItem>
                  <SelectItem value="isolation">Isolation</SelectItem>
                  <SelectItem value="exit">Exit</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <DialogFooter>
              <Button
                type="button"
                variant="outline"
                onClick={() => setIsEditDialogOpen(false)}
              >
                Cancel
              </Button>
              <Button type="submit" disabled={updateZoneMutation.isPending}>
                {updateZoneMutation.isPending ? 'Updating...' : 'Update Zone'}
              </Button>
            </DialogFooter>
          </form>
        </DialogContent>
      </Dialog>
    </Card>
  );
};

export default ZoneManagement;