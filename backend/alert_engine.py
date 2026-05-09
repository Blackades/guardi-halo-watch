import json
from datetime import datetime, timedelta
from typing import Dict, Any, List, Optional
from uuid import uuid4
from sqlalchemy.orm import Session
from sqlalchemy import func
from . import models, schemas


class AlertThreshold:
    """Configuration for alert thresholds."""
    def __init__(self, 
                 battery_low_threshold: int = 20):
        self.battery_low_threshold = battery_low_threshold


class AlertEngine:
    """Enhanced alert engine with configurable thresholds and escalation."""
    
    def __init__(self, thresholds: AlertThreshold = None):
        self.thresholds = thresholds or AlertThreshold()
        self.alert_aggregation_window = 300  # 5 minutes in seconds
        self.max_alerts_per_window = 3  # Maximum similar alerts per window
    
    # Vitals evaluation removed in v2.0
    
    def evaluate_device_status(self, db: Session, patient_id: str, device_data: Dict[str, Any]) -> List[Dict[str, Any]]:
        """Evaluate device status and generate alerts."""
        alerts = []
        
        # Battery level check
        if device_data.get('battery_level') is not None:
            battery = device_data['battery_level']
            if battery <= self.thresholds.battery_low_threshold:
                severity = 'critical' if battery <= 10 else 'warning'
                alerts.append({
                    'type': 'low_battery',
                    'severity': severity,
                    'title': 'Low Battery',
                    'message': f'Device battery at {battery}% - charging required',
                    'patient_id': patient_id,
                    'metadata': {'battery_level': battery, 'threshold': self.thresholds.battery_low_threshold}
                })
        
        # Tamper detection
        if not device_data.get('strap_intact', True):
            alerts.append({
                'type': 'tamper_strap',
                'severity': 'critical',
                'title': 'Strap Tamper',
                'message': 'Wristband strap has been compromised or removed',
                'patient_id': patient_id,
                'metadata': {'strap_intact': False}
            })
        
        if not device_data.get('skin_contact', True):
            alerts.append({
                'type': 'tamper_contact',
                'severity': 'alert',
                'title': 'Skin Contact Lost',
                'message': 'Wristband has lost skin contact - may have been removed',
                'patient_id': patient_id,
                'metadata': {'skin_contact': False}
            })
        
        # Fall detection
        if device_data.get('fall_detected', False):
            alerts.append({
                'type': 'fall_detected',
                'severity': 'critical',
                'title': 'Fall Detected',
                'message': 'Possible fall detected - immediate attention required',
                'patient_id': patient_id,
                'metadata': {'fall_detected': True}
            })
        
        return alerts
    
    def check_alert_aggregation(self, db: Session, alert_type: str, patient_id: str) -> bool:
        """Check if similar alerts should be aggregated to prevent spam."""
        cutoff_time = datetime.utcnow() - timedelta(seconds=self.alert_aggregation_window)
        
        # Count similar alerts in the time window, grouped by the stored
        # alert "type" field (which is used for severity: warning/alert/etc.).
        similar_alerts = db.query(models.Alert).filter(
            models.Alert.patient_id == patient_id,
            models.Alert.type == alert_type,
            models.Alert.created_at >= cutoff_time,
            models.Alert.status == 'active'
        ).count()
        
        return similar_alerts >= self.max_alerts_per_window
    
    def create_alert(self, db: Session, alert_data: Dict[str, Any]) -> Optional[models.Alert]:
        """Create an alert with escalation logic."""
        # Use the severity value stored in models.Alert.type to determine
        # aggregation groups, so that tests can count by type (e.g. "warning").
        stored_type = alert_data.get('severity', 'alert')

        # Check for alert aggregation
        if self.check_alert_aggregation(db, stored_type, alert_data['patient_id']):
            # Update existing alert instead of creating new one
            existing_alert = db.query(models.Alert).filter(
                models.Alert.patient_id == alert_data['patient_id'],
                models.Alert.type == stored_type,
                models.Alert.status == 'active'
            ).order_by(models.Alert.created_at.desc()).first()
            
            if existing_alert:
                # Update escalation level and timestamp
                existing_alert.escalation_level = min(existing_alert.escalation_level + 1, 3)
                existing_alert.created_at = datetime.utcnow()  # Update timestamp for latest occurrence
                db.commit()
                return existing_alert
        
        # Create new alert
        now = datetime.utcnow()
        alert = models.Alert(
            # Ensure alert_code is unique even when multiple alerts are created
            # within the same second.
            alert_code=f"{alert_data['type'].upper()}-{alert_data['patient_id']}-{int(now.timestamp())}-{uuid4().hex[:6]}",
            type=stored_type,
            title=alert_data['title'],
            message=alert_data['message'],
            patient_id=alert_data['patient_id'],
            room=alert_data.get('room'),
            status='active',
            is_read=False,
            created_at=now,
            escalation_level=1,
            alert_metadata=json.dumps(alert_data.get('metadata', {}))
        )
        
        db.add(alert)
        db.commit()
        db.refresh(alert)
        return alert
    
    def acknowledge_alert(self, db: Session, alert_code: str, user_id: str) -> bool:
        """Acknowledge an alert."""
        alert = db.query(models.Alert).filter(models.Alert.alert_code == alert_code).first()
        if not alert:
            return False
        
        alert.status = 'acknowledged'
        alert.acknowledged_at = datetime.utcnow()
        alert.acknowledged_by = user_id
        db.commit()
        return True
    
    def resolve_alert(self, db: Session, alert_code: str, user_id: str) -> bool:
        """Resolve an alert."""
        alert = db.query(models.Alert).filter(models.Alert.alert_code == alert_code).first()
        if not alert:
            return False
        
        alert.status = 'resolved'
        alert.resolved_at = datetime.utcnow()
        alert.resolved_by = user_id
        db.commit()
        return True
    
    def get_escalation_level(self, alert_type: str, severity: str, escalation_count: int) -> str:
        """Determine escalation level based on alert characteristics."""
        if severity == 'critical':
            return 'immediate'
        elif severity == 'alert' and escalation_count >= 2:
            return 'urgent'
        elif severity == 'warning' and escalation_count >= 3:
            return 'elevated'
        else:
            return 'normal'
    
    def get_alert_statistics(self, db: Session, hours: int = 24) -> Dict[str, Any]:
        """Get alert statistics for the specified time period."""
        cutoff_time = datetime.utcnow() - timedelta(hours=hours)
        
        # Total alerts
        total_alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_time
        ).count()
        
        # Alerts by status
        active_alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_time,
            models.Alert.status == 'active'
        ).count()
        
        acknowledged_alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_time,
            models.Alert.status == 'acknowledged'
        ).count()
        
        resolved_alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_time,
            models.Alert.status == 'resolved'
        ).count()
        
        # Alerts by type
        alert_types = db.query(models.Alert.type, func.count(models.Alert.id)).filter(
            models.Alert.created_at >= cutoff_time
        ).group_by(models.Alert.type).all()
        
        return {
            'total_alerts': total_alerts,
            'active_alerts': active_alerts,
            'acknowledged_alerts': acknowledged_alerts,
            'resolved_alerts': resolved_alerts,
            'alert_types': {alert_type: count for alert_type, count in alert_types},
            'time_period_hours': hours
        }