import React, { useState, useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { format, subDays, startOfDay, endOfDay } from 'date-fns';
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
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Calendar } from '@/components/ui/calendar';
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover';
import {
  BarChart,
  Bar,
  LineChart,
  Line,
  PieChart,
  Pie,
  Cell,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  AreaChart,
  Area,
} from 'recharts';
import {
  FileText,
  Download,
  Calendar as CalendarIcon,
  BarChart3,
  PieChart as PieChartIcon,
  TrendingUp,
  Users,
  Activity,
  AlertTriangle,
  Clock,
  MapPin,
  Filter,
  RefreshCw,
} from 'lucide-react';
import { apiConfig, getAuthHeaders } from '@/lib/api';
import { useAuth } from '@/contexts/AuthContext';
import RoleGuard from '@/components/auth/RoleGuard';

interface ReportData {
  patientActivity: {
    date: string;
    totalPatients: number;
    activePatients: number;
    averageSteps: number;
    averageHeartRate: number;
  }[];
  alertStatistics: {
    date: string;
    totalAlerts: number;
    criticalAlerts: number;
    resolvedAlerts: number;
    averageResponseTime: number;
  }[];
  zoneOccupancy: {
    zoneName: string;
    totalVisits: number;
    averageDuration: number;
    violations: number;
  }[];
  systemPerformance: {
    date: string;
    uptime: number;
    deviceConnectivity: number;
    dataTransmissionRate: number;
  }[];
  patientMetrics: {
    patientId: string;
    patientName: string;
    totalSteps: number;
    averageHeartRate: number;
    averageTemperature: number;
    alertsGenerated: number;
    zoneViolations: number;
  }[];
}

interface ExportOptions {
  format: 'csv' | 'pdf' | 'json';
  dateRange: {
    from: Date;
    to: Date;
  };
  includeCharts: boolean;
  sections: string[];
}

const ReportingDashboard: React.FC = () => {
  const { user } = useAuth();
  const [dateRange, setDateRange] = useState({
    from: subDays(new Date(), 7),
    to: new Date(),
  });
  const [reportType, setReportType] = useState<'summary' | 'detailed' | 'custom'>('summary');
  const [selectedMetrics, setSelectedMetrics] = useState<string[]>(['activity', 'alerts', 'zones']);
  const [isExporting, setIsExporting] = useState(false);

  // Fetch report data
  const { data: reportData, isLoading, refetch } = useQuery({
    queryKey: ['report-data', dateRange.from, dateRange.to, selectedMetrics],
    queryFn: async () => {
      const params = new URLSearchParams({
        from: format(dateRange.from, 'yyyy-MM-dd'),
        to: format(dateRange.to, 'yyyy-MM-dd'),
        metrics: selectedMetrics.join(','),
      });

      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/reports/data?${params}`, {
        headers: getAuthHeaders(),
      });
      if (!response.ok) throw new Error('Failed to fetch report data');
      return response.json() as ReportData;
    },
    refetchInterval: 300000, // 5 minutes
  });

  const handleExport = async (fileFormat: 'csv' | 'pdf' | 'json') => {
    setIsExporting(true);
    try {
      const params = new URLSearchParams({
        format: fileFormat,
        from: format(dateRange.from, 'yyyy-MM-dd'),
        to: format(dateRange.to, 'yyyy-MM-dd'),
        metrics: selectedMetrics.join(','),
        type: reportType,
      });

      const response = await fetch(`${apiConfig.API_BASE_URL}/api/v1/reports/export?${params}`, {
        headers: getAuthHeaders(),
      });

      if (!response.ok) throw new Error('Export failed');

      const blob = await response.blob();
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `halo-watch-report-${format(new Date(), 'yyyy-MM-dd')}.${fileFormat}`;
      document.body.appendChild(a);
      a.click();
      window.URL.revokeObjectURL(url);
      document.body.removeChild(a);
    } catch (error) {
      console.error('Export error:', error);
    } finally {
      setIsExporting(false);
    }
  };

  const renderPatientActivityChart = () => {
    if (!reportData?.patientActivity) return null;

    return (
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Activity className="h-5 w-5" />
            Patient Activity Trends
          </CardTitle>
        </CardHeader>
        <CardContent>
          <ResponsiveContainer width="100%" height={300}>
            <AreaChart data={reportData.patientActivity}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis dataKey="date" tickFormatter={(value) => format(new Date(value), 'MMM dd')} />
              <YAxis />
              <Tooltip labelFormatter={(value) => format(new Date(value), 'MMM dd, yyyy')} />
              <Legend />
              <Area
                type="monotone"
                dataKey="totalPatients"
                stackId="1"
                stroke="#3b82f6"
                fill="#3b82f6"
                fillOpacity={0.6}
                name="Total Patients"
              />
              <Area
                type="monotone"
                dataKey="activePatients"
                stackId="2"
                stroke="#10b981"
                fill="#10b981"
                fillOpacity={0.6}
                name="Active Patients"
              />
            </AreaChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>
    );
  };

  const renderAlertStatistics = () => {
    if (!reportData?.alertStatistics) return null;

    return (
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <AlertTriangle className="h-5 w-5" />
            Alert Statistics
          </CardTitle>
        </CardHeader>
        <CardContent>
          <ResponsiveContainer width="100%" height={300}>
            <BarChart data={reportData.alertStatistics}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis dataKey="date" tickFormatter={(value) => format(new Date(value), 'MMM dd')} />
              <YAxis />
              <Tooltip labelFormatter={(value) => format(new Date(value), 'MMM dd, yyyy')} />
              <Legend />
              <Bar dataKey="totalAlerts" fill="#f59e0b" name="Total Alerts" />
              <Bar dataKey="criticalAlerts" fill="#ef4444" name="Critical Alerts" />
              <Bar dataKey="resolvedAlerts" fill="#10b981" name="Resolved Alerts" />
            </BarChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>
    );
  };

  const renderZoneOccupancyChart = () => {
    if (!reportData?.zoneOccupancy) return null;

    const COLORS = ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#06b6d4'];

    return (
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <MapPin className="h-5 w-5" />
            Zone Occupancy Distribution
          </CardTitle>
        </CardHeader>
        <CardContent>
          <ResponsiveContainer width="100%" height={300}>
            <PieChart>
              <Pie
                data={reportData.zoneOccupancy}
                cx="50%"
                cy="50%"
                labelLine={false}
                label={({ name, percent }) => `${name} ${(percent * 100).toFixed(0)}%`}
                outerRadius={80}
                fill="#8884d8"
                dataKey="totalVisits"
              >
                {reportData.zoneOccupancy.map((entry, index) => (
                  <Cell key={`cell-${index}`} fill={COLORS[index % COLORS.length]} />
                ))}
              </Pie>
              <Tooltip />
            </PieChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>
    );
  };

  const renderSystemPerformance = () => {
    if (!reportData?.systemPerformance) return null;

    return (
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <BarChart3 className="h-5 w-5" />
            System Performance
          </CardTitle>
        </CardHeader>
        <CardContent>
          <ResponsiveContainer width="100%" height={300}>
            <LineChart data={reportData.systemPerformance}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis dataKey="date" tickFormatter={(value) => format(new Date(value), 'MMM dd')} />
              <YAxis />
              <Tooltip labelFormatter={(value) => format(new Date(value), 'MMM dd, yyyy')} />
              <Legend />
              <Line
                type="monotone"
                dataKey="uptime"
                stroke="#10b981"
                strokeWidth={2}
                name="Uptime %"
              />
              <Line
                type="monotone"
                dataKey="deviceConnectivity"
                stroke="#3b82f6"
                strokeWidth={2}
                name="Device Connectivity %"
              />
            </LineChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>
    );
  };

  const renderPatientMetricsTable = () => {
    if (!reportData?.patientMetrics) return null;

    return (
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Users className="h-5 w-5" />
            Patient Metrics Summary
          </CardTitle>
        </CardHeader>
        <CardContent>
          <ScrollArea className="h-96">
            <div className="space-y-2">
              <div className="grid grid-cols-7 gap-4 p-3 bg-muted rounded-lg text-sm font-medium">
                <div>Patient</div>
                <div>Steps</div>
                <div>Avg HR</div>
                <div>Avg Temp</div>
                <div>Alerts</div>
                <div>Violations</div>
                <div>Status</div>
              </div>
              {reportData.patientMetrics.map((patient) => (
                <div key={patient.patientId} className="grid grid-cols-7 gap-4 p-3 border rounded-lg text-sm">
                  <div className="font-medium">{patient.patientName}</div>
                  <div>{patient.totalSteps.toLocaleString()}</div>
                  <div>{patient.averageHeartRate} bpm</div>
                  <div>{patient.averageTemperature}°C</div>
                  <div>
                    <Badge variant={patient.alertsGenerated > 5 ? "destructive" : "outline"}>
                      {patient.alertsGenerated}
                    </Badge>
                  </div>
                  <div>
                    <Badge variant={patient.zoneViolations > 0 ? "destructive" : "outline"}>
                      {patient.zoneViolations}
                    </Badge>
                  </div>
                  <div>
                    <Badge variant={
                      patient.alertsGenerated > 5 || patient.zoneViolations > 0 
                        ? "destructive" 
                        : "default"
                    }>
                      {patient.alertsGenerated > 5 || patient.zoneViolations > 0 ? "Attention" : "Normal"}
                    </Badge>
                  </div>
                </div>
              ))}
            </div>
          </ScrollArea>
        </CardContent>
      </Card>
    );
  };

  const renderSummaryCards = () => {
    if (!reportData) return null;

    const totalPatients = reportData.patientActivity?.reduce((sum, day) => sum + day.totalPatients, 0) || 0;
    const totalAlerts = reportData.alertStatistics?.reduce((sum, day) => sum + day.totalAlerts, 0) || 0;
    const criticalAlerts = reportData.alertStatistics?.reduce((sum, day) => sum + day.criticalAlerts, 0) || 0;
    const avgUptime = reportData.systemPerformance?.reduce((sum, day) => sum + day.uptime, 0) / (reportData.systemPerformance?.length || 1) || 0;

    return (
      <div className="grid grid-cols-1 md:grid-cols-4 gap-4 mb-6">
        <Card>
          <CardContent className="p-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Patients</p>
                <p className="text-2xl font-bold">{totalPatients}</p>
              </div>
              <Users className="h-8 w-8 text-blue-500" />
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardContent className="p-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Alerts</p>
                <p className="text-2xl font-bold">{totalAlerts}</p>
              </div>
              <AlertTriangle className="h-8 w-8 text-yellow-500" />
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardContent className="p-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Critical Alerts</p>
                <p className="text-2xl font-bold text-red-600">{criticalAlerts}</p>
              </div>
              <AlertTriangle className="h-8 w-8 text-red-500" />
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardContent className="p-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">System Uptime</p>
                <p className="text-2xl font-bold text-green-600">{avgUptime.toFixed(1)}%</p>
              </div>
              <TrendingUp className="h-8 w-8 text-green-500" />
            </div>
          </CardContent>
        </Card>
      </div>
    );
  };

  return (
    <Card className="h-full">
      <CardHeader>
        <div className="flex items-center justify-between">
          <div>
            <CardTitle className="flex items-center gap-2">
              <FileText className="h-5 w-5" />
              Reports & Analytics
            </CardTitle>
            <CardDescription>
              Generate comprehensive reports and export data for analysis
            </CardDescription>
          </div>
          
          <div className="flex items-center gap-2">
            <Button
              variant="outline"
              size="sm"
              onClick={() => refetch()}
              disabled={isLoading}
            >
              <RefreshCw className={`h-4 w-4 mr-2 ${isLoading ? 'animate-spin' : ''}`} />
              Refresh
            </Button>
            
            <RoleGuard allowedRoles={['admin', 'doctor']}>
              <Popover>
                <PopoverTrigger asChild>
                  <Button>
                    <Download className="h-4 w-4 mr-2" />
                    Export
                  </Button>
                </PopoverTrigger>
                <PopoverContent className="w-80">
                  <div className="space-y-4">
                    <h4 className="font-medium">Export Report</h4>
                    <div className="space-y-2">
                      <Label>Format</Label>
                      <div className="flex gap-2">
                        <Button
                          variant="outline"
                          size="sm"
                          onClick={() => handleExport('csv')}
                          disabled={isExporting}
                        >
                          CSV
                        </Button>
                        <Button
                          variant="outline"
                          size="sm"
                          onClick={() => handleExport('pdf')}
                          disabled={isExporting}
                        >
                          PDF
                        </Button>
                        <Button
                          variant="outline"
                          size="sm"
                          onClick={() => handleExport('json')}
                          disabled={isExporting}
                        >
                          JSON
                        </Button>
                      </div>
                    </div>
                  </div>
                </PopoverContent>
              </Popover>
            </RoleGuard>
          </div>
        </div>

        {/* Date Range and Filters */}
        <div className="flex flex-wrap items-center gap-4 mt-4">
          <div className="flex items-center gap-2">
            <Label>Date Range:</Label>
            <Popover>
              <PopoverTrigger asChild>
                <Button variant="outline" size="sm">
                  <CalendarIcon className="h-4 w-4 mr-2" />
                  {format(dateRange.from, 'MMM dd')} - {format(dateRange.to, 'MMM dd')}
                </Button>
              </PopoverTrigger>
              <PopoverContent className="w-auto p-0" align="start">
                <Calendar
                  initialFocus
                  mode="range"
                  defaultMonth={dateRange.from}
                  selected={dateRange}
                  onSelect={(range) => {
                    if (range?.from && range?.to) {
                      setDateRange({ from: range.from, to: range.to });
                    }
                  }}
                  numberOfMonths={2}
                />
              </PopoverContent>
            </Popover>
          </div>

          <div className="flex items-center gap-2">
            <Label>Report Type:</Label>
            <Select value={reportType} onValueChange={(value: any) => setReportType(value)}>
              <SelectTrigger className="w-32">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="summary">Summary</SelectItem>
                <SelectItem value="detailed">Detailed</SelectItem>
                <SelectItem value="custom">Custom</SelectItem>
              </SelectContent>
            </Select>
          </div>
        </div>
      </CardHeader>

      <CardContent>
        {isLoading ? (
          <div className="flex items-center justify-center h-96">
            <div className="text-center">
              <RefreshCw className="h-8 w-8 animate-spin mx-auto mb-4" />
              <p className="text-muted-foreground">Loading report data...</p>
            </div>
          </div>
        ) : (
          <Tabs defaultValue="overview" className="w-full">
            <TabsList className="grid w-full grid-cols-4">
              <TabsTrigger value="overview">Overview</TabsTrigger>
              <TabsTrigger value="activity">Activity</TabsTrigger>
              <TabsTrigger value="alerts">Alerts</TabsTrigger>
              <TabsTrigger value="performance">Performance</TabsTrigger>
            </TabsList>

            <TabsContent value="overview" className="space-y-6">
              {renderSummaryCards()}
              <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
                {renderPatientActivityChart()}
                {renderZoneOccupancyChart()}
              </div>
              {renderPatientMetricsTable()}
            </TabsContent>

            <TabsContent value="activity" className="space-y-6">
              {renderPatientActivityChart()}
              {renderPatientMetricsTable()}
            </TabsContent>

            <TabsContent value="alerts" className="space-y-6">
              {renderAlertStatistics()}
            </TabsContent>

            <TabsContent value="performance" className="space-y-6">
              {renderSystemPerformance()}
            </TabsContent>
          </Tabs>
        )}
      </CardContent>
    </Card>
  );
};

export default ReportingDashboard;