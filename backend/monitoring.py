import logging
import psutil
import sqlite3
import os
from datetime import datetime, timedelta
from typing import Dict, Any, List, Optional
from sqlalchemy.orm import Session
from sqlalchemy import text
from fastapi import Request, HTTPException
from fastapi.responses import JSONResponse
import traceback
import time

from . import models
from .database import SessionLocal, engine


# Configure structured logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('halo_watch.log'),
        logging.StreamHandler()
    ]
)

logger = logging.getLogger(__name__)


class SystemMonitor:
    """System health monitoring and metrics collection."""
    
    @staticmethod
    def get_system_health() -> Dict[str, Any]:
        """Get comprehensive system health metrics."""
        try:
            # CPU and Memory metrics
            cpu_percent = psutil.cpu_percent(interval=1)
            memory = psutil.virtual_memory()
            disk = psutil.disk_usage('/')
            
            # Database metrics
            db_metrics = SystemMonitor.get_database_metrics()
            
            # Application metrics
            app_metrics = SystemMonitor.get_application_metrics()
            
            # Determine overall health status
            health_status = "healthy"
            issues = []
            
            if cpu_percent > 80:
                health_status = "warning"
                issues.append(f"High CPU usage: {cpu_percent}%")
            
            if memory.percent > 85:
                health_status = "critical" if memory.percent > 95 else "warning"
                issues.append(f"High memory usage: {memory.percent}%")
            
            if disk.percent > 90:
                health_status = "critical" if disk.percent > 95 else "warning"
                issues.append(f"Low disk space: {disk.percent}% used")
            
            return {
                "status": health_status,
                "timestamp": datetime.utcnow().isoformat(),
                "issues": issues,
                "metrics": {
                    "cpu": {
                        "usage_percent": cpu_percent,
                        "count": psutil.cpu_count()
                    },
                    "memory": {
                        "total_gb": round(memory.total / (1024**3), 2),
                        "available_gb": round(memory.available / (1024**3), 2),
                        "used_percent": memory.percent
                    },
                    "disk": {
                        "total_gb": round(disk.total / (1024**3), 2),
                        "free_gb": round(disk.free / (1024**3), 2),
                        "used_percent": disk.percent
                    },
                    "database": db_metrics,
                    "application": app_metrics
                }
            }
        except Exception as e:
            logger.error(f"Error getting system health: {str(e)}")
            return {
                "status": "error",
                "timestamp": datetime.utcnow().isoformat(),
                "error": str(e)
            }
    
    @staticmethod
    def get_database_metrics() -> Dict[str, Any]:
        """Get database performance metrics."""
        try:
            db = SessionLocal()
            try:
                # Database size
                db_path = "halo_watch.db"
                db_size_mb = os.path.getsize(db_path) / (1024 * 1024) if os.path.exists(db_path) else 0
                
                # Table counts
                patient_count = db.query(models.Patient).count()
                alert_count = db.query(models.Alert).count()
                location_count = db.query(models.LocationHistory).count()
                
                # Recent activity (last 24 hours)
                cutoff = datetime.utcnow() - timedelta(hours=24)
                recent_alerts = db.query(models.Alert).filter(models.Alert.created_at >= cutoff).count()
                recent_locations = db.query(models.LocationHistory).filter(models.LocationHistory.timestamp >= cutoff).count()
                
                # Connection test
                start_time = time.time()
                db.execute(text("SELECT 1"))
                query_time_ms = (time.time() - start_time) * 1000
                
                return {
                    "size_mb": round(db_size_mb, 2),
                    "query_time_ms": round(query_time_ms, 2),
                    "tables": {
                        "patients": patient_count,
                        "alerts": alert_count,
                        "locations": location_count
                    },
                    "recent_activity": {
                        "alerts_24h": recent_alerts,
                        "locations_24h": recent_locations
                    }
                }
            finally:
                db.close()
        except Exception as e:
            logger.error(f"Error getting database metrics: {str(e)}")
            return {"error": str(e)}
    
    @staticmethod
    def get_application_metrics() -> Dict[str, Any]:
        """Get application-specific metrics."""
        try:
            db = SessionLocal()
            try:
                # Active patients and devices
                active_patients = db.query(models.Patient).filter(
                    models.Patient.last_activity >= datetime.utcnow() - timedelta(hours=1)
                ).count()
                
                # Active alerts
                active_alerts = db.query(models.Alert).filter(models.Alert.status == 'active').count()
                
                # Receiver status
                active_receivers = db.query(models.Receiver).filter(
                    models.Receiver.last_seen >= datetime.utcnow() - timedelta(minutes=10)
                ).count()
                
                total_receivers = db.query(models.Receiver).count()
                
                # System uptime
                server_status = db.query(models.ServerStatus).first()
                uptime_seconds = 0
                if server_status:
                    uptime_seconds = int((datetime.utcnow() - server_status.started_at).total_seconds())
                
                return {
                    "active_patients": active_patients,
                    "active_alerts": active_alerts,
                    "receivers": {
                        "active": active_receivers,
                        "total": total_receivers,
                        "offline": total_receivers - active_receivers
                    },
                    "uptime_seconds": uptime_seconds
                }
            finally:
                db.close()
        except Exception as e:
            logger.error(f"Error getting application metrics: {str(e)}")
            return {"error": str(e)}
    
    @staticmethod
    def create_backup() -> Dict[str, Any]:
        """Create a database backup."""
        try:
            timestamp = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
            backup_filename = f"halo_watch_backup_{timestamp}.db"
            
            # Simple SQLite backup using file copy
            import shutil
            shutil.copy2("halo_watch.db", backup_filename)
            
            backup_size = os.path.getsize(backup_filename) / (1024 * 1024)  # MB
            
            logger.info(f"Database backup created: {backup_filename}")
            
            return {
                "success": True,
                "backup_file": backup_filename,
                "size_mb": round(backup_size, 2),
                "timestamp": datetime.utcnow().isoformat()
            }
        except Exception as e:
            logger.error(f"Error creating backup: {str(e)}")
            return {
                "success": False,
                "error": str(e)
            }
    
    @staticmethod
    def cleanup_old_data(days_to_keep: int = 90) -> Dict[str, Any]:
        """Clean up old data to maintain database performance."""
        try:
            db = SessionLocal()
            try:
                cutoff_date = datetime.utcnow() - timedelta(days=days_to_keep)
                
                # Clean up old location history
                old_locations = db.query(models.LocationHistory).filter(
                    models.LocationHistory.timestamp < cutoff_date
                ).count()
                
                db.query(models.LocationHistory).filter(
                    models.LocationHistory.timestamp < cutoff_date
                ).delete()
                
                # Clean up resolved alerts older than retention period
                old_alerts = db.query(models.Alert).filter(
                    models.Alert.created_at < cutoff_date,
                    models.Alert.status == 'resolved'
                ).count()
                
                db.query(models.Alert).filter(
                    models.Alert.created_at < cutoff_date,
                    models.Alert.status == 'resolved'
                ).delete()
                
                db.commit()
                
                logger.info(f"Cleaned up {old_locations} location records and {old_alerts} resolved alerts")
                
                return {
                    "success": True,
                    "locations_cleaned": old_locations,
                    "alerts_cleaned": old_alerts,
                    "cutoff_date": cutoff_date.isoformat()
                }
            finally:
                db.close()
        except Exception as e:
            logger.error(f"Error during cleanup: {str(e)}")
            return {
                "success": False,
                "error": str(e)
            }


class ErrorHandler:
    """Centralized error handling and logging."""
    
    @staticmethod
    def log_error(error: Exception, context: Dict[str, Any] = None):
        """Log error with context information."""
        error_info = {
            "error_type": type(error).__name__,
            "error_message": str(error),
            "traceback": traceback.format_exc(),
            "timestamp": datetime.utcnow().isoformat(),
            "context": context or {}
        }
        
        logger.error(f"Application Error: {error_info}")
        
        # Store critical errors in database for monitoring
        if isinstance(error, (HTTPException, ConnectionError, sqlite3.Error)):
            try:
                db = SessionLocal()
                try:
                    # Create system alert for critical errors
                    alert = models.Alert(
                        alert_code=f"SYS-ERROR-{int(datetime.utcnow().timestamp())}",
                        type="critical",
                        title="System Error",
                        message=f"{type(error).__name__}: {str(error)}",
                        patient_id=None,
                        room=None,
                        status="active",
                        is_read=False,
                        created_at=datetime.utcnow(),
                        alert_metadata=str(error_info)
                    )
                    db.add(alert)
                    db.commit()
                finally:
                    db.close()
            except Exception as db_error:
                logger.error(f"Failed to log error to database: {str(db_error)}")
    
    @staticmethod
    def create_error_response(error: Exception, status_code: int = 500) -> JSONResponse:
        """Create standardized error response."""
        ErrorHandler.log_error(error)
        
        return JSONResponse(
            status_code=status_code,
            content={
                "error": True,
                "message": str(error),
                "timestamp": datetime.utcnow().isoformat(),
                "status_code": status_code
            }
        )


async def error_handling_middleware(request: Request, call_next):
    """Global error handling middleware."""
    try:
        start_time = time.time()
        response = await call_next(request)
        process_time = time.time() - start_time
        
        # Log slow requests
        if process_time > 2.0:  # Log requests taking more than 2 seconds
            logger.warning(f"Slow request: {request.method} {request.url} took {process_time:.2f}s")
        
        return response
    except Exception as e:
        ErrorHandler.log_error(e, {
            "method": request.method,
            "url": str(request.url),
            "client": request.client.host if request.client else "unknown"
        })
        return ErrorHandler.create_error_response(e)