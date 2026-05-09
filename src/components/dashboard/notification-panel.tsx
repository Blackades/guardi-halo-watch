import { useState, useEffect, useRef } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { ScrollArea } from "@/components/ui/scroll-area";
import { Separator } from "@/components/ui/separator";
import { 
  AlertTriangle, 
  UserX, 
  MapPin, 
  Shield, 
  X, 
  Bell, 
  BellOff,
  Check,
  Clock,
  Filter,
  Volume2,
  VolumeX,
  Search,
  AlertCircle,
  Activity,
  Users,
  Zap
} from "lucide-react";
import { 
  Dialog, 
  DialogContent, 
  DialogDescription, 
  DialogFooter, 
  DialogHeader, 
  DialogTitle 
} from "@/components/ui/dialog";
import { useToast } from "@/hooks/use-toast";
import {
  acknowledgeAlert,
  createLiveWebSocket,
  fetchAlerts,
  assignPatient,
  type NotificationDto,
  type NotificationType,
} from "@/lib/api";

interface Notification {
  id: string;
  type: NotificationType;
  title: string;
  message: string;
  timestamp: Date;
  patientId?: string;
  room?: string;
  isRead: boolean;
  acknowledged?: boolean;
  acknowledgedBy?: string;
  acknowledgedAt?: Date;
  resolved?: boolean;
  resolvedAt?: Date;
  severity?: 'low' | 'medium' | 'high' | 'critical';
  category?: 'medical' | 'security' | 'system' | 'zone';
  rfidUid?: string;
}

type AlertFilter = 'all' | 'unread' | 'acknowledged' | 'resolved';
type AlertSort = 'newest' | 'oldest' | 'severity' | 'type';

export default function NotificationPanel() {
  const { toast } = useToast();
  const queryClient = useQueryClient();
  const audioRef = useRef<HTMLAudioElement>(null);
  
  const { data } = useQuery<NotificationDto[]>({
    queryKey: ["alerts", "active"],
    queryFn: () => fetchAlerts("active"),
    refetchInterval: 20_000,
  });

  const [notifications, setNotifications] = useState<Notification[]>([]);
  const [filter, setFilter] = useState<AlertFilter>('all');
  const [sortBy, setSortBy] = useState<AlertSort>('newest');
  const [searchTerm, setSearchTerm] = useState('');
  const [soundEnabled, setSoundEnabled] = useState(true);
  const [activeTab, setActiveTab] = useState('active');
  const [assigningRfid, setAssigningRfid] = useState<string | null>(null);
  const [patientName, setPatientName] = useState("");
  const [patientId, setPatientId] = useState("");
  const [bleMinor, setBleMinor] = useState<string>("");
  const [designatedRoom, setDesignatedRoom] = useState<string>("R01");
  const [isAssigningModalOpen, setIsAssigningModalOpen] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);

  const handleOpenAssignmentModal = (rfid: string) => {
    setAssigningRfid(rfid);
    setPatientName("");
    setPatientId("");
    setBleMinor("");
    setDesignatedRoom("R01");
    setIsAssigningModalOpen(true);
  };

  const handleAssignSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!assigningRfid || !patientName || !patientId || !bleMinor) return;

    try {
      await assignPatient({
        patient_id: patientId,
        name: patientName,
        ward: designatedRoom,
        ble_minor: parseInt(bleMinor, 10),
        rfid_uid: assigningRfid,
      });

      toast({
        title: "Patient Registered Successfully",
        description: `${patientName} has been assigned to Room ${designatedRoom}.`,
      });

      setIsAssigningModalOpen(false);
      queryClient.invalidateQueries({ queryKey: ["alerts"] });
      queryClient.invalidateQueries({ queryKey: ["patients"] });
      queryClient.invalidateQueries({ queryKey: ["rooms"] });
    } catch (err: any) {
      toast({
        variant: "destructive",
        title: "Registration Failed",
        description: err.message || "An error occurred during assignment.",
      });
    }
  };

  // Mutations for alert actions
  const acknowledgeMutation = useMutation({
    mutationFn: acknowledgeAlert,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["alerts"] });
      toast({
        title: "Alert Acknowledged",
        description: "Alert has been marked as acknowledged.",
      });
    },
  });

  const resolveMutation = useMutation({
    mutationFn: async (alertId: string) => {
      // This would be a new API endpoint
      const response = await fetch(`/api/v1/alerts/${alertId}/resolve`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
      });
      if (!response.ok) throw new Error('Failed to resolve alert');
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ["alerts"] });
      toast({
        title: "Alert Resolved",
        description: "Alert has been marked as resolved.",
      });
    },
  });

  // Sync state with latest server alerts
  useEffect(() => {
    if (!data) return;
    
    const serverAlerts = data.map((n) => ({
      ...n,
      timestamp: new Date(n.timestamp),
    }));

    setNotifications((prev) => {
      // Retain transient scans (whose IDs start with 'scan-')
      const transientScans = prev.filter(n => n.id.startsWith("scan-"));
      const combined = [...serverAlerts, ...transientScans];
      
      // Remove duplicate keys (just in case)
      const unique = combined.reduce<Notification[]>((acc, current) => {
        if (!acc.some(item => item.id === current.id)) {
          acc.push(current);
        }
        return acc;
      }, []);

      // Return sorted descending by timestamp
      return unique.sort((a, b) => b.timestamp.getTime() - a.timestamp.getTime()).slice(0, 50);
    });
  }, [data]);

  // Live WebSocket feed with automatic reconnection
  useEffect(() => {
    let ws: WebSocket | null = null;
    let reconnectTimeout: NodeJS.Timeout;
    let isComponentMounted = true;

    const connect = () => {
      if (!isComponentMounted) return;
      
      console.log("[WebSocket] Attempting to connect...");
      try {
        ws = createLiveWebSocket();
        if (!ws) {
          setWsConnected(false);
          scheduleReconnect();
          return;
        }

        ws.onopen = () => {
          console.log("[WebSocket] Connected successfully!");
          if (isComponentMounted) {
            setWsConnected(true);
          }
        };

        ws.onmessage = (event) => {
          if (!isComponentMounted) return;
          try {
            const payload = JSON.parse(event.data) as {
              event: string;
              data: any;
            };
            
            console.log("[WebSocket] Event received:", payload.event, payload.data);

            if (payload.event === "alert_created") {
              const n = payload.data as NotificationDto;
              const asNotification: Notification = {
                ...n,
                timestamp: new Date(n.timestamp),
                severity: getSeverityFromType(n.type),
                category: getCategoryFromTitle(n.title),
              };

              // Insert only if not a duplicate
              setNotifications((prev) => {
                if (prev.some((item) => item.id === n.id)) return prev;
                return [asNotification, ...prev].slice(0, 50);
              });

              // Play sound for critical alerts
              if (soundEnabled && (n.type === "alert" || n.type === "warning")) {
                playAlertSound(n.type);
              }

              if (n.type === "alert" || n.type === "warning") {
                toast({
                  variant: n.type === "alert" ? "destructive" : "default",
                  title: n.title,
                  description: n.message,
                });
              }

              // Invalidate React Query active alerts list immediately
              queryClient.invalidateQueries({ queryKey: ["alerts"] });
            } 
            else if (payload.event === "tag_scanned") {
              const scanData = payload.data;
              const isAssigned = scanData.status === "assigned";
              const title = isAssigned ? "Patient Scan" : "Unknown Tag Detected";
              const message = isAssigned 
                ? `Patient ${scanData.patient_name} passed through ${scanData.door_name} (${scanData.action})`
                : `Unknown tag ${scanData.rfid_uid} detected at ${scanData.door_name} (${scanData.action})`;
              
              toast({
                variant: isAssigned ? "default" : "destructive",
                title,
                description: message,
                duration: 5000,
              });

              const scanNotification: Notification = {
                id: `scan-${scanData.rfid_uid}-${Date.now()}`,
                type: isAssigned ? "info" : "alert",
                title,
                message,
                timestamp: new Date(scanData.timestamp),
                isRead: false,
                patientId: scanData.patient_id,
                room: scanData.door_name,
                severity: isAssigned ? "low" : "high",
                category: "security",
                rfidUid: isAssigned ? undefined : scanData.rfid_uid
              };

              setNotifications((prev) => [scanNotification, ...prev].slice(0, 50));
              
              // Invalidate room status, patient list, and active alerts instantly!
              queryClient.invalidateQueries({ queryKey: ["alerts"] });
              queryClient.invalidateQueries({ queryKey: ["patients"] });
              queryClient.invalidateQueries({ queryKey: ["rooms"] });
            }
          } catch (e) {
            console.error("[WebSocket] Payload parsing error:", e);
          }
        };

        ws.onclose = () => {
          console.warn("[WebSocket] Connection closed.");
          if (isComponentMounted) {
            setWsConnected(false);
          }
          scheduleReconnect();
        };

        ws.onerror = (err) => {
          console.error("[WebSocket] Connection error:", err);
          // Handled via onclose
        };
      } catch (err) {
        console.error("[WebSocket] Connection attempt failed:", err);
        if (isComponentMounted) {
          setWsConnected(false);
        }
        scheduleReconnect();
      }
    };

    const scheduleReconnect = () => {
      if (!isComponentMounted) return;
      clearTimeout(reconnectTimeout);
      reconnectTimeout = setTimeout(() => {
        connect();
      }, 3000);
    };

    connect();

    return () => {
      isComponentMounted = false;
      clearTimeout(reconnectTimeout);
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.close();
      }
    };
  }, [toast, soundEnabled, queryClient]);

  // Helper functions
  const getSeverityFromType = (type: NotificationType): Notification['severity'] => {
    switch (type) {
      case 'alert': return 'critical';
      case 'warning': return 'high';
      case 'info': return 'medium';
      case 'success': return 'low';
      default: return 'medium';
    }
  };

  const getCategoryFromTitle = (title: string): Notification['category'] => {
    const lowerTitle = title.toLowerCase();
    if (lowerTitle.includes('heart') || lowerTitle.includes('vital') || lowerTitle.includes('medical')) return 'medical';
    if (lowerTitle.includes('zone') || lowerTitle.includes('restricted') || lowerTitle.includes('exit')) return 'zone';
    if (lowerTitle.includes('tamper') || lowerTitle.includes('security')) return 'security';
    return 'system';
  };

  const playAlertSound = (type: NotificationType) => {
    if (audioRef.current) {
      // Different sounds for different alert types
      audioRef.current.volume = type === 'alert' ? 0.8 : 0.5;
      audioRef.current.play().catch(() => {
        // Ignore audio play errors (user interaction required)
      });
    }
  };

  // Filter and sort notifications
  const filteredNotifications = notifications
    .filter(notification => {
      // Filter by status
      if (filter === 'unread' && notification.isRead) return false;
      if (filter === 'acknowledged' && !notification.acknowledged) return false;
      if (filter === 'resolved' && !notification.resolved) return false;
      
      // Filter by search term
      if (searchTerm) {
        const searchLower = searchTerm.toLowerCase();
        return (
          notification.title.toLowerCase().includes(searchLower) ||
          notification.message.toLowerCase().includes(searchLower) ||
          notification.patientId?.toLowerCase().includes(searchLower) ||
          notification.room?.toLowerCase().includes(searchLower)
        );
      }
      
      return true;
    })
    .sort((a, b) => {
      switch (sortBy) {
        case 'newest':
          return b.timestamp.getTime() - a.timestamp.getTime();
        case 'oldest':
          return a.timestamp.getTime() - b.timestamp.getTime();
        case 'severity':
          const severityOrder = { critical: 4, high: 3, medium: 2, low: 1 };
          return (severityOrder[b.severity || 'medium'] || 2) - (severityOrder[a.severity || 'medium'] || 2);
        case 'type':
          return a.type.localeCompare(b.type);
        default:
          return 0;
      }
    });

  const getNotificationIcon = (notification: Notification) => {
    const iconClass = "h-4 w-4";
    
    if (notification.category === 'medical') {
      return <Activity className={`${iconClass} text-red-500`} />;
    }
    if (notification.category === 'security') {
      return <Shield className={`${iconClass} text-orange-500`} />;
    }
    if (notification.category === 'zone') {
      return <MapPin className={`${iconClass} text-purple-500`} />;
    }
    
    switch (notification.type) {
      case 'alert':
        return <AlertTriangle className={`${iconClass} text-destructive`} />;
      case 'warning':
        return <AlertCircle className={`${iconClass} text-warning`} />;
      case 'info':
        return <Bell className={`${iconClass} text-info`} />;
      case 'success':
        return <Check className={`${iconClass} text-success`} />;
      default:
        return <Bell className={`${iconClass} text-muted-foreground`} />;
    }
  };

  const getSeverityBadge = (severity: Notification['severity']) => {
    switch (severity) {
      case 'critical':
        return <Badge variant="destructive" className="text-xs animate-pulse">Critical</Badge>;
      case 'high':
        return <Badge variant="destructive" className="text-xs">High</Badge>;
      case 'medium':
        return <Badge variant="secondary" className="text-xs">Medium</Badge>;
      case 'low':
        return <Badge variant="outline" className="text-xs">Low</Badge>;
      default:
        return null;
    }
  };

  const markAsRead = (id: string) => {
    setNotifications(prev =>
      prev.map(n => n.id === id ? { ...n, isRead: true } : n)
    );
  };

  // Local helper to call the API mutation and update local state.
  const handleAcknowledgeAlert = (id: string) => {
    acknowledgeMutation.mutate(id);
    setNotifications(prev =>
      prev.map(n => n.id === id ? { 
        ...n, 
        acknowledged: true, 
        acknowledgedAt: new Date(),
        acknowledgedBy: 'Current User' // This would come from auth context
      } : n)
    );
  };

  const resolveAlert = (id: string) => {
    resolveMutation.mutate(id);
    setNotifications(prev =>
      prev.map(n => n.id === id ? { 
        ...n, 
        resolved: true, 
        resolvedAt: new Date()
      } : n)
    );
  };

  const dismissNotification = (id: string) => {
    setNotifications(prev => prev.filter(n => n.id !== id));
  };

  const markAllAsRead = () => {
    setNotifications(prev => prev.map(n => ({ ...n, isRead: true })));
  };

  const clearAll = () => {
    setNotifications([]);
  };

  const unreadCount = notifications.filter(n => !n.isRead).length;
  const criticalCount = notifications.filter(n => n.severity === 'critical' && !n.resolved).length;
  const acknowledgedCount = notifications.filter(n => n.acknowledged && !n.resolved).length;

  return (
    <Card className="h-full">
      <CardHeader className="pb-3">
        <div className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Bell className="h-5 w-5" />
            <span>Alert Management</span>
            <span 
              className={cn(
                "h-2.5 w-2.5 rounded-full inline-block ml-1 transition-all duration-500",
                wsConnected 
                  ? "bg-emerald-500 shadow-[0_0_8px_rgba(16,185,129,0.7)]" 
                  : "bg-amber-500 animate-pulse shadow-[0_0_8px_rgba(245,158,11,0.5)]"
              )} 
              title={wsConnected ? "Live WebSocket Feed Connected" : "WebSocket Disconnected (Reconnecting...)"} 
            />
          </CardTitle>
          
          <div className="flex items-center gap-2">
            <Button
              variant="ghost"
              size="sm"
              onClick={() => setSoundEnabled(!soundEnabled)}
              className="h-8 w-8 p-0"
            >
              {soundEnabled ? <Volume2 className="h-4 w-4" /> : <VolumeX className="h-4 w-4" />}
            </Button>
            
            {criticalCount > 0 && (
              <Badge variant="destructive" className="animate-pulse">
                {criticalCount} Critical
              </Badge>
            )}
            
            {unreadCount > 0 && (
              <Badge variant="secondary">
                {unreadCount} New
              </Badge>
            )}
          </div>
        </div>

        {/* Controls */}
        <div className="flex flex-col gap-3">
          <div className="flex items-center gap-2">
            <div className="relative flex-1">
              <Search className="absolute left-2 top-2.5 h-4 w-4 text-muted-foreground" />
              <Input
                placeholder="Search alerts..."
                value={searchTerm}
                onChange={(e) => setSearchTerm(e.target.value)}
                className="pl-8 h-9"
              />
            </div>
            <Select value={filter} onValueChange={(value: AlertFilter) => setFilter(value)}>
              <SelectTrigger className="w-32 h-9">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="all">All</SelectItem>
                <SelectItem value="unread">Unread</SelectItem>
                <SelectItem value="acknowledged">Acknowledged</SelectItem>
                <SelectItem value="resolved">Resolved</SelectItem>
              </SelectContent>
            </Select>
            <Select value={sortBy} onValueChange={(value: AlertSort) => setSortBy(value)}>
              <SelectTrigger className="w-32 h-9">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="newest">Newest</SelectItem>
                <SelectItem value="oldest">Oldest</SelectItem>
                <SelectItem value="severity">Severity</SelectItem>
                <SelectItem value="type">Type</SelectItem>
              </SelectContent>
            </Select>
          </div>

          <div className="flex items-center justify-between">
            <div className="flex gap-2">
              <Button variant="outline" size="sm" onClick={markAllAsRead}>
                <Check className="h-4 w-4 mr-1" />
                Mark All Read
              </Button>
              <Button variant="outline" size="sm" onClick={clearAll}>
                <X className="h-4 w-4 mr-1" />
                Clear All
              </Button>
            </div>
            
            <div className="text-sm text-muted-foreground">
              {filteredNotifications.length} of {notifications.length} alerts
            </div>
          </div>
        </div>
      </CardHeader>

      <CardContent className="p-0">
        <Tabs value={activeTab} onValueChange={setActiveTab}>
          <TabsList className="grid w-full grid-cols-3 mx-4 mb-4">
            <TabsTrigger value="active" className="text-xs">
              Active ({notifications.filter(n => !n.resolved).length})
            </TabsTrigger>
            <TabsTrigger value="acknowledged" className="text-xs">
              Acknowledged ({acknowledgedCount})
            </TabsTrigger>
            <TabsTrigger value="resolved" className="text-xs">
              Resolved ({notifications.filter(n => n.resolved).length})
            </TabsTrigger>
          </TabsList>

          <TabsContent value={activeTab} className="mt-0">
            <ScrollArea className="h-96">
              {filteredNotifications.length === 0 ? (
                <div className="p-8 text-center text-muted-foreground">
                  <BellOff className="h-12 w-12 mx-auto mb-4 opacity-50" />
                  <p className="text-lg font-medium mb-2">No alerts found</p>
                  <p className="text-sm">
                    {searchTerm ? 'Try adjusting your search terms' : 'All quiet on the ward'}
                  </p>
                </div>
              ) : (
                <div className="space-y-1">
                  {filteredNotifications.map((notification) => (
                    <div
                      key={notification.id}
                      className={cn(
                        "border-b p-4 transition-all duration-200 hover:bg-accent/50",
                        !notification.isRead && "bg-accent/30 border-l-4 border-l-primary",
                        notification.severity === 'critical' && "border-l-4 border-l-destructive",
                        notification.acknowledged && "opacity-75",
                        notification.resolved && "opacity-50"
                      )}
                    >
                      <div className="flex items-start justify-between">
                        <div className="flex items-start gap-3 flex-1">
                          {getNotificationIcon(notification)}
                          <div className="flex-1 min-w-0">
                            <div className="flex items-center gap-2 mb-2">
                              <h4 className="font-medium text-sm truncate">
                                {notification.title}
                              </h4>
                              {getSeverityBadge(notification.severity)}
                              {!notification.isRead && (
                                <div className="w-2 h-2 rounded-full bg-primary animate-pulse" />
                              )}
                            </div>
                            
                            <p className="text-xs text-muted-foreground mb-3">
                              {notification.message}
                            </p>
                            
                            {!notification.patientId && notification.rfidUid && (
                              <div className="mb-3">
                                <Button
                                  variant="outline"
                                  size="sm"
                                  onClick={() => handleOpenAssignmentModal(notification.rfidUid!)}
                                  className="flex items-center gap-1.5 text-xs bg-indigo-500/10 border-indigo-500/20 text-indigo-400 hover:bg-indigo-500/20 hover:text-indigo-300"
                                >
                                  <Users className="h-3 w-3" />
                                  Assign Patient
                                </Button>
                              </div>
                            )}
                            
                            <div className="flex items-center gap-2 text-xs text-muted-foreground mb-2">
                              <Clock className="h-3 w-3" />
                              <span>{notification.timestamp.toLocaleString()}</span>
                              {notification.patientId && (
                                <Badge variant="outline" className="text-xs">
                                  <Users className="h-3 w-3 mr-1" />
                                  {notification.patientId}
                                </Badge>
                              )}
                              {notification.room && (
                                <Badge variant="outline" className="text-xs">
                                  <MapPin className="h-3 w-3 mr-1" />
                                  {notification.room}
                                </Badge>
                              )}
                            </div>

                            {/* Status indicators */}
                            <div className="flex items-center gap-2 text-xs">
                              {notification.acknowledged && (
                                <div className="flex items-center gap-1 text-green-600">
                                  <Check className="h-3 w-3" />
                                  <span>Acknowledged by {notification.acknowledgedBy}</span>
                                </div>
                              )}
                              {notification.resolved && (
                                <div className="flex items-center gap-1 text-blue-600">
                                  <Shield className="h-3 w-3" />
                                  <span>Resolved</span>
                                </div>
                              )}
                            </div>
                          </div>
                        </div>
                        
                        <div className="flex flex-col gap-1 ml-2">
                          {!notification.isRead && (
                            <Button
                              variant="ghost"
                              size="sm"
                              onClick={() => markAsRead(notification.id)}
                              className="h-7 w-7 p-0"
                              title="Mark as read"
                            >
                              <Check className="h-3 w-3" />
                            </Button>
                          )}
                          
                          {!notification.acknowledged && !notification.resolved && (
                            <Button
                              variant="ghost"
                              size="sm"
                              onClick={() => handleAcknowledgeAlert(notification.id)}
                              className="h-7 w-7 p-0"
                              title="Acknowledge"
                              disabled={acknowledgeMutation.isPending}
                            >
                              <Zap className="h-3 w-3" />
                            </Button>
                          )}
                          
                          {notification.acknowledged && !notification.resolved && (
                            <Button
                              variant="ghost"
                              size="sm"
                              onClick={() => resolveAlert(notification.id)}
                              className="h-7 w-7 p-0"
                              title="Resolve"
                              disabled={resolveMutation.isPending}
                            >
                              <Shield className="h-3 w-3" />
                            </Button>
                          )}
                          
                          <Button
                            variant="ghost"
                            size="sm"
                            onClick={() => dismissNotification(notification.id)}
                            className="h-7 w-7 p-0"
                            title="Dismiss"
                          >
                            <X className="h-3 w-3" />
                          </Button>
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </ScrollArea>
          </TabsContent>
        </Tabs>
      </CardContent>

      {/* Audio element for alert sounds */}
      <audio
        ref={audioRef}
        preload="auto"
        src="data:audio/wav;base64,UklGRnoGAABXQVZFZm10IBAAAAABAAEAQB8AAEAfAAABAAgAZGF0YQoGAACBhYqFbF1fdJivrJBhNjVgodDbq2EcBj+a2/LDciUFLIHO8tiJNwgZaLvt559NEAxQp+PwtmMcBjiR1/LMeSwFJHfH8N2QQAoUXrTp66hVFApGn+DyvmwhBSuBzvLZiTYIG2m98OScTgwOUarm7blmGgU7k9n1unEiBC13yO/eizEIHWq+8+OWT"
      />

      <Dialog open={isAssigningModalOpen} onOpenChange={setIsAssigningModalOpen}>
        <DialogContent className="sm:max-w-[425px] bg-slate-900 border-slate-800 text-slate-100">
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2 text-xl font-bold text-slate-100">
              <Users className="h-5 w-5 text-indigo-400" />
              <span>Register &amp; Assign Patient</span>
            </DialogTitle>
            <DialogDescription className="text-slate-400">
              Complete the profile below to pair the scanned RFID tag with a patient.
            </DialogDescription>
          </DialogHeader>
          <form onSubmit={handleAssignSubmit} className="space-y-4 py-4">
            <div className="space-y-2">
              <label className="text-sm font-medium text-slate-300">RFID Tag UID</label>
              <Input
                value={assigningRfid || ""}
                disabled
                className="bg-slate-800/50 border-slate-700 text-slate-300 cursor-not-allowed"
              />
            </div>
            <div className="grid grid-cols-2 gap-4">
              <div className="space-y-2">
                <label className="text-sm font-medium text-slate-300">Patient Name</label>
                <Input
                  required
                  placeholder="e.g. John Doe"
                  value={patientName}
                  onChange={(e) => setPatientName(e.target.value)}
                  className="bg-slate-800 border-slate-700 text-slate-100 focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500"
                />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium text-slate-300">Patient ID</label>
                <Input
                  required
                  placeholder="e.g. P123"
                  value={patientId}
                  onChange={(e) => setPatientId(e.target.value)}
                  className="bg-slate-800 border-slate-700 text-slate-100 focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500"
                />
              </div>
            </div>
            <div className="grid grid-cols-2 gap-4">
              <div className="space-y-2">
                <label className="text-sm font-medium text-slate-300">BLE Minor ID</label>
                <Input
                  required
                  type="number"
                  placeholder="e.g. 5001"
                  value={bleMinor}
                  onChange={(e) => setBleMinor(e.target.value)}
                  className="bg-slate-800 border-slate-700 text-slate-100 focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500"
                />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium text-slate-300">Assigned Room</label>
                <Select required value={designatedRoom} onValueChange={setDesignatedRoom}>
                  <SelectTrigger className="bg-slate-800 border-slate-700 text-slate-100">
                    <SelectValue placeholder="Select room" />
                  </SelectTrigger>
                  <SelectContent className="bg-slate-800 border-slate-700 text-slate-100">
                    <SelectItem value="R01">Patient Room 1 (R01)</SelectItem>
                    <SelectItem value="R02">Patient Room 2 (R02)</SelectItem>
                    <SelectItem value="R03">Patient Room 3 (R03)</SelectItem>
                    <SelectItem value="NURSE">Nurses Station (NURSE)</SelectItem>
                    <SelectItem value="REST">Restricted Area (REST)</SelectItem>
                    <SelectItem value="ISO">Isolation Room (ISO)</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>
            <DialogFooter className="pt-4 border-t border-slate-800">
              <Button
                type="button"
                variant="ghost"
                onClick={() => setIsAssigningModalOpen(false)}
                className="text-slate-400 hover:text-slate-200 hover:bg-slate-800"
              >
                Cancel
              </Button>
              <Button
                type="submit"
                className="bg-indigo-600 hover:bg-indigo-500 text-white shadow-lg shadow-indigo-500/20"
              >
                Register &amp; Assign
              </Button>
            </DialogFooter>
          </form>
        </DialogContent>
      </Dialog>
    </Card>
  );
}

function cn(...classes: (string | undefined | boolean)[]): string {
  return classes.filter(Boolean).join(' ');
}