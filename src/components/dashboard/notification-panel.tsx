import { useState, useEffect } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { AlertTriangle, UserX, MapPin, Shield, X, Bell } from "lucide-react";
import { useToast } from "@/hooks/use-toast";

interface Notification {
  id: string;
  type: 'alert' | 'warning' | 'info' | 'success';
  title: string;
  message: string;
  timestamp: Date;
  patientId?: string;
  room?: string;
  isRead: boolean;
}

const mockNotifications: Notification[] = [
  {
    id: 'N001',
    type: 'alert',
    title: 'Tampering Detection',
    message: 'Patient P003 wristband tampered in Room 103',
    timestamp: new Date(Date.now() - 2 * 60 * 1000),
    patientId: 'P003',
    room: 'R103',
    isRead: false
  },
  {
    id: 'N002',
    type: 'warning',
    title: 'Boundary Alert',
    message: 'Patient P004 approaching unauthorized area',
    timestamp: new Date(Date.now() - 8 * 60 * 1000),
    patientId: 'P004',
    room: 'R202',
    isRead: false
  },
  {
    id: 'N003',
    type: 'info',
    title: 'Room Access',
    message: 'Patient P001 entered Room 101',
    timestamp: new Date(Date.now() - 15 * 60 * 1000),
    patientId: 'P001',
    room: 'R101',
    isRead: true
  },
  {
    id: 'N004',
    type: 'success',
    title: 'System Check',
    message: 'All monitoring systems operational',
    timestamp: new Date(Date.now() - 30 * 60 * 1000),
    isRead: true
  }
];

export default function NotificationPanel() {
  const [notifications, setNotifications] = useState<Notification[]>(mockNotifications);
  const { toast } = useToast();

  // Simulate real-time notifications
  useEffect(() => {
    const interval = setInterval(() => {
      const alertTypes = ['alert', 'warning', 'info'] as const;
      const randomType = alertTypes[Math.floor(Math.random() * alertTypes.length)];
      
      if (Math.random() > 0.7) { // 30% chance every 10 seconds
        const newNotification: Notification = {
          id: `N${Date.now()}`,
          type: randomType,
          title: randomType === 'alert' ? 'Security Alert' : 
                 randomType === 'warning' ? 'Patient Movement' : 'System Update',
          message: randomType === 'alert' ? 'Unauthorized access detected' : 
                   randomType === 'warning' ? 'Patient movement anomaly detected' : 'System status update',
          timestamp: new Date(),
          isRead: false
        };

        setNotifications(prev => [newNotification, ...prev].slice(0, 10));

        // Show toast for high-priority alerts
        if (randomType === 'alert') {
          toast({
            variant: "destructive",
            title: newNotification.title,
            description: newNotification.message,
          });
        }
      }
    }, 10000);

    return () => clearInterval(interval);
  }, [toast]);

  const getNotificationIcon = (type: Notification['type']) => {
    switch (type) {
      case 'alert':
        return <AlertTriangle className="h-4 w-4 text-destructive" />;
      case 'warning':
        return <UserX className="h-4 w-4 text-warning" />;
      case 'info':
        return <MapPin className="h-4 w-4 text-info" />;
      case 'success':
        return <Shield className="h-4 w-4 text-success" />;
    }
  };

  const markAsRead = (id: string) => {
    setNotifications(prev =>
      prev.map(n => n.id === id ? { ...n, isRead: true } : n)
    );
  };

  const dismissNotification = (id: string) => {
    setNotifications(prev => prev.filter(n => n.id !== id));
  };

  const unreadCount = notifications.filter(n => !n.isRead).length;

  return (
    <Card className="h-full">
      <CardHeader>
        <CardTitle className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Bell className="h-5 w-5" />
            <span>Live Alerts</span>
          </div>
          {unreadCount > 0 && (
            <Badge variant="destructive" className="animate-pulse">
              {unreadCount} new
            </Badge>
          )}
        </CardTitle>
      </CardHeader>
      <CardContent className="p-0">
        <div className="max-h-80 overflow-y-auto">
          {notifications.length === 0 ? (
            <div className="p-4 text-center text-muted-foreground">
              <Shield className="h-8 w-8 mx-auto mb-2 opacity-50" />
              <p>No active alerts</p>
            </div>
          ) : (
            notifications.map((notification) => (
              <div
                key={notification.id}
                className={cn(
                  "border-b p-4 transition-all duration-200",
                  !notification.isRead && "bg-accent/30 border-l-4 border-l-primary"
                )}
              >
                <div className="flex items-start justify-between">
                  <div className="flex items-start gap-3 flex-1">
                    {getNotificationIcon(notification.type)}
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2 mb-1">
                        <h4 className="font-medium text-sm truncate">
                          {notification.title}
                        </h4>
                        {!notification.isRead && (
                          <div className="w-2 h-2 rounded-full bg-primary animate-pulse" />
                        )}
                      </div>
                      <p className="text-xs text-muted-foreground mb-2">
                        {notification.message}
                      </p>
                      <div className="flex items-center gap-2 text-xs text-muted-foreground">
                        <span>{notification.timestamp.toLocaleTimeString()}</span>
                        {notification.patientId && (
                          <Badge variant="outline" className="text-xs">
                            {notification.patientId}
                          </Badge>
                        )}
                        {notification.room && (
                          <Badge variant="outline" className="text-xs">
                            {notification.room}
                          </Badge>
                        )}
                      </div>
                    </div>
                  </div>
                  <div className="flex items-center gap-1 ml-2">
                    {!notification.isRead && (
                      <Button
                        variant="ghost"
                        size="sm"
                        onClick={() => markAsRead(notification.id)}
                        className="h-6 w-6 p-0"
                      >
                        <span className="sr-only">Mark as read</span>
                        ✓
                      </Button>
                    )}
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => dismissNotification(notification.id)}
                      className="h-6 w-6 p-0"
                    >
                      <X className="h-3 w-3" />
                    </Button>
                  </div>
                </div>
              </div>
            ))
          )}
        </div>
      </CardContent>
    </Card>
  );
}

function cn(...classes: (string | undefined | boolean)[]): string {
  return classes.filter(Boolean).join(' ');
}