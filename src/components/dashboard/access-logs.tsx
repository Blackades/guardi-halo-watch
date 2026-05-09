import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Search, Download, Clock, User, MapPin } from "lucide-react";
import { fetchAccessLogs, type AccessLogDto, type AccessAction } from "@/lib/api";
import { useToast } from "@/hooks/use-toast";

export default function AccessLogs() {
  const [searchTerm, setSearchTerm] = useState("");
  const [filter, setFilter] = useState<'all' | 'entry' | 'exit' | 'denied'>('all');
  const { data: serverLogs, isLoading, isError } = useQuery<AccessLogDto[]>({
    queryKey: ["access-logs"],
    queryFn: fetchAccessLogs,
    refetchInterval: 30_000,
  });

  const filteredLogs = (serverLogs ?? []).filter(log => {
    const matchesSearch = 
      log.patientName.toLowerCase().includes(searchTerm.toLowerCase()) ||
      log.patientId.toLowerCase().includes(searchTerm.toLowerCase()) ||
      log.room.toLowerCase().includes(searchTerm.toLowerCase());
    
    const matchesFilter = filter === 'all' || log.action === filter;
    
    return matchesSearch && matchesFilter;
  });

  const { toast } = useToast();

  const getActionBadge = (action: AccessAction) => {
    switch (action) {
      case 'entry':
        return <Badge variant="default" className="bg-success text-success-foreground">Entry</Badge>;
      case 'exit':
        return <Badge variant="default" className="bg-info text-info-foreground">Exit</Badge>;
      case 'denied':
        return <Badge variant="default" className="bg-destructive text-destructive-foreground">Denied</Badge>;
    }
  };

  const handleExportCsv = () => {
    if (!serverLogs || serverLogs.length === 0) {
      toast({
        title: "No data to export",
        description: "There are currently no access logs available for export.",
      });
      return;
    }

    const rows = filteredLogs.length > 0 ? filteredLogs : serverLogs;

    const headers = [
      "timestamp",
      "patient_id",
      "patient_name",
      "room",
      "action",
      "duration",
      "rfid_id",
    ];

    const csvLines = [
      headers.join(","),
      ...rows.map((log) => {
        const safe = (value: string | null | undefined) => {
          const str = value ?? "";
          // Escape quotes and commas
          if (/[",\n]/.test(str)) {
            return `"${str.replace(/"/g, '""')}"`;
          }
          return str;
        };

        const ts = new Date(log.timestamp).toISOString();

        return [
          safe(ts),
          safe(log.patientId),
          safe(log.patientName),
          safe(log.room),
          safe(log.action),
          safe(log.duration ?? ""),
          safe(log.rfidId),
        ].join(",");
      }),
    ].join("\n");

    const blob = new Blob([csvLines], { type: "text/csv;charset=utf-8;" });
    const url = URL.createObjectURL(blob);

    const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
    const a = document.createElement("a");
    a.href = url;
    a.download = `room-access-logs-${timestamp}.csv`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  return (
    <Card className="h-full">
      <CardHeader>
        <CardTitle className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Clock className="h-5 w-5" />
            <span>Room Access Logs</span>
          </div>
          <Button
            variant="outline"
            size="sm"
            className="gap-2"
            onClick={handleExportCsv}
            disabled={isLoading}
          >
            <Download className="h-4 w-4" />
            Export CSV
          </Button>
        </CardTitle>
        
        <div className="flex gap-2">
          <div className="relative flex-1">
            <Search className="absolute left-3 top-1/2 transform -translate-y-1/2 h-4 w-4 text-muted-foreground" />
            <Input
              placeholder="Search logs..."
              value={searchTerm}
              onChange={(e) => setSearchTerm(e.target.value)}
              className="pl-10"
            />
          </div>
          <div className="flex gap-1">
            {(['all', 'entry', 'exit', 'denied'] as const).map((filterType) => (
              <Button
                key={filterType}
                variant={filter === filterType ? "default" : "outline"}
                size="sm"
                onClick={() => setFilter(filterType)}
                className="capitalize"
              >
                {filterType}
              </Button>
            ))}
          </div>
        </div>
      </CardHeader>
      
      <CardContent className="p-0">
        <div className="max-h-80 overflow-y-auto">
          {isError && !serverLogs && (
            <div className="p-4 text-sm text-destructive">
              Unable to load room access logs from server.
            </div>
          )}
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead className="w-24">Time</TableHead>
                <TableHead>Patient</TableHead>
                <TableHead>Room</TableHead>
                <TableHead>Action</TableHead>
                <TableHead>Duration</TableHead>
                <TableHead className="w-24">RFID</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {filteredLogs.map((log) => {
                const ts = new Date(log.timestamp);
                return (
                  <TableRow key={log.id} className="hover:bg-accent/50">
                    <TableCell className="font-mono text-xs">
                      {ts.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                    </TableCell>
                    <TableCell>
                      <div className="flex flex-col">
                        <span className="font-medium text-sm">{log.patientName}</span>
                        <span className="text-xs text-muted-foreground">{log.patientId}</span>
                      </div>
                    </TableCell>
                    <TableCell>
                      <div className="flex items-center gap-1">
                        <MapPin className="h-3 w-3 text-muted-foreground" />
                        <span className="text-sm">{log.room}</span>
                      </div>
                    </TableCell>
                    <TableCell>
                      {getActionBadge(log.action)}
                    </TableCell>
                    <TableCell className="text-sm">
                      {log.duration || '-'}
                    </TableCell>
                    <TableCell className="font-mono text-xs text-muted-foreground">
                      {log.rfidId.slice(-3)}
                    </TableCell>
                  </TableRow>
                );
              })}
            </TableBody>
          </Table>
          
          {!isLoading && filteredLogs.length === 0 && (
            <div className="p-8 text-center text-muted-foreground">
              <User className="h-8 w-8 mx-auto mb-2 opacity-50" />
              <p>No access logs found</p>
            </div>
          )}
        </div>
      </CardContent>
    </Card>
  );
}