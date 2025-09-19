import { useState } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Search, Download, Filter, Clock, User, MapPin } from "lucide-react";

interface AccessLog {
  id: string;
  patientId: string;
  patientName: string;
  room: string;
  action: 'entry' | 'exit' | 'denied';
  timestamp: Date;
  rfidId: string;
  duration?: string;
}

const mockAccessLogs: AccessLog[] = [
  {
    id: 'LOG001',
    patientId: 'P001',
    patientName: 'John Smith',
    room: 'R101',
    action: 'entry',
    timestamp: new Date(Date.now() - 5 * 60 * 1000),
    rfidId: 'RFID_001'
  },
  {
    id: 'LOG002',
    patientId: 'P002',
    patientName: 'Sarah Johnson',
    room: 'R104',
    action: 'entry',
    timestamp: new Date(Date.now() - 12 * 60 * 1000),
    rfidId: 'RFID_002',
    duration: '45 min'
  },
  {
    id: 'LOG003',
    patientId: 'P004',
    patientName: 'Emily Wilson',
    room: 'Common Area',
    action: 'denied',
    timestamp: new Date(Date.now() - 18 * 60 * 1000),
    rfidId: 'RFID_004'
  },
  {
    id: 'LOG004',
    patientId: 'P003',
    patientName: 'Michael Davis',
    room: 'R103',
    action: 'exit',
    timestamp: new Date(Date.now() - 25 * 60 * 1000),
    rfidId: 'RFID_003',
    duration: '2h 15min'
  },
  {
    id: 'LOG005',
    patientId: 'P005',
    patientName: 'Robert Brown',
    room: 'R203',
    action: 'entry',
    timestamp: new Date(Date.now() - 32 * 60 * 1000),
    rfidId: 'RFID_005'
  },
  {
    id: 'LOG006',
    patientId: 'P001',
    patientName: 'John Smith',
    room: 'Pharmacy',
    action: 'denied',
    timestamp: new Date(Date.now() - 45 * 60 * 1000),
    rfidId: 'RFID_001'
  }
];

export default function AccessLogs() {
  const [searchTerm, setSearchTerm] = useState("");
  const [filter, setFilter] = useState<'all' | 'entry' | 'exit' | 'denied'>('all');

  const filteredLogs = mockAccessLogs.filter(log => {
    const matchesSearch = 
      log.patientName.toLowerCase().includes(searchTerm.toLowerCase()) ||
      log.patientId.toLowerCase().includes(searchTerm.toLowerCase()) ||
      log.room.toLowerCase().includes(searchTerm.toLowerCase());
    
    const matchesFilter = filter === 'all' || log.action === filter;
    
    return matchesSearch && matchesFilter;
  });

  const getActionBadge = (action: AccessLog['action']) => {
    switch (action) {
      case 'entry':
        return <Badge variant="default" className="bg-success text-success-foreground">Entry</Badge>;
      case 'exit':
        return <Badge variant="default" className="bg-info text-info-foreground">Exit</Badge>;
      case 'denied':
        return <Badge variant="default" className="bg-destructive text-destructive-foreground">Denied</Badge>;
    }
  };

  return (
    <Card className="h-full">
      <CardHeader>
        <CardTitle className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Clock className="h-5 w-5" />
            <span>Room Access Logs</span>
          </div>
          <Button variant="outline" size="sm" className="gap-2">
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
              {filteredLogs.map((log) => (
                <TableRow key={log.id} className="hover:bg-accent/50">
                  <TableCell className="font-mono text-xs">
                    {log.timestamp.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
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
              ))}
            </TableBody>
          </Table>
          
          {filteredLogs.length === 0 && (
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