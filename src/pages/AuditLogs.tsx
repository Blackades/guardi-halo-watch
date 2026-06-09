import React, { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { useNavigate } from 'react-router-dom';
import { format, subMonths, startOfDay, endOfDay } from 'date-fns';
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
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover';
import { Calendar } from '@/components/ui/calendar';
import {
  ArrowLeft,
  Calendar as CalendarIcon,
  Search,
  Download,
  RefreshCw,
  DoorOpen,
  Users,
  AlertCircle
} from 'lucide-react';
import { apiConfig, getAuthHeaders } from '@/lib/api';

interface AuditDoorEvent {
  id: number;
  node_id: string;
  reader_id: string;
  door_name?: string;
  rfid_uid: string;
  patient_id?: string;
  patient_name?: string;
  action: string;
  timestamp: string;
}

interface AuditAdmission {
  id?: number;
  patient_id: string;
  name: string;
  ward?: string;
  ble_minor?: number;
  rfid_uid?: string;
  assigned_at: string;
  unassigned_at?: string;
}

const AuditLogs: React.FC = () => {
  const navigate = useNavigate();
  const [activeTab, setActiveTab] = useState<'doors' | 'admissions'>('doors');
  const [searchQuery, setSearchQuery] = useState('');
  const [dateRange, setDateRange] = useState<{ from: Date; to: Date }>({
    from: subMonths(new Date(), 3),
    to: new Date(),
  });

  // Query door events
  const {
    data: doorEvents = [],
    isLoading: isLoadingDoors,
    isRefetching: isRefetchingDoors,
    refetch: refetchDoors,
  } = useQuery<AuditDoorEvent[]>({
    queryKey: ['audit-door-events', dateRange.from, dateRange.to, searchQuery],
    queryFn: async () => {
      const params = new URLSearchParams({
        start_date: format(startOfDay(dateRange.from), "yyyy-MM-dd'T'HH:mm:ss"),
        end_date: format(endOfDay(dateRange.to), "yyyy-MM-dd'T'HH:mm:ss"),
        ...(searchQuery && { search: searchQuery }),
      });
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/audit/door-events?${params}`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch door events');
      return response.json();
    },
  });

  // Query admissions / assignments
  const {
    data: admissions = [],
    isLoading: isLoadingAdmissions,
    isRefetching: isRefetchingAdmissions,
    refetch: refetchAdmissions,
  } = useQuery<AuditAdmission[]>({
    queryKey: ['audit-admissions', dateRange.from, dateRange.to, searchQuery],
    queryFn: async () => {
      const params = new URLSearchParams({
        start_date: format(startOfDay(dateRange.from), "yyyy-MM-dd'T'HH:mm:ss"),
        end_date: format(endOfDay(dateRange.to), "yyyy-MM-dd'T'HH:mm:ss"),
        ...(searchQuery && { search: searchQuery }),
      });
      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/audit/admissions?${params}`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch admissions');
      return response.json();
    },
  });

  const handleRefresh = () => {
    if (activeTab === 'doors') {
      refetchDoors();
    } else {
      refetchAdmissions();
    }
  };

  const handleExportCSV = () => {
    let headers: string[] = [];
    let rows: string[][] = [];
    let filename = '';

    if (activeTab === 'doors') {
      headers = ['Timestamp', 'Patient Name', 'Patient ID', 'Door Name', 'Action', 'RFID Tag', 'Reader ID', 'Node ID'];
      rows = doorEvents.map(event => [
        format(new Date(event.timestamp), 'yyyy-MM-dd HH:mm:ss'),
        event.patient_name || 'Unknown',
        event.patient_id || 'N/A',
        event.door_name || 'Unknown',
        event.action.toUpperCase(),
        event.rfid_uid,
        event.reader_id,
        event.node_id
      ]);
      filename = `door_access_audit_${format(new Date(), 'yyyyMMdd_HHmmss')}.csv`;
    } else {
      headers = ['Assigned At', 'Unassigned At', 'Status', 'Patient Name', 'Patient ID', 'Ward/Room', 'RFID Tag', 'BLE Minor'];
      rows = admissions.map(adm => [
        format(new Date(adm.assigned_at), 'yyyy-MM-dd HH:mm:ss'),
        adm.unassigned_at ? format(new Date(adm.unassigned_at), 'yyyy-MM-dd HH:mm:ss') : 'Active',
        adm.unassigned_at ? 'Discharged/Deassigned' : 'Currently Active',
        adm.name,
        adm.patient_id,
        adm.ward || 'N/A',
        adm.rfid_uid || 'N/A',
        adm.ble_minor !== undefined ? String(adm.ble_minor) : 'N/A'
      ]);
      filename = `patient_admissions_audit_${format(new Date(), 'yyyyMMdd_HHmmss')}.csv`;
    }

    const csvContent = [
      headers.join(','),
      ...rows.map(e => e.map(val => `"${val.replace(/"/g, '""')}"`).join(','))
    ].join('\n');

    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.setAttribute('href', url);
    link.setAttribute('download', filename);
    link.style.visibility = 'hidden';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  const isCurrentLoading = activeTab === 'doors' ? isLoadingDoors : isLoadingAdmissions;
  const isCurrentRefetching = activeTab === 'doors' ? isRefetchingDoors : isRefetchingAdmissions;

  return (
    <div className="min-h-screen bg-gradient-subtle py-8 px-6">
      <div className="container mx-auto max-w-6xl space-y-6">
        
        {/* Navigation & Title */}
        <div className="flex flex-col sm:flex-row justify-between items-start sm:items-center gap-4">
          <div className="flex items-center gap-3">
            <Button
              variant="outline"
              size="icon"
              onClick={() => navigate('/')}
              className="h-10 w-10 shadow-sm"
            >
              <ArrowLeft className="h-5 w-5" />
            </Button>
            <div>
              <h1 className="text-3xl font-bold text-foreground tracking-tight">System Audit logs</h1>
              <p className="text-sm text-muted-foreground">Audit door events and patient admissions over the past 3 months</p>
            </div>
          </div>
          
          <div className="flex flex-wrap items-center gap-3 w-full sm:w-auto">
            {/* Date Picker */}
            <Popover>
              <PopoverTrigger asChild>
                <Button variant="outline" className="w-full sm:w-[260px] justify-start text-left font-normal shadow-sm">
                  <CalendarIcon className="mr-2 h-4 w-4 text-muted-foreground" />
                  {format(dateRange.from, 'MMM dd, yyyy')} - {format(dateRange.to, 'MMM dd, yyyy')}
                </Button>
              </PopoverTrigger>
              <PopoverContent className="w-auto p-0" align="end">
                <Calendar
                  initialFocus
                  mode="range"
                  defaultMonth={dateRange.from}
                  selected={dateRange}
                  onSelect={(range) => {
                    if (range?.from) {
                      setDateRange({
                        from: range.from,
                        to: range.to || range.from
                      });
                    }
                  }}
                  numberOfMonths={2}
                />
              </PopoverContent>
            </Popover>

            <Button
              variant="outline"
              size="icon"
              onClick={handleRefresh}
              disabled={isCurrentLoading}
              className="shadow-sm"
              title="Refresh log data"
            >
              <RefreshCw className={`h-4 w-4 ${isCurrentRefetching ? 'animate-spin' : ''}`} />
            </Button>

            <Button
              onClick={handleExportCSV}
              disabled={isCurrentLoading || (activeTab === 'doors' ? doorEvents.length === 0 : admissions.length === 0)}
              className="bg-gradient-primary text-white gap-2 shadow-md w-full sm:w-auto"
            >
              <Download className="h-4 w-4" />
              Export CSV
            </Button>
          </div>
        </div>

        {/* Search & Filters */}
        <div className="relative">
          <Search className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-muted-foreground pointer-events-none" />
          <Input
            placeholder="Search by patient name, patient ID, door name, or RFID tag UID..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="pl-10 h-11 bg-card border shadow-sm rounded-lg"
          />
        </div>

        {/* Tabs Grid */}
        <Tabs
          defaultValue="doors"
          value={activeTab}
          onValueChange={(val) => setActiveTab(val as any)}
          className="space-y-4"
        >
          <TabsList className="grid w-full sm:w-[400px] grid-cols-2 bg-muted p-1 rounded-lg">
            <TabsTrigger value="doors" className="flex items-center gap-2 py-2">
              <DoorOpen className="h-4 w-4" />
              Door Access Logs
            </TabsTrigger>
            <TabsTrigger value="admissions" className="flex items-center gap-2 py-2">
              <Users className="h-4 w-4" />
              Patient Admissions
            </TabsTrigger>
          </TabsList>

          {/* Door Access Logs Content */}
          <TabsContent value="doors">
            <Card className="shadow-medical">
              <CardHeader className="pb-3 border-b">
                <CardTitle className="text-lg font-semibold flex items-center gap-2">
                  <DoorOpen className="h-5 w-5 text-primary" />
                  RFID Door Entry & Exit Records
                </CardTitle>
                <CardDescription>
                  Tracks real-time entry and exit events registered by RFID readers at ward doors.
                </CardDescription>
              </CardHeader>
              <CardContent className="p-0">
                {isCurrentLoading ? (
                  <div className="flex flex-col items-center justify-center py-20 gap-3">
                    <RefreshCw className="h-8 w-8 animate-spin text-primary" />
                    <p className="text-sm text-muted-foreground">Fetching door access logs...</p>
                  </div>
                ) : doorEvents.length === 0 ? (
                  <div className="flex flex-col items-center justify-center py-20 text-center space-y-3">
                    <AlertCircle className="h-10 w-10 text-muted-foreground" />
                    <h3 className="font-semibold text-base text-foreground">No Door Events Found</h3>
                    <p className="text-sm text-muted-foreground max-w-sm">
                      There are no door events recorded for the selected date range or search query.
                    </p>
                  </div>
                ) : (
                  <ScrollArea className="h-[550px] w-full">
                    <div className="divide-y">
                      {/* Header Row */}
                      <div className="grid grid-cols-12 gap-4 px-6 py-3 bg-muted/50 text-xs font-semibold text-muted-foreground tracking-wider uppercase">
                        <div className="col-span-3">Timestamp</div>
                        <div className="col-span-3">Patient Name</div>
                        <div className="col-span-2">Patient ID</div>
                        <div className="col-span-2">Door / Reader</div>
                        <div className="col-span-2 text-right">Action</div>
                      </div>
                      
                      {/* Data Rows */}
                      {doorEvents.map((event) => (
                        <div
                          key={event.id}
                          className="grid grid-cols-12 gap-4 px-6 py-4 items-center text-sm hover:bg-muted/30 transition-colors"
                        >
                          <div className="col-span-3 font-medium text-muted-foreground">
                            {format(new Date(event.timestamp), 'MMM dd, yyyy HH:mm:ss')}
                          </div>
                          <div className="col-span-3 font-semibold text-foreground">
                            {event.patient_name || 'Unknown Patient'}
                          </div>
                          <div className="col-span-2 text-muted-foreground font-mono text-xs">
                            {event.patient_id || 'N/A'}
                          </div>
                          <div className="col-span-2 flex flex-col gap-0.5">
                            <span className="font-medium text-foreground">{event.door_name || `Door ${event.reader_id}`}</span>
                            <span className="text-[10px] text-muted-foreground font-mono">{event.reader_id}</span>
                          </div>
                          <div className="col-span-2 text-right">
                            <Badge
                              variant={event.action === 'entry' ? 'default' : 'secondary'}
                              className={
                                event.action === 'entry'
                                  ? 'bg-emerald-100 text-emerald-800 hover:bg-emerald-100 border-none'
                                  : 'bg-amber-100 text-amber-800 hover:bg-amber-100 border-none'
                              }
                            >
                              {event.action.toUpperCase()}
                            </Badge>
                          </div>
                        </div>
                      ))}
                    </div>
                  </ScrollArea>
                )}
              </CardContent>
            </Card>
          </TabsContent>

          {/* Patient Admissions Content */}
          <TabsContent value="admissions">
            <Card className="shadow-medical">
              <CardHeader className="pb-3 border-b">
                <CardTitle className="text-lg font-semibold flex items-center gap-2">
                  <Users className="h-5 w-5 text-primary" />
                  Patient Tag & Ward Assignment History
                </CardTitle>
                <CardDescription>
                  Tracks when patients were admitted, assigned wristband tags, or deassigned/discharged.
                </CardDescription>
              </CardHeader>
              <CardContent className="p-0">
                {isCurrentLoading ? (
                  <div className="flex flex-col items-center justify-center py-20 gap-3">
                    <RefreshCw className="h-8 w-8 animate-spin text-primary" />
                    <p className="text-sm text-muted-foreground">Fetching patient admissions history...</p>
                  </div>
                ) : admissions.length === 0 ? (
                  <div className="flex flex-col items-center justify-center py-20 text-center space-y-3">
                    <AlertCircle className="h-10 w-10 text-muted-foreground" />
                    <h3 className="font-semibold text-base text-foreground">No Admission Records Found</h3>
                    <p className="text-sm text-muted-foreground max-w-sm">
                      There are no patient admissions or tag assignments recorded in this period.
                    </p>
                  </div>
                ) : (
                  <ScrollArea className="h-[550px] w-full">
                    <div className="divide-y">
                      {/* Header Row */}
                      <div className="grid grid-cols-12 gap-4 px-6 py-3 bg-muted/50 text-xs font-semibold text-muted-foreground tracking-wider uppercase">
                        <div className="col-span-3">Assigned At</div>
                        <div className="col-span-3">Unassigned At</div>
                        <div className="col-span-3">Patient Name / ID</div>
                        <div className="col-span-2">Ward / Room</div>
                        <div className="col-span-1 text-right">Status</div>
                      </div>
                      
                      {/* Data Rows */}
                      {admissions.map((adm, idx) => (
                        <div
                          key={adm.id || `active-${idx}`}
                          className="grid grid-cols-12 gap-4 px-6 py-4 items-center text-sm hover:bg-muted/30 transition-colors"
                        >
                          <div className="col-span-3 font-medium text-muted-foreground">
                            {format(new Date(adm.assigned_at), 'MMM dd, yyyy HH:mm:ss')}
                          </div>
                          <div className="col-span-3 font-medium text-muted-foreground">
                            {adm.unassigned_at
                              ? format(new Date(adm.unassigned_at), 'MMM dd, yyyy HH:mm:ss')
                              : <span className="text-emerald-600 font-semibold">— Active</span>
                            }
                          </div>
                          <div className="col-span-3 flex flex-col gap-0.5">
                            <span className="font-semibold text-foreground">{adm.name}</span>
                            <span className="text-xs text-muted-foreground font-mono">{adm.patient_id}</span>
                          </div>
                          <div className="col-span-2 flex flex-col gap-0.5">
                            <span className="font-medium text-foreground">{adm.ward || 'General Ward'}</span>
                            {adm.rfid_uid && (
                              <span className="text-[10px] text-muted-foreground font-mono">Tag: {adm.rfid_uid}</span>
                            )}
                          </div>
                          <div className="col-span-1 text-right">
                            <Badge
                              variant={adm.unassigned_at ? 'secondary' : 'default'}
                              className={
                                !adm.unassigned_at
                                  ? 'bg-emerald-100 text-emerald-800 hover:bg-emerald-100 border-none'
                                  : 'bg-zinc-100 text-zinc-800 hover:bg-zinc-100 border-none'
                              }
                            >
                              {adm.unassigned_at ? 'HISTORIC' : 'ACTIVE'}
                            </Badge>
                          </div>
                        </div>
                      ))}
                    </div>
                  </ScrollArea>
                )}
              </CardContent>
            </Card>
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
};

export default AuditLogs;
