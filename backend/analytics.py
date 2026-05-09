"""
Data Analytics and Reporting System for Halo Watch Patient Monitoring.

This module provides comprehensive analytics capabilities including:
- Patient movement pattern analysis
- Alert trend analysis and false positive detection
- System performance monitoring
- Automated report generation
"""

import json
import numpy as np
from datetime import datetime, timedelta
from typing import Dict, Any, List, Optional, Tuple
from sqlalchemy.orm import Session
from sqlalchemy import func, and_, or_, desc, asc
from dataclasses import dataclass
from collections import defaultdict
import statistics

from . import models


@dataclass
class MovementPattern:
    """Data class for patient movement patterns."""
    patient_id: str
    total_movements: int
    unique_zones: int
    average_dwell_time: float  # minutes
    movement_frequency: float  # movements per hour
    most_visited_zone: str
    activity_score: float  # 0-100 scale
    abnormal_patterns: List[str]


@dataclass
class ZoneDwellTime:
    """Data class for zone dwell time analysis."""
    zone: str
    total_time_minutes: float
    visit_count: int
    average_dwell_minutes: float
    longest_stay_minutes: float
    shortest_stay_minutes: float


class MovementAnalytics:
    """Patient movement pattern analysis algorithms."""
    
    def __init__(self):
        self.normal_activity_threshold = 10  # movements per day
        self.abnormal_dwell_threshold = 240  # 4 hours in one zone
        self.rapid_movement_threshold = 5  # movements in 5 minutes
    
    def analyze_patient_movement_patterns(
        self, 
        db: Session, 
        patient_id: str, 
        days: int = 7
    ) -> MovementPattern:
        """
        Analyze patient movement patterns over specified time period.
        
        Args:
            db: Database session
            patient_id: Patient identifier
            days: Number of days to analyze
            
        Returns:
            MovementPattern object with analysis results
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        # Get location history for the patient
        locations = db.query(models.LocationHistory).filter(
            models.LocationHistory.patient_id == patient_id,
            models.LocationHistory.timestamp >= cutoff_date
        ).order_by(models.LocationHistory.timestamp.asc()).all()
        
        if not locations:
            return MovementPattern(
                patient_id=patient_id,
                total_movements=0,
                unique_zones=0,
                average_dwell_time=0.0,
                movement_frequency=0.0,
                most_visited_zone="",
                activity_score=0.0,
                abnormal_patterns=[]
            )
        
        # Calculate basic movement metrics
        total_movements = len(locations)
        unique_zones = len(set(loc.zone for loc in locations))
        
        # Calculate zone visit frequency
        zone_visits = defaultdict(int)
        for loc in locations:
            zone_visits[loc.zone] += 1
        
        most_visited_zone = max(zone_visits.items(), key=lambda x: x[1])[0]
        
        # Calculate dwell times
        dwell_times = self._calculate_zone_dwell_times(locations)
        average_dwell_time = statistics.mean(dwell_times) if dwell_times else 0.0
        
        # Calculate movement frequency (movements per hour)
        time_span_hours = (locations[-1].timestamp - locations[0].timestamp).total_seconds() / 3600
        movement_frequency = total_movements / max(time_span_hours, 1)
        
        # Calculate activity score (0-100)
        activity_score = self._calculate_activity_score(
            total_movements, unique_zones, movement_frequency, days
        )
        
        # Detect abnormal patterns
        abnormal_patterns = self._detect_abnormal_movement_patterns(
            locations, dwell_times, movement_frequency
        )
        
        return MovementPattern(
            patient_id=patient_id,
            total_movements=total_movements,
            unique_zones=unique_zones,
            average_dwell_time=average_dwell_time,
            movement_frequency=movement_frequency,
            most_visited_zone=most_visited_zone,
            activity_score=activity_score,
            abnormal_patterns=abnormal_patterns
        )
    
    def calculate_zone_dwell_analysis(
        self, 
        db: Session, 
        patient_id: str, 
        days: int = 7
    ) -> List[ZoneDwellTime]:
        """
        Calculate detailed zone dwell time analysis.
        
        Args:
            db: Database session
            patient_id: Patient identifier
            days: Number of days to analyze
            
        Returns:
            List of ZoneDwellTime objects
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        locations = db.query(models.LocationHistory).filter(
            models.LocationHistory.patient_id == patient_id,
            models.LocationHistory.timestamp >= cutoff_date
        ).order_by(models.LocationHistory.timestamp.asc()).all()
        
        if not locations:
            return []
        
        # Group locations by zone and calculate dwell times
        zone_sessions = defaultdict(list)
        current_zone = None
        session_start = None
        
        for loc in locations:
            if loc.zone != current_zone:
                # End previous session
                if current_zone and session_start:
                    session_duration = (loc.timestamp - session_start).total_seconds() / 60
                    zone_sessions[current_zone].append(session_duration)
                
                # Start new session
                current_zone = loc.zone
                session_start = loc.timestamp
        
        # Handle the last session
        if current_zone and session_start:
            session_duration = (datetime.utcnow() - session_start).total_seconds() / 60
            zone_sessions[current_zone].append(session_duration)
        
        # Calculate statistics for each zone
        zone_analysis = []
        for zone, durations in zone_sessions.items():
            if durations:
                zone_analysis.append(ZoneDwellTime(
                    zone=zone,
                    total_time_minutes=sum(durations),
                    visit_count=len(durations),
                    average_dwell_minutes=statistics.mean(durations),
                    longest_stay_minutes=max(durations),
                    shortest_stay_minutes=min(durations)
                ))
        
        return sorted(zone_analysis, key=lambda x: x.total_time_minutes, reverse=True)
    
    def detect_abnormal_movement_patterns(
        self, 
        db: Session, 
        patient_id: str, 
        hours: int = 24
    ) -> List[Dict[str, Any]]:
        """
        Detect abnormal movement patterns that may indicate health issues.
        
        Args:
            db: Database session
            patient_id: Patient identifier
            hours: Number of hours to analyze
            
        Returns:
            List of abnormal pattern detections
        """
        cutoff_time = datetime.utcnow() - timedelta(hours=hours)
        
        locations = db.query(models.LocationHistory).filter(
            models.LocationHistory.patient_id == patient_id,
            models.LocationHistory.timestamp >= cutoff_time
        ).order_by(models.LocationHistory.timestamp.asc()).all()
        
        abnormal_patterns = []
        
        if not locations:
            abnormal_patterns.append({
                'type': 'no_movement',
                'severity': 'warning',
                'description': f'No movement detected in the last {hours} hours',
                'timestamp': datetime.utcnow(),
                'details': {'hours_without_movement': hours}
            })
            return abnormal_patterns
        
        # Check for excessive movement (possible agitation)
        if len(locations) > self.rapid_movement_threshold * (hours / 24):
            abnormal_patterns.append({
                'type': 'excessive_movement',
                'severity': 'warning',
                'description': f'Unusually high movement activity: {len(locations)} movements in {hours} hours',
                'timestamp': datetime.utcnow(),
                'details': {'movement_count': len(locations), 'threshold': self.rapid_movement_threshold}
            })
        
        # Check for prolonged stay in one zone
        dwell_times = self._calculate_zone_dwell_times(locations)
        if dwell_times and max(dwell_times) > self.abnormal_dwell_threshold:
            abnormal_patterns.append({
                'type': 'prolonged_stay',
                'severity': 'alert',
                'description': f'Patient stayed in one zone for {max(dwell_times):.1f} minutes',
                'timestamp': datetime.utcnow(),
                'details': {'max_dwell_minutes': max(dwell_times), 'threshold': self.abnormal_dwell_threshold}
            })
        
        # Check for rapid zone changes (possible confusion)
        rapid_changes = self._detect_rapid_zone_changes(locations)
        if rapid_changes:
            abnormal_patterns.append({
                'type': 'rapid_zone_changes',
                'severity': 'warning',
                'description': f'Rapid zone changes detected: {rapid_changes} instances',
                'timestamp': datetime.utcnow(),
                'details': {'rapid_change_count': rapid_changes}
            })
        
        return abnormal_patterns
    
    def calculate_patient_activity_score(
        self, 
        db: Session, 
        patient_id: str, 
        days: int = 7
    ) -> Dict[str, Any]:
        """
        Calculate comprehensive patient activity score and trends.
        
        Args:
            db: Database session
            patient_id: Patient identifier
            days: Number of days to analyze
            
        Returns:
            Dictionary with activity score and trend analysis
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        # Get daily movement counts
        daily_movements = db.query(
            func.date(models.LocationHistory.timestamp).label('date'),
            func.count(models.LocationHistory.id).label('movement_count')
        ).filter(
            models.LocationHistory.patient_id == patient_id,
            models.LocationHistory.timestamp >= cutoff_date
        ).group_by(func.date(models.LocationHistory.timestamp)).all()
        
        if not daily_movements:
            return {
                'activity_score': 0,
                'trend': 'no_data',
                'daily_average': 0,
                'consistency_score': 0,
                'recommendations': ['No movement data available']
            }
        
        # Calculate metrics
        movement_counts = [day.movement_count for day in daily_movements]
        daily_average = statistics.mean(movement_counts)
        
        # Activity score based on movement frequency and consistency
        activity_score = min(100, (daily_average / self.normal_activity_threshold) * 100)
        
        # Consistency score (lower standard deviation = higher consistency)
        if len(movement_counts) > 1:
            std_dev = statistics.stdev(movement_counts)
            consistency_score = max(0, 100 - (std_dev / daily_average * 100))
        else:
            consistency_score = 100
        
        # Trend analysis
        if len(movement_counts) >= 3:
            recent_avg = statistics.mean(movement_counts[-3:])
            earlier_avg = statistics.mean(movement_counts[:-3]) if len(movement_counts) > 3 else daily_average
            
            if recent_avg > earlier_avg * 1.2:
                trend = 'increasing'
            elif recent_avg < earlier_avg * 0.8:
                trend = 'decreasing'
            else:
                trend = 'stable'
        else:
            trend = 'insufficient_data'
        
        # Generate recommendations
        recommendations = []
        if activity_score < 50:
            recommendations.append('Consider encouraging more movement and activity')
        if consistency_score < 60:
            recommendations.append('Movement patterns are irregular - monitor for health changes')
        if trend == 'decreasing':
            recommendations.append('Activity levels are declining - may need medical attention')
        
        return {
            'activity_score': round(activity_score, 1),
            'trend': trend,
            'daily_average': round(daily_average, 1),
            'consistency_score': round(consistency_score, 1),
            'recommendations': recommendations,
            'daily_data': [{'date': day.date.isoformat(), 'movements': day.movement_count} for day in daily_movements]
        }
    
    def _calculate_zone_dwell_times(self, locations: List[models.LocationHistory]) -> List[float]:
        """Calculate dwell times in minutes for each zone visit."""
        if len(locations) < 2:
            return []
        
        dwell_times = []
        current_zone = locations[0].zone
        zone_start = locations[0].timestamp
        
        for i in range(1, len(locations)):
            if locations[i].zone != current_zone:
                # Calculate dwell time for previous zone
                dwell_minutes = (locations[i].timestamp - zone_start).total_seconds() / 60
                dwell_times.append(dwell_minutes)
                
                # Start tracking new zone
                current_zone = locations[i].zone
                zone_start = locations[i].timestamp
        
        # Handle the last zone
        if locations:
            final_dwell = (datetime.utcnow() - zone_start).total_seconds() / 60
            dwell_times.append(final_dwell)
        
        return dwell_times
    
    def _calculate_activity_score(
        self, 
        total_movements: int, 
        unique_zones: int, 
        movement_frequency: float, 
        days: int
    ) -> float:
        """Calculate activity score based on movement metrics."""
        # Base score from movement frequency
        frequency_score = min(50, (movement_frequency / 2.0) * 50)  # 2 movements/hour = 50 points
        
        # Zone diversity score
        diversity_score = min(25, unique_zones * 5)  # 5 zones = 25 points
        
        # Consistency score (movements per day)
        daily_movements = total_movements / max(days, 1)
        consistency_score = min(25, (daily_movements / self.normal_activity_threshold) * 25)
        
        return frequency_score + diversity_score + consistency_score
    
    def _detect_abnormal_movement_patterns(
        self, 
        locations: List[models.LocationHistory], 
        dwell_times: List[float], 
        movement_frequency: float
    ) -> List[str]:
        """Detect abnormal movement patterns."""
        patterns = []
        
        # Check for excessive movement
        if movement_frequency > 10:  # More than 10 movements per hour
            patterns.append('excessive_movement')
        
        # Check for minimal movement
        if movement_frequency < 0.5:  # Less than 0.5 movements per hour
            patterns.append('minimal_movement')
        
        # Check for prolonged stays
        if dwell_times and max(dwell_times) > self.abnormal_dwell_threshold:
            patterns.append('prolonged_stay')
        
        # Check for rapid zone changes
        if self._detect_rapid_zone_changes(locations) > 0:
            patterns.append('rapid_zone_changes')
        
        return patterns
    
    def _detect_rapid_zone_changes(self, locations: List[models.LocationHistory]) -> int:
        """Detect rapid zone changes that might indicate confusion or agitation."""
        if len(locations) < 3:
            return 0
        
        rapid_changes = 0
        window_size = 5  # 5-minute window
        
        for i in range(len(locations) - 2):
            # Check if patient changed zones multiple times within window
            window_end = locations[i].timestamp + timedelta(minutes=window_size)
            changes_in_window = 0
            current_zone = locations[i].zone
            
            j = i + 1
            while j < len(locations) and locations[j].timestamp <= window_end:
                if locations[j].zone != current_zone:
                    changes_in_window += 1
                    current_zone = locations[j].zone
                j += 1
            
            if changes_in_window >= 3:  # 3 or more zone changes in 5 minutes
                rapid_changes += 1
        
        return rapid_changes


class AlertAnalytics:
    """Alert trend analysis and false positive detection."""
    
    def __init__(self):
        self.false_positive_threshold = 0.7  # 70% resolution rate indicates potential false positives
        self.alert_frequency_threshold = 10  # alerts per hour threshold
        self.escalation_effectiveness_threshold = 0.8  # 80% effectiveness threshold
    
    def analyze_alert_trends(
        self, 
        db: Session, 
        days: int = 30
    ) -> Dict[str, Any]:
        """
        Analyze alert trends and patterns over specified time period.
        
        Args:
            db: Database session
            days: Number of days to analyze
            
        Returns:
            Dictionary with comprehensive alert trend analysis
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        # Get all alerts in the time period
        alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_date
        ).all()
        
        if not alerts:
            return {
                'total_alerts': 0,
                'trends': {},
                'patterns': {},
                'recommendations': ['No alert data available for analysis']
            }
        
        # Basic statistics
        total_alerts = len(alerts)
        alert_types = defaultdict(int)
        alert_severities = defaultdict(int)
        hourly_distribution = defaultdict(int)
        daily_counts = defaultdict(int)
        
        for alert in alerts:
            alert_types[alert.type] += 1
            alert_severities[alert.type] += 1
            hourly_distribution[alert.created_at.hour] += 1
            daily_counts[alert.created_at.date()] += 1
        
        # Calculate trends
        daily_values = list(daily_counts.values())
        if len(daily_values) >= 7:
            recent_week = statistics.mean(daily_values[-7:])
            previous_week = statistics.mean(daily_values[-14:-7]) if len(daily_values) >= 14 else recent_week
            
            if recent_week > previous_week * 1.2:
                trend = 'increasing'
            elif recent_week < previous_week * 0.8:
                trend = 'decreasing'
            else:
                trend = 'stable'
        else:
            trend = 'insufficient_data'
        
        # Peak hours analysis
        peak_hour = max(hourly_distribution.items(), key=lambda x: x[1])[0]
        
        # Alert effectiveness analysis
        effectiveness = self._calculate_alert_effectiveness(alerts)
        
        return {
            'total_alerts': total_alerts,
            'daily_average': round(total_alerts / days, 1),
            'trend': trend,
            'alert_types': dict(alert_types),
            'alert_severities': dict(alert_severities),
            'peak_hour': peak_hour,
            'effectiveness': effectiveness,
            'hourly_distribution': dict(hourly_distribution),
            'recommendations': self._generate_alert_recommendations(alerts, effectiveness)
        }
    
    def detect_false_positives(
        self, 
        db: Session, 
        alert_type: str = None, 
        days: int = 30
    ) -> Dict[str, Any]:
        """
        Detect potential false positive alerts using machine learning-like analysis.
        
        Args:
            db: Database session
            alert_type: Specific alert type to analyze (optional)
            days: Number of days to analyze
            
        Returns:
            Dictionary with false positive analysis
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        query = db.query(models.Alert).filter(models.Alert.created_at >= cutoff_date)
        if alert_type:
            query = query.filter(models.Alert.type == alert_type)
        
        alerts = query.all()
        
        if not alerts:
            return {
                'false_positive_indicators': [],
                'confidence_score': 0,
                'recommendations': ['Insufficient data for false positive analysis']
            }
        
        false_positive_indicators = []
        
        # Analyze resolution patterns
        resolution_analysis = self._analyze_resolution_patterns(alerts)
        if resolution_analysis['quick_resolution_rate'] > self.false_positive_threshold:
            false_positive_indicators.append({
                'type': 'quick_resolution',
                'description': f'{resolution_analysis["quick_resolution_rate"]:.1%} of alerts resolved within 5 minutes',
                'severity': 'high' if resolution_analysis['quick_resolution_rate'] > 0.8 else 'medium'
            })
        
        # Analyze escalation patterns
        escalation_analysis = self._analyze_escalation_patterns(alerts)
        if escalation_analysis['low_escalation_rate'] > 0.7:
            false_positive_indicators.append({
                'type': 'low_escalation',
                'description': f'{escalation_analysis["low_escalation_rate"]:.1%} of alerts never escalated',
                'severity': 'medium'
            })
        
        # Analyze temporal clustering
        clustering_analysis = self._analyze_temporal_clustering(alerts)
        if clustering_analysis['cluster_rate'] > 0.6:
            false_positive_indicators.append({
                'type': 'temporal_clustering',
                'description': f'{clustering_analysis["cluster_rate"]:.1%} of alerts occur in clusters',
                'severity': 'high'
            })
        
        # Calculate confidence score
        confidence_score = len(false_positive_indicators) / 3.0  # Normalize to 0-1
        
        return {
            'false_positive_indicators': false_positive_indicators,
            'confidence_score': confidence_score,
            'resolution_analysis': resolution_analysis,
            'escalation_analysis': escalation_analysis,
            'clustering_analysis': clustering_analysis,
            'recommendations': self._generate_false_positive_recommendations(false_positive_indicators)
        }
    
    def analyze_alert_effectiveness(
        self, 
        db: Session, 
        days: int = 30
    ) -> Dict[str, Any]:
        """
        Analyze alert effectiveness and generate optimization suggestions.
        
        Args:
            db: Database session
            days: Number of days to analyze
            
        Returns:
            Dictionary with effectiveness analysis and optimization suggestions
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_date
        ).all()
        
        if not alerts:
            return {
                'effectiveness_score': 0,
                'metrics': {},
                'optimization_suggestions': ['No alert data available for analysis']
            }
        
        # Calculate key effectiveness metrics
        total_alerts = len(alerts)
        acknowledged_alerts = len([a for a in alerts if a.acknowledged_at])
        resolved_alerts = len([a for a in alerts if a.resolved_at])
        
        # Response time analysis
        response_times = []
        resolution_times = []
        
        for alert in alerts:
            if alert.acknowledged_at:
                response_time = (alert.acknowledged_at - alert.created_at).total_seconds() / 60
                response_times.append(response_time)
            
            if alert.resolved_at:
                resolution_time = (alert.resolved_at - alert.created_at).total_seconds() / 60
                resolution_times.append(resolution_time)
        
        # Calculate effectiveness score
        acknowledgment_rate = acknowledged_alerts / total_alerts if total_alerts > 0 else 0
        resolution_rate = resolved_alerts / total_alerts if total_alerts > 0 else 0
        avg_response_time = statistics.mean(response_times) if response_times else 0
        avg_resolution_time = statistics.mean(resolution_times) if resolution_times else 0
        
        # Effectiveness score (0-100)
        effectiveness_score = (
            acknowledgment_rate * 30 +  # 30% weight for acknowledgment
            resolution_rate * 40 +      # 40% weight for resolution
            (1 - min(avg_response_time / 60, 1)) * 20 +  # 20% weight for response time (faster = better)
            (1 - min(avg_resolution_time / 240, 1)) * 10  # 10% weight for resolution time
        ) * 100
        
        metrics = {
            'acknowledgment_rate': acknowledgment_rate,
            'resolution_rate': resolution_rate,
            'average_response_time_minutes': avg_response_time,
            'average_resolution_time_minutes': avg_resolution_time,
            'total_alerts': total_alerts
        }
        
        # Generate optimization suggestions
        optimization_suggestions = []
        
        if acknowledgment_rate < 0.8:
            optimization_suggestions.append('Improve alert visibility and notification methods')
        
        if resolution_rate < 0.7:
            optimization_suggestions.append('Review alert resolution procedures and staff training')
        
        if avg_response_time > 30:  # 30 minutes
            optimization_suggestions.append('Implement faster alert notification systems')
        
        if avg_resolution_time > 120:  # 2 hours
            optimization_suggestions.append('Streamline alert resolution workflows')
        
        return {
            'effectiveness_score': round(effectiveness_score, 1),
            'metrics': metrics,
            'optimization_suggestions': optimization_suggestions
        }
    
    def analyze_escalation_patterns(
        self, 
        db: Session, 
        days: int = 30
    ) -> Dict[str, Any]:
        """
        Analyze alert escalation patterns and effectiveness.
        
        Args:
            db: Database session
            days: Number of days to analyze
            
        Returns:
            Dictionary with escalation pattern analysis
        """
        cutoff_date = datetime.utcnow() - timedelta(days=days)
        
        alerts = db.query(models.Alert).filter(
            models.Alert.created_at >= cutoff_date
        ).all()
        
        if not alerts:
            return {
                'escalation_patterns': {},
                'recommendations': ['No alert data available for escalation analysis']
            }
        
        # Analyze escalation levels
        escalation_levels = defaultdict(int)
        escalation_by_type = defaultdict(lambda: defaultdict(int))
        
        for alert in alerts:
            level = alert.escalation_level or 1
            escalation_levels[level] += 1
            escalation_by_type[alert.type][level] += 1
        
        # Calculate escalation effectiveness
        high_escalation_resolved = 0
        high_escalation_total = 0
        
        for alert in alerts:
            if (alert.escalation_level or 1) >= 2:
                high_escalation_total += 1
                if alert.resolved_at:
                    high_escalation_resolved += 1
        
        escalation_effectiveness = (
            high_escalation_resolved / high_escalation_total 
            if high_escalation_total > 0 else 0
        )
        
        return {
            'escalation_levels': dict(escalation_levels),
            'escalation_by_type': {k: dict(v) for k, v in escalation_by_type.items()},
            'escalation_effectiveness': escalation_effectiveness,
            'recommendations': self._generate_escalation_recommendations(escalation_effectiveness)
        }
    
    def _calculate_alert_effectiveness(self, alerts: List[models.Alert]) -> Dict[str, float]:
        """Calculate alert effectiveness metrics."""
        if not alerts:
            return {'overall_effectiveness': 0.0}
        
        total_alerts = len(alerts)
        acknowledged = len([a for a in alerts if a.acknowledged_at])
        resolved = len([a for a in alerts if a.resolved_at])
        
        # Calculate response times
        response_times = []
        for alert in alerts:
            if alert.acknowledged_at:
                response_time = (alert.acknowledged_at - alert.created_at).total_seconds() / 60
                response_times.append(response_time)
        
        avg_response_time = statistics.mean(response_times) if response_times else 0
        
        return {
            'acknowledgment_rate': acknowledged / total_alerts,
            'resolution_rate': resolved / total_alerts,
            'average_response_time': avg_response_time,
            'overall_effectiveness': (acknowledged + resolved) / (total_alerts * 2)
        }
    
    def _analyze_resolution_patterns(self, alerts: List[models.Alert]) -> Dict[str, Any]:
        """Analyze alert resolution patterns for false positive detection."""
        if not alerts:
            return {'quick_resolution_rate': 0}
        
        quick_resolutions = 0
        total_resolved = 0
        
        for alert in alerts:
            if alert.resolved_at:
                total_resolved += 1
                resolution_time = (alert.resolved_at - alert.created_at).total_seconds() / 60
                if resolution_time <= 5:  # Resolved within 5 minutes
                    quick_resolutions += 1
        
        return {
            'quick_resolution_rate': quick_resolutions / total_resolved if total_resolved > 0 else 0,
            'total_resolved': total_resolved,
            'quick_resolutions': quick_resolutions
        }
    
    def _analyze_escalation_patterns(self, alerts: List[models.Alert]) -> Dict[str, Any]:
        """Analyze escalation patterns for false positive detection."""
        if not alerts:
            return {'low_escalation_rate': 0}
        
        low_escalation = len([a for a in alerts if (a.escalation_level or 1) == 1])
        
        return {
            'low_escalation_rate': low_escalation / len(alerts),
            'total_alerts': len(alerts),
            'low_escalation_count': low_escalation
        }
    
    def _analyze_temporal_clustering(self, alerts: List[models.Alert]) -> Dict[str, Any]:
        """Analyze temporal clustering of alerts."""
        if len(alerts) < 2:
            return {'cluster_rate': 0}
        
        # Sort alerts by timestamp
        sorted_alerts = sorted(alerts, key=lambda x: x.created_at)
        
        clustered_alerts = 0
        cluster_window = timedelta(minutes=10)  # 10-minute window
        
        for i in range(len(sorted_alerts) - 1):
            time_diff = sorted_alerts[i + 1].created_at - sorted_alerts[i].created_at
            if time_diff <= cluster_window:
                clustered_alerts += 1
        
        return {
            'cluster_rate': clustered_alerts / len(alerts),
            'clustered_count': clustered_alerts,
            'total_alerts': len(alerts)
        }
    
    def _generate_alert_recommendations(
        self, 
        alerts: List[models.Alert], 
        effectiveness: Dict[str, float]
    ) -> List[str]:
        """Generate recommendations based on alert analysis."""
        recommendations = []
        
        if effectiveness['acknowledgment_rate'] < 0.8:
            recommendations.append('Improve alert notification methods to increase acknowledgment rate')
        
        if effectiveness['resolution_rate'] < 0.7:
            recommendations.append('Review alert resolution procedures and staff training')
        
        if effectiveness['average_response_time'] > 30:
            recommendations.append('Implement faster alert notification systems')
        
        # Analyze alert types
        alert_types = defaultdict(int)
        for alert in alerts:
            alert_types[alert.type] += 1
        
        if alert_types:
            most_common = max(alert_types.items(), key=lambda x: x[1])
            if most_common[1] > len(alerts) * 0.4:  # More than 40% of alerts
                recommendations.append(f'High frequency of {most_common[0]} alerts - review thresholds')
        
        return recommendations
    
    def _generate_false_positive_recommendations(
        self, 
        indicators: List[Dict[str, Any]]
    ) -> List[str]:
        """Generate recommendations for reducing false positives."""
        recommendations = []
        
        for indicator in indicators:
            if indicator['type'] == 'quick_resolution':
                recommendations.append('Review alert thresholds - many alerts resolved quickly')
            elif indicator['type'] == 'low_escalation':
                recommendations.append('Consider adjusting escalation criteria')
            elif indicator['type'] == 'temporal_clustering':
                recommendations.append('Implement alert aggregation to reduce clustering')
        
        if not recommendations:
            recommendations.append('Alert patterns appear normal - continue monitoring')
        
        return recommendations
    
    def _generate_escalation_recommendations(self, effectiveness: float) -> List[str]:
        """Generate escalation-specific recommendations."""
        recommendations = []
        
        if effectiveness < self.escalation_effectiveness_threshold:
            recommendations.append('Review escalation procedures - low effectiveness detected')
            recommendations.append('Consider staff training on escalation protocols')
        else:
            recommendations.append('Escalation procedures are working effectively')
        
        return recommendations


class SystemPerformanceAnalytics:
    """System performance monitoring and optimization analysis."""
    
    def __init__(self):
        self.device_offline_threshold = 300  # 5 minutes
        self.network_latency_threshold = 1000  # 1 second in milliseconds
        self.query_performance_threshold = 500  # 500ms
    
    def analyze_device_health(
        self, 
        db: Session, 
        hours: int = 24
    ) -> Dict[str, Any]:
        """
        Analyze device health and predict maintenance needs.
        
        Args:
            db: Database session
            hours: Number of hours to analyze
            
        Returns:
            Dictionary with device health analysis
        """
        cutoff_time = datetime.utcnow() - timedelta(hours=hours)
        
        # Analyze wristband devices (through patient data)
        patients = db.query(models.Patient).all()
        wristband_health = []
        
        for patient in patients:
            if patient.last_activity and patient.last_activity >= cutoff_time:
                device_status = self._analyze_wristband_health(patient)
                wristband_health.append(device_status)
        
        # Analyze receiver nodes
        receivers = db.query(models.Receiver).all()
        receiver_health = []
        
        for receiver in receivers:
            receiver_status = self._analyze_receiver_health(receiver, cutoff_time)
            receiver_health.append(receiver_status)
        
        # Calculate overall health metrics
        total_devices = len(wristband_health) + len(receiver_health)
        healthy_devices = len([d for d in wristband_health + receiver_health if d['status'] == 'healthy'])
        
        health_score = (healthy_devices / total_devices * 100) if total_devices > 0 else 0
        
        # Predictive maintenance recommendations
        maintenance_recommendations = self._generate_maintenance_recommendations(
            wristband_health, receiver_health
        )
        
        return {
            'overall_health_score': round(health_score, 1),
            'total_devices': total_devices,
            'healthy_devices': healthy_devices,
            'wristband_devices': {
                'total': len(wristband_health),
                'healthy': len([d for d in wristband_health if d['status'] == 'healthy']),
                'warning': len([d for d in wristband_health if d['status'] == 'warning']),
                'critical': len([d for d in wristband_health if d['status'] == 'critical']),
                'details': wristband_health
            },
            'receiver_nodes': {
                'total': len(receiver_health),
                'healthy': len([d for d in receiver_health if d['status'] == 'healthy']),
                'warning': len([d for d in receiver_health if d['status'] == 'warning']),
                'critical': len([d for d in receiver_health if d['status'] == 'critical']),
                'details': receiver_health
            },
            'maintenance_recommendations': maintenance_recommendations
        }
    
    def analyze_network_performance(
        self, 
        db: Session, 
        hours: int = 24
    ) -> Dict[str, Any]:
        """
        Analyze network performance and connectivity issues.
        
        Args:
            db: Database session
            hours: Number of hours to analyze
            
        Returns:
            Dictionary with network performance analysis
        """
        cutoff_time = datetime.utcnow() - timedelta(hours=hours)
        
        # Analyze receiver connectivity
        receivers = db.query(models.Receiver).all()
        connectivity_issues = []
        
        for receiver in receivers:
            if not receiver.last_seen or receiver.last_seen < cutoff_time:
                connectivity_issues.append({
                    'device_id': receiver.receiver_id,
                    'device_type': 'receiver',
                    'issue': 'offline',
                    'last_seen': receiver.last_seen.isoformat() if receiver.last_seen else None,
                    'duration_hours': (datetime.utcnow() - receiver.last_seen).total_seconds() / 3600 if receiver.last_seen else None
                })
            elif receiver.wifi_strength and receiver.wifi_strength < -70:  # Weak signal
                connectivity_issues.append({
                    'device_id': receiver.receiver_id,
                    'device_type': 'receiver',
                    'issue': 'weak_signal',
                    'wifi_strength': receiver.wifi_strength,
                    'severity': 'critical' if receiver.wifi_strength < -80 else 'warning'
                })
        
        # Analyze data transmission patterns
        recent_locations = db.query(models.LocationHistory).filter(
            models.LocationHistory.timestamp >= cutoff_time
        ).count()
        
        expected_transmissions = len(receivers) * hours * 12  # Assuming 12 transmissions per hour per receiver
        transmission_rate = (recent_locations / expected_transmissions * 100) if expected_transmissions > 0 else 0
        
        # Network health score
        online_receivers = len([r for r in receivers if r.last_seen and r.last_seen >= cutoff_time])
        network_health_score = (online_receivers / len(receivers) * 100) if receivers else 0
        
        return {
            'network_health_score': round(network_health_score, 1),
            'transmission_rate': round(transmission_rate, 1),
            'connectivity_issues': connectivity_issues,
            'total_receivers': len(receivers),
            'online_receivers': online_receivers,
            'offline_receivers': len(receivers) - online_receivers,
            'recent_transmissions': recent_locations,
            'recommendations': self._generate_network_recommendations(connectivity_issues, transmission_rate)
        }
    
    def analyze_database_performance(
        self, 
        db: Session
    ) -> Dict[str, Any]:
        """
        Analyze database performance and query optimization opportunities.
        
        Args:
            db: Database session
            
        Returns:
            Dictionary with database performance analysis
        """
        import time
        import os
        
        performance_metrics = {}
        
        # Database size analysis
        db_path = "halo_watch.db"
        if os.path.exists(db_path):
            db_size_mb = os.path.getsize(db_path) / (1024 * 1024)
            performance_metrics['database_size_mb'] = round(db_size_mb, 2)
        
        # Table size analysis
        table_counts = {
            'patients': db.query(models.Patient).count(),
            'alerts': db.query(models.Alert).count(),
            'location_history': db.query(models.LocationHistory).count(),
            'access_logs': db.query(models.AccessLog).count(),
            'receivers': db.query(models.Receiver).count(),
            'zones': db.query(models.Zone).count(),
            'users': db.query(models.User).count()
        }
        
        # Query performance testing
        query_performance = {}
        
        # Test common queries
        test_queries = [
            ('patient_list', lambda: db.query(models.Patient).all()),
            ('active_alerts', lambda: db.query(models.Alert).filter(models.Alert.status == 'active').all()),
            ('recent_locations', lambda: db.query(models.LocationHistory).filter(
                models.LocationHistory.timestamp >= datetime.utcnow() - timedelta(hours=1)
            ).all()),
            ('receiver_status', lambda: db.query(models.Receiver).all())
        ]
        
        for query_name, query_func in test_queries:
            start_time = time.time()
            try:
                query_func()
                execution_time = (time.time() - start_time) * 1000  # Convert to milliseconds
                query_performance[query_name] = round(execution_time, 2)
            except Exception as e:
                query_performance[query_name] = f"Error: {str(e)}"
        
        # Identify slow queries
        slow_queries = [
            name for name, time_ms in query_performance.items() 
            if isinstance(time_ms, (int, float)) and time_ms > self.query_performance_threshold
        ]
        
        # Generate optimization recommendations
        optimization_recommendations = []
        
        if db_size_mb > 1000:  # 1GB
            optimization_recommendations.append('Database size is large - consider data archiving')
        
        if table_counts['location_history'] > 100000:
            optimization_recommendations.append('Location history table is large - implement data retention policy')
        
        if slow_queries:
            optimization_recommendations.append(f'Slow queries detected: {", ".join(slow_queries)} - consider indexing')
        
        return {
            'database_size_mb': performance_metrics.get('database_size_mb', 0),
            'table_counts': table_counts,
            'query_performance_ms': query_performance,
            'slow_queries': slow_queries,
            'optimization_recommendations': optimization_recommendations,
            'performance_score': self._calculate_db_performance_score(query_performance, db_size_mb)
        }
    
    def monitor_system_resources(self) -> Dict[str, Any]:
        """
        Monitor system resource usage and performance.
        
        Returns:
            Dictionary with system resource metrics
        """
        try:
            import psutil
            
            # CPU metrics
            cpu_percent = psutil.cpu_percent(interval=1)
            cpu_count = psutil.cpu_count()
            
            # Memory metrics
            memory = psutil.virtual_memory()
            
            # Disk metrics
            disk = psutil.disk_usage('/')
            
            # Process-specific metrics
            process = psutil.Process()
            process_memory = process.memory_info()
            process_cpu = process.cpu_percent()
            
            # Calculate resource health score
            resource_issues = []
            
            if cpu_percent > 80:
                resource_issues.append('High CPU usage')
            if memory.percent > 85:
                resource_issues.append('High memory usage')
            if disk.percent > 90:
                resource_issues.append('Low disk space')
            
            health_score = max(0, 100 - len(resource_issues) * 25)
            
            return {
                'health_score': health_score,
                'cpu': {
                    'usage_percent': cpu_percent,
                    'count': cpu_count,
                    'status': 'critical' if cpu_percent > 90 else 'warning' if cpu_percent > 80 else 'healthy'
                },
                'memory': {
                    'total_gb': round(memory.total / (1024**3), 2),
                    'available_gb': round(memory.available / (1024**3), 2),
                    'used_percent': memory.percent,
                    'status': 'critical' if memory.percent > 95 else 'warning' if memory.percent > 85 else 'healthy'
                },
                'disk': {
                    'total_gb': round(disk.total / (1024**3), 2),
                    'free_gb': round(disk.free / (1024**3), 2),
                    'used_percent': disk.percent,
                    'status': 'critical' if disk.percent > 95 else 'warning' if disk.percent > 90 else 'healthy'
                },
                'process': {
                    'memory_mb': round(process_memory.rss / (1024**2), 2),
                    'cpu_percent': process_cpu
                },
                'issues': resource_issues,
                'recommendations': self._generate_resource_recommendations(resource_issues)
            }
        except ImportError:
            return {
                'error': 'psutil not available for system monitoring',
                'health_score': 0
            }
    
    def _analyze_wristband_health(self, patient: models.Patient) -> Dict[str, Any]:
        """Analyze individual wristband device health."""
        issues = []
        status = 'healthy'
        
        # Battery level check
        if patient.battery_level is not None:
            if patient.battery_level <= 10:
                issues.append('Critical battery level')
                status = 'critical'
            elif patient.battery_level <= 20:
                issues.append('Low battery level')
                status = 'warning' if status == 'healthy' else status
        
        # Connectivity check
        if patient.last_activity:
            hours_since_activity = (datetime.utcnow() - patient.last_activity).total_seconds() / 3600
            if hours_since_activity > 2:
                issues.append(f'No activity for {hours_since_activity:.1f} hours')
                status = 'critical'
        
        # Sensor integrity
        if not patient.strap_intact:
            issues.append('Strap integrity compromised')
            status = 'critical'
        
        if not patient.skin_contact:
            issues.append('Skin contact lost')
            status = 'warning' if status == 'healthy' else status
        
        return {
            'device_id': patient.patient_id,
            'device_type': 'wristband',
            'status': status,
            'battery_level': patient.battery_level,
            'last_activity': patient.last_activity.isoformat() if patient.last_activity else None,
            'issues': issues
        }
    
    def _analyze_receiver_health(self, receiver: models.Receiver, cutoff_time: datetime) -> Dict[str, Any]:
        """Analyze individual receiver node health."""
        issues = []
        status = 'healthy'
        
        # Connectivity check
        if not receiver.last_seen or receiver.last_seen < cutoff_time:
            issues.append('Device offline')
            status = 'critical'
        
        # WiFi signal strength
        if receiver.wifi_strength is not None:
            if receiver.wifi_strength < -80:
                issues.append('Very weak WiFi signal')
                status = 'critical'
            elif receiver.wifi_strength < -70:
                issues.append('Weak WiFi signal')
                status = 'warning' if status == 'healthy' else status
        
        # Uptime check
        if receiver.uptime is not None and receiver.uptime < 3600:  # Less than 1 hour uptime
            issues.append('Recent restart detected')
            status = 'warning' if status == 'healthy' else status
        
        return {
            'device_id': receiver.receiver_id,
            'device_type': 'receiver',
            'status': status,
            'wifi_strength': receiver.wifi_strength,
            'uptime_hours': round(receiver.uptime / 3600, 1) if receiver.uptime else None,
            'last_seen': receiver.last_seen.isoformat() if receiver.last_seen else None,
            'issues': issues
        }
    
    def _generate_maintenance_recommendations(
        self, 
        wristband_health: List[Dict[str, Any]], 
        receiver_health: List[Dict[str, Any]]
    ) -> List[str]:
        """Generate predictive maintenance recommendations."""
        recommendations = []
        
        # Wristband recommendations
        critical_wristbands = [d for d in wristband_health if d['status'] == 'critical']
        low_battery_devices = [d for d in wristband_health if d.get('battery_level', 100) <= 20]
        
        if critical_wristbands:
            recommendations.append(f'{len(critical_wristbands)} wristband(s) need immediate attention')
        
        if low_battery_devices:
            recommendations.append(f'{len(low_battery_devices)} wristband(s) need charging')
        
        # Receiver recommendations
        offline_receivers = [d for d in receiver_health if d['status'] == 'critical']
        weak_signal_receivers = [d for d in receiver_health if 'WiFi signal' in str(d.get('issues', []))]
        
        if offline_receivers:
            recommendations.append(f'{len(offline_receivers)} receiver(s) are offline')
        
        if weak_signal_receivers:
            recommendations.append(f'{len(weak_signal_receivers)} receiver(s) have weak WiFi signal')
        
        if not recommendations:
            recommendations.append('All devices are operating normally')
        
        return recommendations
    
    def _generate_network_recommendations(
        self, 
        connectivity_issues: List[Dict[str, Any]], 
        transmission_rate: float
    ) -> List[str]:
        """Generate network optimization recommendations."""
        recommendations = []
        
        if transmission_rate < 80:
            recommendations.append('Low data transmission rate - check network connectivity')
        
        offline_devices = [issue for issue in connectivity_issues if issue['issue'] == 'offline']
        if offline_devices:
            recommendations.append(f'{len(offline_devices)} device(s) are offline - check power and connectivity')
        
        weak_signal_devices = [issue for issue in connectivity_issues if issue['issue'] == 'weak_signal']
        if weak_signal_devices:
            recommendations.append(f'{len(weak_signal_devices)} device(s) have weak WiFi signal - consider WiFi extenders')
        
        if not recommendations:
            recommendations.append('Network performance is optimal')
        
        return recommendations
    
    def _generate_resource_recommendations(self, issues: List[str]) -> List[str]:
        """Generate system resource optimization recommendations."""
        recommendations = []
        
        if 'High CPU usage' in issues:
            recommendations.append('Consider upgrading CPU or optimizing application performance')
        
        if 'High memory usage' in issues:
            recommendations.append('Consider adding more RAM or optimizing memory usage')
        
        if 'Low disk space' in issues:
            recommendations.append('Free up disk space or add more storage capacity')
        
        if not recommendations:
            recommendations.append('System resources are operating within normal parameters')
        
        return recommendations
    
    def _calculate_db_performance_score(self, query_performance: Dict[str, Any], db_size_mb: float) -> float:
        """Calculate database performance score."""
        score = 100
        
        # Penalize slow queries
        for query_name, time_ms in query_performance.items():
            if isinstance(time_ms, (int, float)) and time_ms > self.query_performance_threshold:
                score -= 10
        
        # Penalize large database size
        if db_size_mb > 1000:
            score -= 20
        elif db_size_mb > 500:
            score -= 10
        
        return max(0, score)



class ReportingSystem:
    """Automated report generation and data export functionality."""
    
    def __init__(self):
        self.default_retention_days = 90
        self.report_formats = ['json', 'csv']
    
    def generate_patient_movement_report(
        self, 
        db: Session, 
        patient_id: str = None, 
        start_date: datetime = None, 
        end_date: datetime = None
    ) -> Dict[str, Any]:
        """
        Generate comprehensive patient movement report.
        
        Args:
            db: Database session
            patient_id: Specific patient ID (optional, generates for all if None)
            start_date: Report start date
            end_date: Report end date
            
        Returns:
            Dictionary with report data
        """
        if not start_date:
            start_date = datetime.utcnow() - timedelta(days=7)
        if not end_date:
            end_date = datetime.utcnow()
        
        movement_analytics = MovementAnalytics()
        
        if patient_id:
            # Single patient report
            pattern = movement_analytics.analyze_patient_movement_patterns(
                db, patient_id, (end_date - start_date).days
            )
            zone_analysis = movement_analytics.calculate_zone_dwell_analysis(
                db, patient_id, (end_date - start_date).days
            )
            activity_score = movement_analytics.calculate_patient_activity_score(
                db, patient_id, (end_date - start_date).days
            )
            
            return {
                'report_type': 'patient_movement',
                'patient_id': patient_id,
                'period': {
                    'start': start_date.isoformat(),
                    'end': end_date.isoformat()
                },
                'movement_pattern': {
                    'total_movements': pattern.total_movements,
                    'unique_zones': pattern.unique_zones,
                    'average_dwell_time': pattern.average_dwell_time,
                    'movement_frequency': pattern.movement_frequency,
                    'most_visited_zone': pattern.most_visited_zone,
                    'activity_score': pattern.activity_score,
                    'abnormal_patterns': pattern.abnormal_patterns
                },
                'zone_analysis': [
                    {
                        'zone': z.zone,
                        'total_time_minutes': z.total_time_minutes,
                        'visit_count': z.visit_count,
                        'average_dwell_minutes': z.average_dwell_minutes
                    } for z in zone_analysis
                ],
                'activity_metrics': activity_score,
                'generated_at': datetime.utcnow().isoformat()
            }
        else:
            # All patients report
            patients = db.query(models.Patient).all()
            patient_reports = []
            
            for patient in patients:
                pattern = movement_analytics.analyze_patient_movement_patterns(
                    db, patient.patient_id, (end_date - start_date).days
                )
                patient_reports.append({
                    'patient_id': patient.patient_id,
                    'patient_name': patient.name,
                    'total_movements': pattern.total_movements,
                    'activity_score': pattern.activity_score,
                    'abnormal_patterns': pattern.abnormal_patterns
                })
            
            return {
                'report_type': 'patient_movement_summary',
                'period': {
                    'start': start_date.isoformat(),
                    'end': end_date.isoformat()
                },
                'total_patients': len(patients),
                'patient_summaries': patient_reports,
                'generated_at': datetime.utcnow().isoformat()
            }
    
    def generate_alert_summary_report(
        self, 
        db: Session, 
        start_date: datetime = None, 
        end_date: datetime = None
    ) -> Dict[str, Any]:
        """
        Generate comprehensive alert summary report.
        
        Args:
            db: Database session
            start_date: Report start date
            end_date: Report end date
            
        Returns:
            Dictionary with alert report data
        """
        if not start_date:
            start_date = datetime.utcnow() - timedelta(days=30)
        if not end_date:
            end_date = datetime.utcnow()
        
        alert_analytics = AlertAnalytics()
        days = (end_date - start_date).days
        
        # Get comprehensive alert analysis
        trends = alert_analytics.analyze_alert_trends(db, days)
        effectiveness = alert_analytics.analyze_alert_effectiveness(db, days)
        escalation = alert_analytics.analyze_escalation_patterns(db, days)
        false_positives = alert_analytics.detect_false_positives(db, None, days)
        
        return {
            'report_type': 'alert_summary',
            'period': {
                'start': start_date.isoformat(),
                'end': end_date.isoformat(),
                'days': days
            },
            'summary': {
                'total_alerts': trends['total_alerts'],
                'daily_average': trends['daily_average'],
                'trend': trends['trend'],
                'peak_hour': trends['peak_hour']
            },
            'alert_types': trends['alert_types'],
            'effectiveness': effectiveness,
            'escalation_patterns': escalation,
            'false_positive_analysis': false_positives,
            'recommendations': trends['recommendations'],
            'generated_at': datetime.utcnow().isoformat()
        }
    
    def generate_system_performance_report(
        self, 
        db: Session
    ) -> Dict[str, Any]:
        """
        Generate system performance and health report.
        
        Args:
            db: Database session
            
        Returns:
            Dictionary with system performance report
        """
        performance_analytics = SystemPerformanceAnalytics()
        
        # Gather all performance metrics
        device_health = performance_analytics.analyze_device_health(db, 24)
        network_performance = performance_analytics.analyze_network_performance(db, 24)
        database_performance = performance_analytics.analyze_database_performance(db)
        system_resources = performance_analytics.monitor_system_resources()
        
        # Calculate overall system health score
        health_scores = [
            device_health['overall_health_score'],
            network_performance['network_health_score'],
            database_performance['performance_score'],
            system_resources['health_score']
        ]
        overall_health = statistics.mean(health_scores)
        
        return {
            'report_type': 'system_performance',
            'overall_health_score': round(overall_health, 1),
            'device_health': device_health,
            'network_performance': network_performance,
            'database_performance': database_performance,
            'system_resources': system_resources,
            'critical_issues': self._identify_critical_issues(
                device_health, network_performance, database_performance, system_resources
            ),
            'generated_at': datetime.utcnow().isoformat()
        }
    
    def generate_zone_occupancy_report(
        self, 
        db: Session, 
        start_date: datetime = None, 
        end_date: datetime = None
    ) -> Dict[str, Any]:
        """
        Generate zone occupancy and utilization report.
        
        Args:
            db: Database session
            start_date: Report start date
            end_date: Report end date
            
        Returns:
            Dictionary with zone occupancy report
        """
        if not start_date:
            start_date = datetime.utcnow() - timedelta(days=7)
        if not end_date:
            end_date = datetime.utcnow()
        
        # Get all zones
        zones = db.query(models.Zone).filter(models.Zone.is_active == True).all()
        
        zone_reports = []
        for zone in zones:
            # Get location history for this zone
            locations = db.query(models.LocationHistory).filter(
                models.LocationHistory.zone == zone.zone_code,
                models.LocationHistory.timestamp >= start_date,
                models.LocationHistory.timestamp <= end_date
            ).all()
            
            # Calculate unique patients
            unique_patients = len(set(loc.patient_id for loc in locations))
            
            # Calculate average occupancy
            total_visits = len(locations)
            
            # Check for violations
            violations = db.query(models.Alert).filter(
                models.Alert.message.like(f'%{zone.zone_name}%'),
                models.Alert.created_at >= start_date,
                models.Alert.created_at <= end_date
            ).count()
            
            zone_reports.append({
                'zone_code': zone.zone_code,
                'zone_name': zone.zone_name,
                'zone_type': zone.zone_type,
                'total_visits': total_visits,
                'unique_patients': unique_patients,
                'violations': violations,
                'max_occupancy': zone.max_occupancy,
                'utilization_rate': (unique_patients / zone.max_occupancy * 100) if zone.max_occupancy else None
            })
        
        return {
            'report_type': 'zone_occupancy',
            'period': {
                'start': start_date.isoformat(),
                'end': end_date.isoformat()
            },
            'total_zones': len(zones),
            'zone_details': zone_reports,
            'generated_at': datetime.utcnow().isoformat()
        }
    
    def export_report_to_csv(self, report_data: Dict[str, Any]) -> str:
        """
        Export report data to CSV format.
        
        Args:
            report_data: Report data dictionary
            
        Returns:
            CSV formatted string
        """
        import csv
        import io
        
        output = io.StringIO()
        
        # Handle different report types
        report_type = report_data.get('report_type', 'unknown')
        
        if report_type == 'patient_movement_summary':
            writer = csv.DictWriter(output, fieldnames=[
                'patient_id', 'patient_name', 'total_movements', 
                'activity_score', 'abnormal_patterns'
            ])
            writer.writeheader()
            for patient in report_data.get('patient_summaries', []):
                writer.writerow({
                    'patient_id': patient['patient_id'],
                    'patient_name': patient['patient_name'],
                    'total_movements': patient['total_movements'],
                    'activity_score': patient['activity_score'],
                    'abnormal_patterns': ', '.join(patient['abnormal_patterns'])
                })
        
        elif report_type == 'alert_summary':
            writer = csv.DictWriter(output, fieldnames=[
                'metric', 'value'
            ])
            writer.writeheader()
            summary = report_data.get('summary', {})
            for key, value in summary.items():
                writer.writerow({'metric': key, 'value': value})
        
        elif report_type == 'zone_occupancy':
            writer = csv.DictWriter(output, fieldnames=[
                'zone_code', 'zone_name', 'zone_type', 'total_visits',
                'unique_patients', 'violations', 'max_occupancy', 'utilization_rate'
            ])
            writer.writeheader()
            for zone in report_data.get('zone_details', []):
                writer.writerow(zone)
        
        return output.getvalue()
    
    def schedule_report_generation(
        self, 
        db: Session, 
        report_type: str, 
        frequency: str = 'daily'
    ) -> Dict[str, Any]:
        """
        Schedule automated report generation.
        
        Args:
            db: Database session
            report_type: Type of report to generate
            frequency: Report frequency (daily, weekly, monthly)
            
        Returns:
            Dictionary with scheduling confirmation
        """
        # This is a placeholder for scheduling logic
        # In production, this would integrate with a task scheduler like Celery
        
        return {
            'success': True,
            'report_type': report_type,
            'frequency': frequency,
            'next_generation': self._calculate_next_generation_time(frequency),
            'message': f'{report_type} report scheduled for {frequency} generation'
        }
    
    def apply_data_retention_policy(
        self, 
        db: Session, 
        retention_days: int = None
    ) -> Dict[str, Any]:
        """
        Apply data retention policy and clean up old data.
        
        Args:
            db: Database session
            retention_days: Number of days to retain data
            
        Returns:
            Dictionary with cleanup results
        """
        if retention_days is None:
            retention_days = self.default_retention_days
        
        cutoff_date = datetime.utcnow() - timedelta(days=retention_days)
        
        # Clean up old location history
        old_locations = db.query(models.LocationHistory).filter(
            models.LocationHistory.timestamp < cutoff_date
        ).count()
        
        db.query(models.LocationHistory).filter(
            models.LocationHistory.timestamp < cutoff_date
        ).delete()
        
        # Clean up resolved alerts
        old_alerts = db.query(models.Alert).filter(
            models.Alert.created_at < cutoff_date,
            models.Alert.status == 'resolved'
        ).count()
        
        db.query(models.Alert).filter(
            models.Alert.created_at < cutoff_date,
            models.Alert.status == 'resolved'
        ).delete()
        
        # Clean up old access logs
        old_logs = db.query(models.AccessLog).filter(
            models.AccessLog.timestamp < cutoff_date
        ).count()
        
        db.query(models.AccessLog).filter(
            models.AccessLog.timestamp < cutoff_date
        ).delete()
        
        db.commit()
        
        return {
            'success': True,
            'retention_days': retention_days,
            'cutoff_date': cutoff_date.isoformat(),
            'records_deleted': {
                'location_history': old_locations,
                'alerts': old_alerts,
                'access_logs': old_logs
            },
            'total_deleted': old_locations + old_alerts + old_logs
        }
    
    def create_data_backup(
        self, 
        db: Session, 
        backup_type: str = 'full'
    ) -> Dict[str, Any]:
        """
        Create database backup for disaster recovery.
        
        Args:
            db: Database session
            backup_type: Type of backup (full, incremental)
            
        Returns:
            Dictionary with backup details
        """
        import shutil
        import os
        
        timestamp = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
        backup_filename = f"halo_watch_backup_{backup_type}_{timestamp}.db"
        
        try:
            # Simple SQLite backup using file copy
            shutil.copy2("halo_watch.db", backup_filename)
            
            backup_size = os.path.getsize(backup_filename) / (1024 * 1024)  # MB
            
            return {
                'success': True,
                'backup_type': backup_type,
                'backup_file': backup_filename,
                'size_mb': round(backup_size, 2),
                'timestamp': datetime.utcnow().isoformat()
            }
        except Exception as e:
            return {
                'success': False,
                'error': str(e),
                'timestamp': datetime.utcnow().isoformat()
            }
    
    def _identify_critical_issues(
        self, 
        device_health: Dict[str, Any], 
        network_performance: Dict[str, Any],
        database_performance: Dict[str, Any],
        system_resources: Dict[str, Any]
    ) -> List[Dict[str, Any]]:
        """Identify critical issues across all system components."""
        critical_issues = []
        
        # Device health issues
        if device_health['overall_health_score'] < 70:
            critical_issues.append({
                'category': 'device_health',
                'severity': 'high',
                'description': f"Device health score is low: {device_health['overall_health_score']}%",
                'recommendations': device_health['maintenance_recommendations']
            })
        
        # Network issues
        if network_performance['network_health_score'] < 80:
            critical_issues.append({
                'category': 'network',
                'severity': 'high',
                'description': f"Network health score is low: {network_performance['network_health_score']}%",
                'recommendations': network_performance['recommendations']
            })
        
        # Database issues
        if database_performance['performance_score'] < 70:
            critical_issues.append({
                'category': 'database',
                'severity': 'medium',
                'description': f"Database performance score is low: {database_performance['performance_score']}%",
                'recommendations': database_performance['optimization_recommendations']
            })
        
        # System resource issues
        if system_resources['health_score'] < 70:
            critical_issues.append({
                'category': 'system_resources',
                'severity': 'high',
                'description': f"System resource health is low: {system_resources['health_score']}%",
                'recommendations': system_resources['recommendations']
            })
        
        return critical_issues
    
    def _calculate_next_generation_time(self, frequency: str) -> str:
        """Calculate next report generation time based on frequency."""
        now = datetime.utcnow()
        
        if frequency == 'daily':
            next_time = now + timedelta(days=1)
        elif frequency == 'weekly':
            next_time = now + timedelta(weeks=1)
        elif frequency == 'monthly':
            next_time = now + timedelta(days=30)
        else:
            next_time = now + timedelta(days=1)
        
        return next_time.isoformat()
