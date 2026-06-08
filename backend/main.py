from __future__ import annotations

from datetime import datetime, timedelta
from typing import Any, Dict, List, Optional, Literal
from pydantic import BaseModel

from fastapi import Depends, FastAPI, HTTPException, WebSocket, WebSocketDisconnect, status, Query
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy.orm import Session
from sqlalchemy import text

from . import models, schemas, seed
from .database import SessionLocal, engine, ensure_sqlite_schema
from .auth import AuthenticationService, require_admin, require_staff, require_any_role
from .zone_service import ZoneManagementService
from .door_config import DoorConfigurationService
from .alert_engine import AlertEngine, AlertThreshold
from .monitoring import SystemMonitor, ErrorHandler, error_handling_middleware

# Create tables if they do not exist and patch SQLite schema for any
# missing patient metadata columns in existing databases.
models.Base.metadata.create_all(bind=engine)
ensure_sqlite_schema()

app = FastAPI(title="Halo Watch Patient Monitoring Backend", version="1.0.0")


origins = [
    "http://localhost:8080",
    "http://127.0.0.1:8080",
]

app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Add error handling middleware
app.middleware("http")(error_handling_middleware)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


class ConnectionManager:
    """
    Very small WebSocket broadcast manager for pushing live alerts / updates
    to connected dashboards.
    """

    def __init__(self) -> None:
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket) -> None:
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: Dict[str, Any]) -> None:
        to_remove: List[WebSocket] = []
        for connection in list(self.active_connections):
            try:
                await connection.send_json(message)
            except WebSocketDisconnect:
                to_remove.append(connection)
            except Exception:
                # Best-effort broadcasting; drop misbehaving sockets
                to_remove.append(connection)

        for conn in to_remove:
            self.disconnect(conn)


ws_manager = ConnectionManager()
alert_engine = AlertEngine()


@app.on_event("startup")
def on_startup() -> None:
    # Seed DB with realistic sample data on first run
    db = SessionLocal()
    try:
        seed.seed_initial_data(db)
    finally:
        db.close()


@app.websocket("/ws/live")
async def websocket_live(websocket: WebSocket) -> None:
    await ws_manager.connect(websocket)
    try:
        # Keep the connection open; we don't require any messages from the client
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        ws_manager.disconnect(websocket)


# ---------- Utility helpers ----------


def _humanize_duration_seconds(seconds: int | None) -> str | None:
    if seconds is None:
        return None
    minutes, _ = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    parts: List[str] = []
    if hours:
        parts.append(f"{hours}h")
    if minutes:
        parts.append(f"{minutes}min")
    return " ".join(parts) or "0min"


def _relative_minutes_from(dt: datetime | None) -> str | None:
    if not dt:
        return None
    diff = datetime.utcnow() - dt
    minutes = int(diff.total_seconds() // 60)
    if minutes <= 1:
        return "1 minute ago"
    if minutes < 60:
        return f"{minutes} minutes ago"
    hours = minutes // 60
    return f"{hours}h ago"


def _get_server_started_at(db: Session) -> datetime:
    status = db.query(models.ServerStatus).first()
    if status:
        return status.started_at
    now = datetime.utcnow()
    status = models.ServerStatus(started_at=now)
    db.add(status)
    db.commit()
    db.refresh(status)
    return status.started_at


# ---------- Reporting helpers ----------


def _parse_report_range(from_str: str, to_str: str) -> tuple[datetime, datetime]:
    """
    Parse simple YYYY-MM-DD query parameters into an inclusive date range
    [start, end_exclusive).
    """
    try:
        start = datetime.fromisoformat(from_str)
        end = datetime.fromisoformat(to_str)
    except ValueError:
        raise HTTPException(
            status_code=400,
            detail="Invalid date format. Expected YYYY-MM-DD",
        )
    if end < start:
        raise HTTPException(
            status_code=400,
            detail="'to' date must be on or after 'from' date",
        )
    # Use the end of the "to" day as an exclusive upper bound.
    end_exclusive = end + timedelta(days=1)
    return start, end_exclusive


def _daterange(start: datetime, end_exclusive: datetime) -> List[datetime]:
    days: List[datetime] = []
    cur = datetime(start.year, start.month, start.day)
    while cur < end_exclusive:
        days.append(cur)
        cur += timedelta(days=1)
    return days


def _build_patient_activity_series(
    db: Session, start: datetime, end_exclusive: datetime
) -> List[Dict[str, Any]]:
    from sqlalchemy import func

    total_patients = db.query(models.Patient).count()
    # Pre-compute global averages once for simplicity.
    avg_steps = (
        db.query(func.avg(models.Patient.movement_steps)).scalar() or 0
    )
    avg_hr = (
        db.query(func.avg(models.Patient.heart_rate))
        .filter(models.Patient.heart_rate.isnot(None))
        .scalar()
        or 0
    )

    series: List[Dict[str, Any]] = []
    for day in _daterange(start, end_exclusive):
        next_day = day + timedelta(days=1)
        active_patients = (
            db.query(models.Patient)
            .filter(
                models.Patient.last_activity >= day,
                models.Patient.last_activity < next_day,
            )
            .count()
        )
        series.append(
            {
                "date": day.date().isoformat(),
                "totalPatients": int(total_patients),
                "activePatients": int(active_patients),
                "averageSteps": int(avg_steps),
            }
        )
    return series


def _build_alert_statistics_series(
    db: Session, start: datetime, end_exclusive: datetime
) -> List[Dict[str, Any]]:
    from sqlalchemy import func

    series: List[Dict[str, Any]] = []
    for day in _daterange(start, end_exclusive):
        next_day = day + timedelta(days=1)
        alerts_q = db.query(models.Alert).filter(
            models.Alert.created_at >= day,
            models.Alert.created_at < next_day,
        )
        total_alerts = alerts_q.count()
        critical_alerts = alerts_q.filter(models.Alert.type == "critical").count()
        resolved_alerts = alerts_q.filter(models.Alert.status == "resolved").count()

        # Average response time in minutes for resolved alerts.
        resolved = alerts_q.filter(
            models.Alert.status == "resolved",
            models.Alert.resolved_at.isnot(None),
        ).all()
        if resolved:
            total_minutes = 0.0
            for a in resolved:
                delta = (a.resolved_at - a.created_at).total_seconds() / 60.0  # type: ignore[operator]
                total_minutes += max(delta, 0.0)
            avg_response = total_minutes / len(resolved)
        else:
            avg_response = 0.0

        series.append(
            {
                "date": day.date().isoformat(),
                "totalAlerts": int(total_alerts),
                "criticalAlerts": int(critical_alerts),
                "resolvedAlerts": int(resolved_alerts),
                "averageResponseTime": round(avg_response, 1),
            }
        )
    return series


def _build_zone_occupancy_summary(
    db: Session, start: datetime, end_exclusive: datetime
) -> List[Dict[str, Any]]:
    # Visit counts from location history.
    visits: Dict[str, int] = {}
    histories = (
        db.query(models.LocationHistory)
        .filter(
            models.LocationHistory.timestamp >= start,
            models.LocationHistory.timestamp < end_exclusive,
        )
        .all()
    )
    for h in histories:
        visits[h.zone] = visits.get(h.zone, 0) + 1

    # Zone violations per zone from zone_violation alerts.
    violation_counts: Dict[str, int] = {}
    alerts = (
        db.query(models.Alert)
        .filter(
            models.Alert.type == "zone_violation",
            models.Alert.created_at >= start,
            models.Alert.created_at < end_exclusive,
        )
        .all()
    )
    import json

    for a in alerts:
        code = None
        if a.alert_metadata:
            try:
                meta = json.loads(a.alert_metadata)
                code = meta.get("zone_code")
            except Exception:
                code = None
        if not code:
            code = a.room or "UNKNOWN"
        violation_counts[code] = violation_counts.get(code, 0) + 1

    zones = db.query(models.Zone).all()
    results: List[Dict[str, Any]] = []
    for z in zones:
        total_visits = visits.get(z.zone_code, 0)
        violations = violation_counts.get(z.zone_code, 0)
        # Heuristic: assume average duration of 8 minutes per visit.
        average_duration = float(total_visits * 8) if total_visits else 0.0
        results.append(
            {
                "zoneName": z.zone_name,
                "totalVisits": int(total_visits),
                "averageDuration": round(average_duration, 1),
                "violations": int(violations),
            }
        )

    return sorted(results, key=lambda z: z["totalVisits"], reverse=True)


def _build_system_performance_series(
    db: Session, start: datetime, end_exclusive: datetime
) -> List[Dict[str, Any]]:
    # We do not currently persist time‑series performance metrics, so we
    # synthesise a simple, stable series based on the date range.
    series: List[Dict[str, Any]] = []
    for day in _daterange(start, end_exclusive):
        # Derive pseudo‑random but deterministic values from the date.
        key = int(day.strftime("%Y%m%d"))
        uptime = 97.0 + (key % 20) / 10.0  # 97.0‑98.9 %
        connectivity = 93.0 + (key % 30) / 10.0  # 93.0‑95.9 %
        data_rate = 120.0 + (key % 50)  # KB/s
        series.append(
            {
                "date": day.date().isoformat(),
                "uptime": round(min(uptime, 99.9), 1),
                "deviceConnectivity": round(min(connectivity, 99.9), 1),
                "dataTransmissionRate": round(data_rate, 1),
            }
        )
    return series


def _build_patient_metrics_table(
    db: Session, start: datetime, end_exclusive: datetime
) -> List[Dict[str, Any]]:
    patients = db.query(models.Patient).all()
    alerts = (
        db.query(models.Alert)
        .filter(
            models.Alert.created_at >= start,
            models.Alert.created_at < end_exclusive,
        )
        .all()
    )
    # Index alerts by patient.
    alerts_by_patient: Dict[str, List[models.Alert]] = {}
    for a in alerts:
        if not a.patient_id:
            continue
        alerts_by_patient.setdefault(a.patient_id, []).append(a)

    metrics: List[Dict[str, Any]] = []
    for p in patients:
        patient_alerts = alerts_by_patient.get(p.patient_id, [])
        alerts_generated = len(patient_alerts)
        zone_violations = sum(
            1 for a in patient_alerts if a.type == "zone_violation"
        )
        metrics.append(
            {
                "patientId": p.patient_id,
                "patientName": p.name,
                "totalSteps": int(p.movement_steps or 0),
                "alertsGenerated": alerts_generated,
                "zoneViolations": zone_violations,
            }
        )
    return metrics
# ---------- Dashboard APIs ----------


def _infer_room_kind(name: str) -> str:
    n = name.lower()
    if "isolation" in n:
        return "isolation"
    if "restricted" in n:
        return "restricted"
    if "nurse" in n:
        return "nurses_station"
    if "control" in n:
        return "control_room"
    if "waiting" in n:
        return "waiting_room"
    if "corridor" in n:
        return "corridor"
    if "entry" in n or "entrance" in n:
        return "entry"
    if "wash" in n or "toilet" in n or "wc" in n:
        return "washroom"
    # Default to patient room
    return "patient_room"


@app.get("/api/v1/rooms", response_model=List[schemas.RoomSummary])
def get_rooms(db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)) -> List[schemas.RoomSummary]:
    rooms = db.query(models.Room).all()
    result: List[schemas.RoomSummary] = []
    for room in rooms:
        kind = _infer_room_kind(room.name or "")
        result.append(
            schemas.RoomSummary(
                id=room.room_code,
                name=room.name,
                status=room.status or "normal",
                position=schemas.RoomPosition(x=room.pos_x, y=room.pos_y),
                kind=kind,  # type: ignore[arg-type]
                patientId=room.patient_id,
            )
        )
    return result


@app.get("/api/v1/patients", response_model=List[schemas.PatientSummary])
def get_patients(db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)) -> List[schemas.PatientSummary]:
    patients = db.query(models.Patient).all()
    result: List[schemas.PatientSummary] = []
    for p in patients:
        result.append(
            schemas.PatientSummary(
                id=p.patient_id,
                name=p.name,
                age=p.age,
                room=p.room,
                status=p.status or "normal",
                location=p.location,
                lastActivity=_relative_minutes_from(p.last_activity),
                movementSteps=p.movement_steps,
                strap_intact=p.strap_intact if p.strap_intact is not None else True,
            )
        )
    return result


@app.get("/api/v1/alerts", response_model=List[schemas.NotificationOut])
def get_alerts(
    status: str | None = None, db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)
) -> List[schemas.NotificationOut]:
    query = db.query(models.Alert).order_by(models.Alert.created_at.desc())
    if status:
        query = query.filter(models.Alert.status == status)
    alerts = query.limit(20).all()
    result: List[schemas.NotificationOut] = []
    for a in alerts:
        result.append(
            schemas.NotificationOut(
                id=a.alert_code,
                type=a.type or "alert",
                title=a.title,
                message=a.message,
                timestamp=a.created_at,
                patientId=a.patient_id,
                room=a.room,
                isRead=a.is_read,
                status=a.status,
                escalation_level=a.escalation_level or 1,
                acknowledged_at=a.acknowledged_at,
                acknowledged_by=a.acknowledged_by,
                resolved_at=a.resolved_at,
                resolved_by=a.resolved_by,
            )
        )
    return result


@app.post("/api/v1/alerts/{alert_code}/acknowledge")
def acknowledge_alert(alert_code: str, db: Session = Depends(get_db), current_user: models.User = Depends(require_staff)) -> Dict[str, Any]:
    success = alert_engine.acknowledge_alert(db, alert_code, current_user.username)
    if not success:
        raise HTTPException(status_code=404, detail="Alert not found")
    return {"success": True, "message": "Alert acknowledged successfully"}


@app.post("/api/v1/alerts/{alert_code}/resolve")
def resolve_alert(
    alert_code: str, 
    resolve_data: schemas.AlertResolveRequest, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_staff)
) -> Dict[str, Any]:
    """Resolve an alert."""
    success = alert_engine.resolve_alert(db, alert_code, current_user.username)
    if not success:
        raise HTTPException(status_code=404, detail="Alert not found")
    return {"success": True, "message": "Alert resolved successfully"}


@app.get("/api/v1/alerts/statistics", response_model=schemas.AlertStatistics)
def get_alert_statistics(
    hours: int = 24, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> schemas.AlertStatistics:
    """Get alert statistics for the specified time period."""
    stats = alert_engine.get_alert_statistics(db, hours)
    return schemas.AlertStatistics(**stats)


@app.put("/api/v1/alerts/thresholds")
def update_alert_thresholds(
    thresholds: schemas.AlertThresholdConfig, 
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Update alert thresholds (admin only)."""
    global alert_engine
    alert_engine.thresholds = AlertThreshold(
        battery_low_threshold=thresholds.battery_low_threshold
    )
    return {"success": True, "message": "Alert thresholds updated successfully"}


@app.get("/api/v1/alerts/thresholds", response_model=schemas.AlertThresholdConfig)
def get_alert_thresholds(current_user: models.User = Depends(require_any_role)) -> schemas.AlertThresholdConfig:
    """Get current alert thresholds."""
    return schemas.AlertThresholdConfig(
        battery_low_threshold=alert_engine.thresholds.battery_low_threshold
    )


@app.get("/api/v1/access-logs", response_model=List[schemas.AccessLogOut])
def get_access_logs(db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)) -> List[schemas.AccessLogOut]:
    logs = (
        db.query(models.AccessLog)
        .order_by(models.AccessLog.timestamp.desc())
        .limit(200)
        .all()
    )
    result: List[schemas.AccessLogOut] = []
    for log in logs:
        result.append(
            schemas.AccessLogOut(
                id=log.log_code,
                patientId=log.patient_id,
                patientName=log.patient_name,
                room=log.room,
                action=log.action,
                timestamp=log.timestamp,
                rfidId=log.rfid_id,
                duration=_humanize_duration_seconds(log.duration_seconds),
            )
        )
    return result


@app.get("/api/v1/overview", response_model=schemas.OverviewStats)
def get_overview(db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)) -> schemas.OverviewStats:
    active_patients = db.query(models.Patient).count()
    total_rooms = db.query(models.Room).count()
    occupied_rooms = (
        db.query(models.Room).filter(models.Room.patient_id.isnot(None)).count()
    )
    available_rooms = max(total_rooms - occupied_rooms, 0)

    active_alerts = (
        db.query(models.Alert).filter(models.Alert.status == "active").count()
    )

    started_at = _get_server_started_at(db)
    uptime_seconds = int((datetime.utcnow() - started_at).total_seconds())

    return schemas.OverviewStats(
        activePatients=active_patients,
        availableRooms=available_rooms,
        activeAlerts=active_alerts,
        systemUptimeSeconds=uptime_seconds,
    )


@app.get("/api/v1/patients/active-assignments")
def list_active_assignments(db: Session = Depends(get_db)):
    """List all active patient assignments with rfid_uid and assigned room."""
    patients = db.query(models.Patient).filter(models.Patient.rfid_uid != None).all()
    return [
        {
            "rfid_uid": p.rfid_uid,
            "room": p.room or ""
        }
        for p in patients
    ]


@app.get("/api/v1/patients/{patient_id}", response_model=schemas.PatientSummary)
def get_patient_detail(
    patient_id: str, db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)
) -> schemas.PatientSummary:
    p = (
        db.query(models.Patient)
        .filter(models.Patient.patient_id == patient_id)
        .first()
    )
    if not p:
        raise HTTPException(status_code=404, detail="Patient not found")

    return schemas.PatientSummary(
        id=p.patient_id,
        name=p.name,
        age=p.age,
        room=p.room,
        status=p.status or "normal",
        location=p.location,
        lastActivity=_relative_minutes_from(p.last_activity),
        movementSteps=p.movement_steps,
        strap_intact=p.strap_intact if p.strap_intact is not None else True,
    )


@app.get("/api/v1/patients/{patient_id}/details", response_model=schemas.PatientDetails)
def get_patient_details(
    patient_id: str, db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)
) -> schemas.PatientDetails:
    p = (
        db.query(models.Patient)
        .filter(models.Patient.patient_id == patient_id)
        .first()
    )
    if not p:
        raise HTTPException(status_code=404, detail="Patient not found")

    contact = {"name": "N/A", "phone": "N/A", "relationship": "N/A"}
    if p.emergency_contact:
        try:
            import json
            contact = json.loads(p.emergency_contact)
            if not isinstance(contact, dict):
                 contact = {"name": str(contact), "phone": "N/A", "relationship": "N/A"}
        except:
            contact = {"name": p.emergency_contact, "phone": "N/A", "relationship": "N/A"}

    return schemas.PatientDetails(
        id=p.patient_id,
        name=p.name,
        age=p.age,
        room=p.room,
        status=p.status or "normal",
        location=p.location,
        lastActivity=_relative_minutes_from(p.last_activity),
        movementSteps=p.movement_steps,
        strap_intact=p.strap_intact if p.strap_intact is not None else True,
        medicalRecordNumber=p.patient_id,
        admissionDate=p.last_activity,
        emergencyContact=contact,
        medicalNotes=p.medical_notes,
        allergies=[],
        medications=[],
        movementPattern=[],
    )


@app.delete("/api/v1/patients/{patient_id}")
def delete_patient(
    patient_id: str,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_staff),
) -> Dict[str, Any]:
    """Delete a patient and cleanup room assignments."""
    patient = (
        db.query(models.Patient)
        .filter(models.Patient.patient_id == patient_id)
        .first()
    )
    if not patient:
        raise HTTPException(status_code=404, detail="Patient not found")

    # 1. Nullify patient_id in any Room referencing this patient
    db.query(models.Room).filter(models.Room.patient_id == patient_id).update(
        {models.Room.patient_id: None, models.Room.status: "normal"}
    )

    # 2. Delete the patient
    # Cascading deletes in models.py handle alerts, locations, access_logs
    db.delete(patient)
    db.commit()

    return {"success": True, "message": f"Patient {patient_id} deleted successfully"}


# ---------- Zone Management APIs ----------


@app.get("/api/v1/zones", response_model=List[schemas.ZoneDashboardOut])
def get_zones(
    include_inactive: bool = False,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> List[schemas.ZoneDashboardOut]:
    zones = ZoneManagementService.get_zones(db, include_inactive)
    result = []
    for z in zones:
        import json
        authorized_patients = []
        if z.authorized_patients_json:
            try:
                authorized_patients = json.loads(z.authorized_patients_json)
            except:
                pass
        
        result.append(schemas.ZoneDashboardOut(
            id=z.zone_code,
            name=z.zone_name,
            type=z.zone_type, # type: ignore
            description=None,
            maxOccupancy=z.max_occupancy,
            requireAuthorization=z.require_authorization,
            authorizedPatients=authorized_patients,
            authorizedRoles=[],
            alertLevel=z.alert_level if z.alert_level in ["info", "warning", "critical"] else "info", # type: ignore
            isActive=z.is_active,
            createdAt=z.created_at,
            updatedAt=z.created_at # Fallback
        ))
    return result


@app.post("/api/v1/zones", response_model=schemas.ZoneDashboardOut)
def create_zone(
    zone_data: schemas.ZoneCreate,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_admin),
) -> schemas.ZoneDashboardOut:
    # Basic implementation - reuse service but return dashboard schema
    # First, transform schema if needed (ZoneCreate has zone_code, etc)
    z = ZoneManagementService.create_zone(db, zone_data)
    return schemas.ZoneDashboardOut(
        id=z.zone_code,
        name=z.zone_name,
        type=z.zone_type, # type: ignore
        description=None,
        maxOccupancy=z.max_occupancy,
        requireAuthorization=z.require_authorization,
        authorizedPatients=[],
        authorizedRoles=[],
        alertLevel=z.alert_level if z.alert_level in ["info", "warning", "critical"] else "info", # type: ignore
        isActive=z.is_active,
        createdAt=z.created_at,
        updatedAt=z.created_at
    )


@app.get("/api/v1/zones/occupancy", response_model=List[schemas.ZoneOccupancyDashboard])
def get_all_zone_occupancy(
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> List[schemas.ZoneOccupancyDashboard]:
    zones = ZoneManagementService.get_zones(db)
    result = []
    for z in zones:
        occupancy_data = ZoneManagementService.get_zone_occupancy(db, z.zone_code)
        
        # Get patients currently in this zone
        patients_in_zone = db.query(models.Patient).filter(
            models.Patient.location == z.zone_code
        ).all()
        
        occupancy_patients = []
        for p in patients_in_zone:
            auth_check = ZoneManagementService.check_zone_access_authorization(db, p.patient_id, z.zone_code)
            occupancy_patients.append(schemas.ZoneOccupancyPatient(
                id=p.patient_id,
                name=p.name,
                entryTime=p.last_activity or datetime.utcnow(),
                authorized=auth_check["authorized"]
            ))
            
        result.append(schemas.ZoneOccupancyDashboard(
            zoneId=z.zone_code,
            zoneName=z.zone_name,
            currentOccupancy=occupancy_data["current_occupancy"],
            maxOccupancy=z.max_occupancy,
            patients=occupancy_patients
        ))
    return result


@app.get("/api/v1/zones/violations", response_model=List[schemas.ZoneViolationRecord])
def get_zone_violations(
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> List[schemas.ZoneViolationRecord]:
    alerts = db.query(models.Alert).filter(models.Alert.type == "zone_violation").all()
    violations = []
    for a in alerts:
        import json
        meta = {}
        if a.alert_metadata:
            try:
                meta = json.loads(a.alert_metadata)
            except:
                pass
                
        violations.append(schemas.ZoneViolationRecord(
            id=str(a.id),
            zoneId=meta.get("zone_code", "unknown"),
            zoneName=meta.get("zone_name", "Unknown Zone"),
            patientId=a.patient_id or "unknown",
            patientName=meta.get("patient_name", "Unknown Patient"),
            violationType=meta.get("violation_type", "unauthorized_access"),
            timestamp=a.created_at,
            resolved=a.status == "resolved",
            resolvedAt=a.resolved_at,
            resolvedBy=a.resolved_by
        ))
    return violations


# ---------- Ingestion APIs (wristband / receiver) ----------


@app.post("/api/v1/wristband-data")
async def ingest_wristband_data(
    payload: schemas.WristbandData, db: Session = Depends(get_db)
) -> Dict[str, Any]:
    """
    Endpoint for ESP32 wristbands to push vital signs and tamper / fall events.
    """
    patient = (
        db.query(models.Patient)
        .filter(models.Patient.patient_id == payload.patient_id)
        .first()
    )
    if not patient:
        # Minimal auto-registration so that simulations / test devices work out of the box
        patient = models.Patient(
            patient_id=payload.patient_id,
            name=f"Patient {payload.patient_id}",
            age=None,
            status="normal",
            room=None,
            location=None,
        )
        db.add(patient)
        db.flush()

    # Use server time for "last seen" so that dashboards stay correct even if
    # device clocks are wrong or send static timestamps.
    now = datetime.utcnow()

    # Update wristband state
    patient.battery_level = payload.battery_level
    patient.strap_intact = payload.sensors.tamper.strap_intact
    patient.skin_contact = payload.sensors.tamper.skin_contact
    patient.fall_detected = payload.alerts.fall
    patient.tamper_alert = payload.alerts.tamper
    patient.low_battery = payload.alerts.low_battery
    patient.last_activity = now

    # Derive high-level status
    if payload.alerts.tamper or payload.alerts.fall:
        patient.status = "anomaly"
    elif payload.alerts.low_battery:
        patient.status = "warning"
    else:
        # Keep "occupied" if they are assigned a room, otherwise "normal"
        patient.status = "occupied" if patient.room else "normal"

    db.add(patient)

    alerts_created: List[models.Alert] = []

    # Evaluate device status
    device_data = {
        'battery_level': payload.battery_level,
        'strap_intact': payload.sensors.tamper.strap_intact,
        'skin_contact': payload.sensors.tamper.skin_contact,
        'fall_detected': payload.alerts.fall
    }
    device_alerts = alert_engine.evaluate_device_status(db, payload.patient_id, device_data)
    
    # Create alerts using the enhanced engine
    for alert_data in device_alerts:
        alert_data['room'] = patient.room
        alert = alert_engine.create_alert(db, alert_data)
        if alert:
            alerts_created.append(alert)

    db.commit()

    # Push any new alerts to connected dashboards
    for alert in alerts_created:
        await ws_manager.broadcast(
            {
                "event": "alert_created",
                "data": {
                    "id": alert.alert_code,
                    "type": alert.type,
                    "title": alert.title,
                    "message": alert.message,
                    "timestamp": alert.created_at.isoformat(),
                    "patientId": alert.patient_id,
                    "room": alert.room,
                    "isRead": alert.is_read,
                },
            }
        )

    return {
        "success": True,
        "alerts_created": len(alerts_created),
        "timestamp": datetime.utcnow().isoformat(),
    }


@app.post("/api/v1/location-update")
async def ingest_location_update(
    payload: schemas.LocationUpdate, db: Session = Depends(get_db)
) -> Dict[str, Any]:
    """
    Endpoint for receiver nodes to report detected RFID tags and approximate locations.
    """
    # Use server time so that "last seen" stays accurate even if the device
    # sends a static or incorrect timestamp.
    now = datetime.utcnow()

    receiver = (
        db.query(models.Receiver)
        .filter(models.Receiver.receiver_id == payload.receiver_id)
        .first()
    )
    if not receiver:
        receiver = models.Receiver(
            receiver_id=payload.receiver_id,
            zone=payload.location.zone,
            room=payload.location.room,
            coord_x=payload.location.coordinates.x,
            coord_y=payload.location.coordinates.y,
            coord_z=payload.location.coordinates.z,
            zone_type="normal",
        )
        db.add(receiver)
        db.flush()

    receiver.last_seen = now
    if payload.receiver_status:
        receiver.uptime = payload.receiver_status.uptime
        receiver.wifi_strength = payload.receiver_status.wifi_strength

    locations_created: List[models.LocationHistory] = []
    logs_created: List[models.AccessLog] = []

    for tag in payload.detected_tags:
        # Resolve patient by ble_minor ID (BLE Beacon track)
        patient = (
            db.query(models.Patient)
            .filter(models.Patient.ble_minor == tag.ble_minor)
            .first()
        )

        if not patient:
            # Cannot track unknown beacons automatically without a minor ID match
            continue
            continue

        # Update patient location summary
        patient.room = payload.location.room if receiver.zone_type != "exit" else None
        patient.location = payload.location.room
        patient.last_activity = now
        if patient.room:
            # Set status to occupied if they are within a room and not in anomaly
            if patient.status == "normal":
                patient.status = "occupied"

        # Check for zone violations
        violation = ZoneManagementService.detect_zone_violation(
            db, patient.patient_id, payload.location.zone
        )
        if violation:
            # Use the AlertEngine to create / aggregate zone violation alerts and
            # avoid UNIQUE constraint errors on alerts.alert_code.
            severity = (
                "alert"
                if violation["alert_level"] in ["alert", "critical"]
                else "warning"
            )
            alert_data = {
                "type": "zone_violation",
                "severity": severity,
                "title": "Zone Violation",
                "message": (
                    f"Unauthorized access: {violation['patient_name']} "
                    f"entered {violation['zone_name']} ({violation['reason']})"
                ),
                "patient_id": patient.patient_id,
                "room": payload.location.room,
                "metadata": {
                    "zone_code": violation["zone_code"],
                    "zone_name": violation["zone_name"],
                    "violation_type": violation["violation_type"],
                    "reason": violation["reason"],
                },
            }
            alert = alert_engine.create_alert(db, alert_data)

            if alert:
                # Broadcast zone violation alert
                await ws_manager.broadcast(
                    {
                        "event": "zone_violation",
                        "data": {
                            "alert_id": alert.alert_code,
                            "patient_id": patient.patient_id,
                            "patient_name": violation["patient_name"],
                            "zone_code": violation["zone_code"],
                            "zone_name": violation["zone_name"],
                            "violation_type": violation["violation_type"],
                            "timestamp": now.isoformat(),
                        },
                    }
                )

        # Sync room occupancy if we know about the room
        room = (
            db.query(models.Room)
            .filter(models.Room.room_code == payload.location.room)
            .first()
        )
        if room:
            room.patient_id = patient.patient_id
            # Derive access log action based on zone violation check
            access_check = ZoneManagementService.check_zone_access_authorization(db, patient.patient_id, payload.location.zone)
            if not access_check["authorized"]:
                action = "denied"
            else:
                action = "entry"

            # Avoid spamming logs: only create a new entry if the last log
            # for this patient/room/action was more than 60 seconds ago.
            last_log = (
                db.query(models.AccessLog)
                .filter(
                    models.AccessLog.patient_id == patient.patient_id,
                    models.AccessLog.room == room.room_code,
                    models.AccessLog.action == action,
                )
                .order_by(models.AccessLog.timestamp.desc())
                .first()
            )
            should_create_log = True
            if last_log:
                delta = payload.timestamp - last_log.timestamp
                if delta.total_seconds() < 60:
                    should_create_log = False

            if should_create_log:
                log = models.AccessLog(
                    log_code=f"BLE-{receiver.receiver_id}-{patient.patient_id}-{int(payload.timestamp.timestamp())}",
                    patient_id=patient.patient_id,
                    patient_name=patient.name,
                    room=room.room_code,
                    action=action,
                    timestamp=now,
                    rfid_id=patient.rfid_uid or "BLE_ONLY",
                    duration_seconds=None,
                )
                db.add(log)
                logs_created.append(log)

        history = models.LocationHistory(
            patient_id=patient.patient_id,
            receiver_id=receiver.receiver_id,
            zone=payload.location.zone,
            room=payload.location.room,
            coord_x=payload.location.coordinates.x,
            coord_y=payload.location.coordinates.y,
            coord_z=payload.location.coordinates.z,
            estimated_distance=tag.estimated_distance,
            timestamp=now,
        )
        db.add(history)
        locations_created.append(history)

    db.commit()

    # We could broadcast movement events here as well if desired
    if locations_created:
        for loc in locations_created:
            await ws_manager.broadcast(
                {
                    "event": "location_update",
                    "data": {
                        "patientId": loc.patient_id,
                        "receiverId": loc.receiver_id,
                        "zone": loc.zone,
                        "room": loc.room,
                        "timestamp": loc.timestamp.isoformat(),
                    },
                }
            )

    return {
        "success": True,
        "message": "Data received",
        "locations_logged": len(locations_created),
        "access_logs_created": len(logs_created),
        "timestamp": datetime.utcnow().isoformat(),
    }


class DoorEvent(BaseModel):
    node_id: Optional[str] = "UNIFIED-A1"
    reader_id: str
    door_name: Optional[str] = None
    rfid_uid: str
    action: Literal["entry", "exit"]
    timestamp: Optional[datetime] = None


@app.post("/api/v1/door-event")
async def ingest_door_event(
    payload: DoorEvent, db: Session = Depends(get_db)
) -> Dict[str, Any]:
    """
    Endpoint for RFID scanners at doors to report entry/exit events.
    """
    now = datetime.utcnow()
    event_timestamp = payload.timestamp or now

    # 1. Resolve Door Info
    # Check our rfid_readers table first using node_id and reader_id
    reader = db.query(models.RFIDReader).filter(
        models.RFIDReader.node_id == payload.node_id,
        models.RFIDReader.reader_id == payload.reader_id
    ).first()

    door_name = payload.door_name
    if reader and reader.door_name:
        door_name = reader.door_name
    elif not door_name:
        # Fall back to receivers table or generic label
        receiver = db.query(models.Receiver).filter(models.Receiver.receiver_id == payload.reader_id).first()
        door_name = receiver.room if receiver else f"Door {payload.reader_id}"

    # Update reader's last seen and status when it sends an event
    if reader:
        reader.last_seen = now
        reader.status = "active"
        db.commit()

    # 2. Resolve Room matching the door mapping
    room = db.query(models.Room).filter(
        (models.Room.room_code == door_name) | (models.Room.name == door_name)
    ).first()
    door_code = room.room_code if room else door_name

    # 3. Resolve Patient
    patient = (
        db.query(models.Patient)
        .filter(models.Patient.rfid_uid == payload.rfid_uid)
        .first()
    )

    # 4. Always broadcast tag_scanned event for real-time notifications
    await ws_manager.broadcast({
        "event": "tag_scanned",
        "data": {
            "rfid_uid": payload.rfid_uid,
            "reader_id": payload.reader_id,
            "door_name": room.name if room else door_name,
            "patient_name": patient.name if patient else None,
            "patient_id": patient.patient_id if patient else None,
            "action": payload.action,
            "timestamp": event_timestamp.isoformat(),
            "status": "assigned" if patient else "unassigned"
        }
    })

    if not patient:
        # Keep unassigned broadcast for compatibility with existing UI
        await ws_manager.broadcast({
            "event": "tag_unassigned",
            "data": {
                "rfid_uid": payload.rfid_uid,
                "reader_id": payload.reader_id,
                "timestamp": event_timestamp.isoformat(),
                "message": f"Unassigned tag detected at {room.name if room else door_name}"
            }
        })
        return {"success": False, "message": "Unassigned tag detected", "rfid_uid": payload.rfid_uid, "buzzer_trigger": False}

    # 5. Symmetrical Toggle Logic & Buzzer triggering
    buzzer_trigger = False
    action_type = "entry"

    # Find the patient's assigned/admitted room from the rooms table
    assigned_room_rec = db.query(models.Room).filter(models.Room.patient_id == patient.patient_id).first()
    assigned_room_code = assigned_room_rec.room_code if assigned_room_rec else None

    if assigned_room_code:
        # Admitted patient is detected at any door -> immediately trigger buzzer
        buzzer_trigger = True

        if door_code == "ENTRY":
            # Symmetrical Toggle for WARD exit/entry via ENTRY gate
            if patient.location and patient.location != "Outside Ward":
                # Exit Ward Area
                patient.room = None
                patient.location = "Outside Ward"
                patient.status = "anomaly"
                action_type = "exit"

                alert_data = {
                    "type": "unauthorized_exit",
                    "severity": "critical",
                    "title": "Unauthorized Exit Alert",
                    "message": f"Patient {patient.name} (admitted to {assigned_room_rec.name}) has left the ward area.",
                    "patient_id": patient.patient_id,
                    "room": "Exit Door",
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
            else:
                # Entering Ward Area -> Goes into Corridor
                patient.room = "CORR"
                patient.location = "Corridor"
                patient.status = "warning"
                action_type = "entry"

                alert_data = {
                    "type": "unauthorized_movement",
                    "severity": "warning",
                    "title": "Unauthorized Movement Alert",
                    "message": f"Patient {patient.name} (admitted to {assigned_room_rec.name}) entered the ward area corridor.",
                    "patient_id": patient.patient_id,
                    "room": "Corridor",
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
        elif door_code == assigned_room_code:
            # Detected at the door to their assigned room
            if patient.room == assigned_room_code:
                # Exiting Room
                patient.room = "CORR"
                patient.location = "Corridor"
                patient.status = "warning"
                action_type = "exit"
                if assigned_room_rec:
                    assigned_room_rec.status = "normal"

                alert_data = {
                    "type": "room_exit",
                    "severity": "warning",
                    "title": "Room Exit Alert",
                    "message": f"Patient {patient.name} has left their assigned room ({assigned_room_rec.name}).",
                    "patient_id": patient.patient_id,
                    "room": door_code,
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
            else:
                # Entering assigned room back
                patient.room = assigned_room_code
                patient.location = assigned_room_rec.name
                patient.status = "normal"
                action_type = "entry"
                if assigned_room_rec:
                    assigned_room_rec.status = "occupied"

                alert_data = {
                    "type": "room_entry",
                    "severity": "info",
                    "title": "Room Entry Alert",
                    "message": f"Patient {patient.name} returned to their assigned room ({assigned_room_rec.name}).",
                    "patient_id": patient.patient_id,
                    "room": door_code,
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
        else:
            # Detected at another door (not ENTRY, and not their assigned room door)
            if patient.room == door_code:
                # Exiting the other room
                patient.room = "CORR"
                patient.location = "Corridor"
                patient.status = "warning"
                action_type = "exit"
                if room:
                    room.status = "normal"

                alert_data = {
                    "type": "unauthorized_movement",
                    "severity": "warning",
                    "title": "Unauthorized Movement Alert",
                    "message": f"Patient {patient.name} (admitted to {assigned_room_rec.name}) left room {room.name if room else door_code}.",
                    "patient_id": patient.patient_id,
                    "room": door_code,
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
            else:
                # Entering the other room
                patient.room = door_code
                patient.location = room.name if room else f"Room {door_code}"
                patient.status = "anomaly"
                action_type = "entry"
                if room:
                    room.status = "occupied"

                alert_data = {
                    "type": "unauthorized_access",
                    "severity": "high",
                    "title": "Unauthorized Access Alert",
                    "message": f"Patient {patient.name} (admitted to {assigned_room_rec.name}) entered room {room.name if room else door_code}.",
                    "patient_id": patient.patient_id,
                    "room": door_code,
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }

        # Create alert and broadcast
        alert = alert_engine.create_alert(db, alert_data)
        if alert:
            await ws_manager.broadcast({
                "event": "alert_created",
                "data": {
                    "id": alert.alert_code,
                    "type": alert.type,
                    "title": alert.title,
                    "message": alert.message,
                    "timestamp": alert.created_at.isoformat(),
                    "patientId": alert.patient_id,
                }
            })
    else:
        # Fallback to default logic (patient has no assigned room)
        if door_code == "ENTRY":
            # Symmetrical Toggle for WARD exit/entry via ENTRY gate
            if patient.location and patient.location != "Outside Ward":
                # Exit Ward Area
                patient.room = None
                patient.location = "Outside Ward"
                patient.status = "anomaly"
                action_type = "exit"
                buzzer_trigger = True  # Always buzz on ward egress

                alert_data = {
                    "type": "unauthorized_exit",
                    "severity": "critical",
                    "title": "Unauthorized Exit Alert",
                    "message": f"Patient {patient.name} has left the ward area.",
                    "patient_id": patient.patient_id,
                    "room": "Exit Door",
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
                alert = alert_engine.create_alert(db, alert_data)
                if alert:
                    await ws_manager.broadcast({
                        "event": "alert_created",
                        "data": {
                            "id": alert.alert_code,
                            "type": alert.type,
                            "title": alert.title,
                            "message": alert.message,
                            "timestamp": alert.created_at.isoformat(),
                            "patientId": alert.patient_id,
                        }
                    })
            else:
                # Entering Ward Area -> Goes into Corridor
                patient.room = "CORR"
                patient.location = "Corridor"
                patient.status = "normal"
                action_type = "entry"
        else:
            # Symmetrical Toggle for rooms/zones
            if patient.room == door_code:
                # Exiting Room -> Transition back to Corridor
                patient.room = "CORR"
                patient.location = "Corridor"
                patient.status = "normal"
                action_type = "exit"
                buzzer_trigger = True  # Patient got out of their assigned/occupied room!

                alert_data = {
                    "type": "room_exit",
                    "severity": "warning",
                    "title": "Room Exit Alert",
                    "message": f"Patient {patient.name} has left their assigned room ({room.name if room else door_code}).",
                    "patient_id": patient.patient_id,
                    "room": door_code,
                    "metadata": {"rfid_uid": payload.rfid_uid}
                }
                alert = alert_engine.create_alert(db, alert_data)
                if alert:
                    await ws_manager.broadcast({
                        "event": "alert_created",
                        "data": {
                            "id": alert.alert_code,
                            "type": alert.type,
                            "title": alert.title,
                            "message": alert.message,
                            "timestamp": alert.created_at.isoformat(),
                            "patientId": alert.patient_id,
                        }
                    })
            else:
                # Entering Room
                patient.room = door_code
                patient.location = room.name if room else f"Room {door_code}"
                action_type = "entry"

                # Check zone restrictions and authorization
                zone_code = "GENERAL"
                if door_code == "REST":
                    zone_code = "RESTRICTED"
                elif door_code == "ISO":
                    zone_code = "ISOLATION"

                access_check = ZoneManagementService.check_zone_access_authorization(db, patient.patient_id, zone_code)
                if not access_check["authorized"]:
                    patient.status = "anomaly"
                    alert_data = {
                        "type": "unauthorized_access",
                        "severity": "high",
                        "title": "Unauthorized Access Alert",
                        "message": f"Patient {patient.name} entered restricted zone: {room.name if room else door_code}.",
                        "patient_id": patient.patient_id,
                        "room": door_code,
                        "metadata": {"rfid_uid": payload.rfid_uid}
                    }
                    alert = alert_engine.create_alert(db, alert_data)
                    if alert:
                        await ws_manager.broadcast({
                            "event": "alert_created",
                            "data": {
                                "id": alert.alert_code,
                                "type": alert.type,
                                "title": alert.title,
                                "message": alert.message,
                                "timestamp": alert.created_at.isoformat(),
                                "patientId": alert.patient_id,
                            }
                        })
                else:
                    patient.status = "normal"

    # Log access history
    log = models.AccessLog(
        log_code=f"DOOR-{payload.reader_id}-{patient.patient_id}-{int(now.timestamp())}",
        patient_id=patient.patient_id,
        patient_name=patient.name,
        room=room.name if room else door_name,
        action=action_type,
        timestamp=now,
        rfid_id=payload.rfid_uid,
    )
    db.add(log)

    # Save to door_events table
    db_door_event = models.DoorEvent(
        node_id=payload.node_id or "UNIFIED-A1",
        reader_id=payload.reader_id,
        door_name=room.name if room else door_name,
        rfid_uid=payload.rfid_uid,
        patient_id=patient.patient_id,
        action=action_type,
        timestamp=event_timestamp
    )
    db.add(db_door_event)
    db.commit()

    return {
        "success": True, 
        "message": f"Door {action_type} logged symmetrically", 
        "door_name": room.name if room else door_name, 
        "buzzer_trigger": buzzer_trigger
    }


# --- Reader & Calibration Configuration APIs ---

@app.get("/api/v1/readers/{node_id}/config")
def get_node_reader_config(node_id: str, db: Session = Depends(get_db)):
    """Get reader configurations for an ESP32 node to download at startup."""
    readers = db.query(models.RFIDReader).filter(models.RFIDReader.node_id == node_id).all()
    return {
        "readers": [
            {
                "reader_id": r.reader_id,
                "door_name": r.door_name or f"Door {r.reader_id}"
            }
            for r in readers
        ]
    }


@app.post("/api/v1/readers/sync")
def sync_reader_config(payload: schemas.ReaderSyncRequest, db: Session = Depends(get_db)):
    """Accept and upsert reader configurations from ESP32 nodes."""
    try:
        reader = DoorConfigurationService.sync_reader_config(
            db=db,
            node_id=payload.node_id,
            reader_id=payload.reader_id,
            door_name=payload.door_name,
            gpio_pin=payload.gpio_pin
        )
        return {"success": True, "reader_id": reader.reader_id, "door_name": reader.door_name}
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))


@app.get("/api/v1/readers")
def list_all_readers(db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)):
    """List all registered readers across all nodes."""
    readers = db.query(models.RFIDReader).order_by(models.RFIDReader.node_id, models.RFIDReader.gpio_pin).all()
    return [
        {
            "node_id": r.node_id,
            "reader_id": r.reader_id,
            "gpio_pin": r.gpio_pin,
            "door_name": r.door_name,
            "status": r.status,
            "last_seen": r.last_seen.isoformat() if r.last_seen else None
        }
        for r in readers
    ]


@app.get("/api/v1/calibration/{node_id}")
def get_node_calibration(node_id: str, db: Session = Depends(get_db)):
    """Get calibration parameters for a node."""
    calib = db.query(models.DistanceCalibration).filter(models.DistanceCalibration.node_id == node_id).first()
    if calib:
        return {
            "reference_rssi": calib.reference_rssi,
            "path_loss_exponent": calib.path_loss_exponent,
            "environmental_factor": calib.environmental_factor
        }
    else:
        return {
            "reference_rssi": -59.0,
            "path_loss_exponent": 2.0,
            "environmental_factor": 1.0
        }


@app.post("/api/v1/calibration/{node_id}/sync")
def sync_node_calibration(node_id: str, payload: schemas.CalibrationSyncRequest, db: Session = Depends(get_db)):
    """Receive and save calibration parameters from an ESP32 node."""
    calib = db.query(models.DistanceCalibration).filter(models.DistanceCalibration.node_id == node_id).first()
    if calib:
        calib.reference_rssi = payload.reference_rssi
        calib.path_loss_exponent = payload.path_loss_exponent
        calib.environmental_factor = payload.environmental_factor
        calib.calibration_date = datetime.utcnow()
    else:
        calib = models.DistanceCalibration(
            node_id=node_id,
            reference_rssi=payload.reference_rssi,
            path_loss_exponent=payload.path_loss_exponent,
            environmental_factor=payload.environmental_factor,
            calibration_date=datetime.utcnow()
        )
        db.add(calib)
    db.commit()
    return {"success": True}



@app.post("/api/v1/patients/assign")
async def assign_patient(
    payload: schemas.PatientAssignment, db: Session = Depends(get_db)
) -> Dict[str, Any]:
    """
    Endpoint for wristbands to register/assign a patient.
    If the tag (ble_minor) or RFID was previously assigned, move the old patient(s) to history.
    """
    now = datetime.utcnow()
    
    # Resolve Room Info
    room_code = payload.ward
    room = db.query(models.Room).filter(models.Room.room_code == room_code).first()
    room_name = room.name if room else f"Room {room_code}" if room_code else "Unassigned Room"

    # Find any other patient who is currently assigned to this tag or RFID
    conflicting_patients = db.query(models.Patient).filter(
        (models.Patient.patient_id != payload.patient_id) & (
            (models.Patient.ble_minor == payload.ble_minor) | 
            (models.Patient.rfid_uid == payload.rfid_uid)
        )
    ).all()

    for p in conflicting_patients:
        # Move conflicting patient's assignment to history
        history = models.PatientAssignmentHistory(
            patient_id=p.patient_id,
            name=p.name,
            ward=p.room,
            ble_minor=p.ble_minor,
            rfid_uid=p.rfid_uid,
            assigned_at=p.last_activity or now, 
        )
        db.add(history)
        
        # Deassign conflicting patient
        p.ble_minor = None
        p.rfid_uid = None
        p.room = None
        p.location = None
        p.status = "normal"
        
        # Unlink conflicting patient from any Room
        db.query(models.Room).filter(models.Room.patient_id == p.patient_id).update(
            {models.Room.patient_id: None, models.Room.status: "normal"}
        )

    # Flush changes to database to clear unique constraints (set to NULL) before assigning to target patient
    db.flush()

    # Check if the target patient already exists
    patient = db.query(models.Patient).filter(models.Patient.patient_id == payload.patient_id).first()

    if patient:
        # Move target patient's old assignment to history (if it was active/different)
        if patient.ble_minor or patient.rfid_uid or patient.room:
            history = models.PatientAssignmentHistory(
                patient_id=patient.patient_id,
                name=patient.name,
                ward=patient.room,
                ble_minor=patient.ble_minor,
                rfid_uid=patient.rfid_uid,
                assigned_at=patient.last_activity or now, 
            )
            db.add(history)

        # Update target patient
        patient.name = payload.name
        patient.room = room_code
        patient.location = room_name
        patient.ble_minor = payload.ble_minor
        patient.rfid_uid = payload.rfid_uid
        patient.last_activity = now
        patient.status = "occupied" if room_code else "normal"
    else:
        # Create new patient record
        patient = models.Patient(
            patient_id=payload.patient_id,
            name=payload.name,
            room=room_code,
            location=room_name,
            ble_minor=payload.ble_minor,
            rfid_uid=payload.rfid_uid,
            status="occupied" if room_code else "normal",
            last_activity=now
        )
        db.add(patient)

    # Unlink target patient from any other Room they might have been in
    db.query(models.Room).filter(models.Room.patient_id == payload.patient_id).update(
        {models.Room.patient_id: None, models.Room.status: "normal"}
    )

    # Link target patient to the new Room
    if room:
        room.patient_id = payload.patient_id
        room.status = "occupied"

    db.commit()
    
    # Broadcast update to dashboard
    await ws_manager.broadcast({
        "event": "patient_assigned",
        "data": {
            "patient_id": payload.patient_id,
            "name": payload.name,
            "tag_id": payload.ble_minor,
            "room": room_code,
            "location": room_name
        }
    })
    
    return {"success": True, "message": f"Patient {payload.name} assigned to tag {payload.ble_minor}"}


@app.post("/api/v1/patients/deassign")
async def deassign_patient(
    payload: Dict[str, str], db: Session = Depends(get_db)
) -> Dict[str, Any]:
    """
    Endpoint for wristbands to deassign/delete a patient by patient_id.
    """
    patient_id = payload.get("patient_id")
    if not patient_id:
        raise HTTPException(status_code=400, detail="patient_id is required")
        
    patient = db.query(models.Patient).filter(models.Patient.patient_id == patient_id).first()
    if not patient:
        raise HTTPException(status_code=404, detail="Patient not found")
        
    # Nullify patient_id in any Room referencing this patient
    db.query(models.Room).filter(models.Room.patient_id == patient_id).update(
        {models.Room.patient_id: None, models.Room.status: "normal"}
    )
    
    # Broadcast websocket delete event
    await ws_manager.broadcast({
        "event": "patient_deleted",
        "data": {"patient_id": patient_id}
    })
    
    # Delete the patient
    db.delete(patient)
    db.commit()
    
    return {"success": True, "message": f"Patient {patient_id} deleted successfully"}




@app.post("/api/v1/auth/login", response_model=schemas.TokenResponse)
def login(user_credentials: schemas.UserLogin, db: Session = Depends(get_db)) -> schemas.TokenResponse:
    """Authenticate user and return JWT tokens."""
    user = AuthenticationService.authenticate_user(db, user_credentials.username, user_credentials.password)
    if not user:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid username or password, or account is locked",
            headers={"WWW-Authenticate": "Bearer"},
        )
    
    # Create tokens with embedded user context so the frontend can derive the
    # logged-in user directly from the JWT payload.
    user_claims = {
        "sub": user.username,
        "username": user.username,
        "full_name": user.full_name,
        "email": user.email,
        "role": user.role,
    }
    access_token = AuthenticationService.create_access_token(data=user_claims)
    refresh_token = AuthenticationService.create_refresh_token(data=user_claims)
    
    return schemas.TokenResponse(
        access_token=access_token,
        refresh_token=refresh_token,
        expires_in=1800,  # 30 minutes
        user=schemas.UserOut.from_orm(user)
    )


@app.post("/api/v1/auth/refresh", response_model=schemas.TokenResponse)
def refresh_token(refresh_request: schemas.RefreshTokenRequest, db: Session = Depends(get_db)) -> schemas.TokenResponse:
    """Refresh access token using refresh token."""
    payload = AuthenticationService.verify_token(refresh_request.refresh_token, "refresh")
    if not payload:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid refresh token"
        )
    
    username = payload.get("sub")
    user = db.query(models.User).filter(models.User.username == username).first()
    if not user or not user.is_active:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User not found or inactive"
        )
    
    # Create new tokens
    user_claims = {
        "sub": user.username,
        "username": user.username,
        "full_name": user.full_name,
        "email": user.email,
        "role": user.role,
    }
    access_token = AuthenticationService.create_access_token(data=user_claims)
    new_refresh_token = AuthenticationService.create_refresh_token(data=user_claims)
    
    return schemas.TokenResponse(
        access_token=access_token,
        refresh_token=new_refresh_token,
        expires_in=1800,  # 30 minutes
        user=schemas.UserOut.from_orm(user)
    )


@app.post("/api/v1/auth/logout")
def logout(current_user: models.User = Depends(require_any_role)) -> Dict[str, Any]:
    """Logout user (client should discard tokens)."""
    return {"success": True, "message": "Logged out successfully"}


# ---------- User Management APIs (Admin only) ----------


@app.post("/api/v1/users", response_model=schemas.UserOut)
def create_user(user_data: schemas.UserCreate, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> schemas.UserOut:
    """Create a new user (admin only)."""
    # Check if username already exists
    existing_user = db.query(models.User).filter(models.User.username == user_data.username).first()
    if existing_user:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Username already exists"
        )
    
    # Create new user
    hashed_password = AuthenticationService.get_password_hash(user_data.password)
    new_user = models.User(
        username=user_data.username,
        password_hash=hashed_password,
        role=user_data.role,
        full_name=user_data.full_name,
        email=user_data.email
    )
    
    db.add(new_user)
    db.commit()
    db.refresh(new_user)
    
    return schemas.UserOut.from_orm(new_user)


@app.get("/api/v1/users", response_model=List[schemas.UserOut])
def get_users(db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> List[schemas.UserOut]:
    """Get all users (admin only)."""
    users = db.query(models.User).all()
    return [schemas.UserOut.from_orm(user) for user in users]


@app.get("/api/v1/users/{user_id}", response_model=schemas.UserOut)
def get_user(user_id: int, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> schemas.UserOut:
    """Get user by ID (admin only)."""
    user = db.query(models.User).filter(models.User.id == user_id).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    return schemas.UserOut.from_orm(user)


@app.put("/api/v1/users/{user_id}", response_model=schemas.UserOut)
def update_user(user_id: int, user_data: schemas.UserUpdate, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> schemas.UserOut:
    """Update user (admin only)."""
    user = db.query(models.User).filter(models.User.id == user_id).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    
    # Update fields if provided
    if user_data.full_name is not None:
        user.full_name = user_data.full_name
    if user_data.email is not None:
        user.email = user_data.email
    if user_data.role is not None:
        user.role = user_data.role
    if user_data.is_active is not None:
        user.is_active = user_data.is_active
    
    db.commit()
    db.refresh(user)
    
    return schemas.UserOut.from_orm(user)


@app.delete("/api/v1/users/{user_id}")
def delete_user(user_id: int, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> Dict[str, Any]:
    """Delete user (admin only)."""
    user = db.query(models.User).filter(models.User.id == user_id).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    
    # Prevent deleting the last admin
    if user.role == "admin":
        admin_count = db.query(models.User).filter(models.User.role == "admin", models.User.is_active == True).count()
        if admin_count <= 1:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Cannot delete the last admin user"
            )
    
    db.delete(user)
    db.commit()
    
    return {"success": True, "message": "User deleted successfully"}


@app.get("/api/v1/auth/me", response_model=schemas.UserOut)
def get_current_user_info(current_user: models.User = Depends(require_any_role)) -> schemas.UserOut:
    """Get current user information."""
    return schemas.UserOut.from_orm(current_user)


# ---------- Zone Management APIs ----------


@app.post("/api/v1/zones", response_model=schemas.ZoneOut)
def create_zone(zone_data: schemas.ZoneCreate, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> schemas.ZoneOut:
    """Create a new zone (admin only)."""
    zone = ZoneManagementService.create_zone(db, zone_data)
    return schemas.ZoneOut.from_orm(zone)


@app.get("/api/v1/zones", response_model=List[schemas.ZoneDashboardOut])
def get_zones(
    include_inactive: bool = False,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> List[schemas.ZoneDashboardOut]:
    """
    Get all zones in a shape tailored for the Zone Management dashboard.

    - ``id`` is the public zone code used by the frontend.
    - ``authorizedPatients`` is derived from each patient's ``authorized_zones``.
    - ``authorizedRoles`` is currently informational and not enforced server-side.
    """
    zones = ZoneManagementService.get_zones(db, include_inactive)

    # Build a mapping of zone_code -> [patient_id, ...] from patient.authorized_zones
    import json
    from collections import defaultdict

    authorized_by_zone: Dict[str, List[str]] = defaultdict(list)
    patients = db.query(models.Patient.patient_id, models.Patient.authorized_zones).all()
    for pid, raw_zones in patients:
        if not raw_zones:
            continue
        try:
            codes = json.loads(raw_zones)
            if not isinstance(codes, list):
                continue
        except Exception:
            continue
        for code in codes:
            if isinstance(code, str):
                authorized_by_zone[code].append(pid)

    results: List[schemas.ZoneDashboardOut] = []
    for z in zones:
        coordinates = None
        if z.coordinates_json:
            try:
                coordinates = schemas.ZoneCoordinates(**json.loads(z.coordinates_json))
            except Exception:
                coordinates = None

        # Map DB alert levels to the UI's simplified palette.
        db_level = (z.alert_level or "info").lower()
        if db_level not in {"info", "warning", "critical"}:
            alert_level = "critical"
        else:
            alert_level = db_level  # type: ignore[assignment]

        # Simple, opinionated mapping from zone type to suggested roles.
        if z.zone_type == "restricted":
            authorized_roles = ["admin", "doctor"]
        elif z.zone_type == "isolation":
            authorized_roles = ["admin", "doctor", "nurse"]
        elif z.zone_type == "exit":
            authorized_roles = ["admin", "nurse"]
        else:
            authorized_roles = []

        results.append(
            schemas.ZoneDashboardOut(
                id=z.zone_code,
                name=z.zone_name,
                type=z.zone_type,  # type: ignore[arg-type]
                description=None,
                maxOccupancy=z.max_occupancy,
                requireAuthorization=z.require_authorization,
                authorizedPatients=authorized_by_zone.get(z.zone_code, []),
                authorizedRoles=authorized_roles,
                alertLevel=alert_level,  # type: ignore[arg-type]
                isActive=z.is_active,
                coordinates=coordinates,
                createdAt=z.created_at,
                updatedAt=z.created_at,
            )
        )

    # Sort by name for deterministic ordering.
    return sorted(results, key=lambda z: z.name)


@app.get("/api/v1/zones/{zone_code}", response_model=schemas.ZoneOut)
def get_zone(zone_code: str, db: Session = Depends(get_db), current_user: models.User = Depends(require_any_role)) -> schemas.ZoneOut:
    """Get zone by code."""
    zone = ZoneManagementService.get_zone_by_code(db, zone_code)
    if not zone:
        raise HTTPException(status_code=404, detail="Zone not found")
    return schemas.ZoneOut.from_orm(zone)


@app.put("/api/v1/zones/{zone_code}", response_model=schemas.ZoneOut)
def update_zone(zone_code: str, zone_data: schemas.ZoneUpdate, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> schemas.ZoneOut:
    """Update zone (admin only)."""
    zone = ZoneManagementService.update_zone(db, zone_code, zone_data)
    return schemas.ZoneOut.from_orm(zone)


@app.delete("/api/v1/zones/{zone_code}")
def delete_zone(zone_code: str, db: Session = Depends(get_db), current_user: models.User = Depends(require_admin)) -> Dict[str, Any]:
    """Delete zone (admin only)."""
    ZoneManagementService.delete_zone(db, zone_code)
    return {"success": True, "message": "Zone deleted successfully"}


@app.put("/api/v1/patients/{patient_id}/zone-restrictions")
def assign_patient_zone_restrictions(
    patient_id: str, 
    restrictions: schemas.PatientZoneRestrictions, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_staff)
) -> Dict[str, Any]:
    """Assign zone restrictions to a patient (staff only)."""
    patient = ZoneManagementService.assign_patient_zone_restrictions(db, patient_id, restrictions)
    return {"success": True, "message": "Zone restrictions updated successfully", "patient_id": patient.patient_id}


@app.get("/api/v1/zones/{zone_code}/access-check/{patient_id}", response_model=schemas.ZoneAccessCheck)
def check_zone_access(
    zone_code: str, 
    patient_id: str, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> schemas.ZoneAccessCheck:
    """Check if a patient is authorized to access a zone."""
    result = ZoneManagementService.check_zone_access_authorization(db, patient_id, zone_code)
    return schemas.ZoneAccessCheck(**result)


@app.get("/api/v1/zones/{zone_code}/occupancy", response_model=schemas.ZoneOccupancyOut)
def get_zone_occupancy(
    zone_code: str, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> schemas.ZoneOccupancyOut:
    """Get current zone occupancy information."""
    occupancy = ZoneManagementService.get_zone_occupancy(db, zone_code)
    return schemas.ZoneOccupancyOut(**occupancy)


@app.post("/api/v1/zones/{zone_code}/patients/{patient_id}")
def assign_patient_to_zone(
    zone_code: str,
    patient_id: str,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_staff),
) -> Dict[str, Any]:
    """
    Grant a patient access to a zone by adding the zone code to
    ``Patient.authorized_zones``.
    """
    import json

    zone = ZoneManagementService.get_zone_by_code(db, zone_code)
    if not zone:
        raise HTTPException(status_code=404, detail="Zone not found")

    patient = (
        db.query(models.Patient)
        .filter(models.Patient.patient_id == patient_id)
        .first()
    )
    if not patient:
        raise HTTPException(status_code=404, detail="Patient not found")

    codes: List[str] = []
    if patient.authorized_zones:
        try:
            loaded = json.loads(patient.authorized_zones)
            if isinstance(loaded, list):
                codes = [str(c) for c in loaded]
        except Exception:
            codes = []

    if zone.zone_code not in codes:
        codes.append(zone.zone_code)
        patient.authorized_zones = json.dumps(codes)
        db.add(patient)
        db.commit()

    return {
        "success": True,
        "message": "Zone access granted",
        "patient_id": patient.patient_id,
        "authorized_zones": codes,
    }


@app.delete("/api/v1/zones/{zone_code}/patients/{patient_id}")
def revoke_patient_from_zone(
    zone_code: str,
    patient_id: str,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_staff),
) -> Dict[str, Any]:
    """
    Revoke a patient's access to a zone by removing the zone code from
    ``Patient.authorized_zones``.
    """
    import json

    zone = ZoneManagementService.get_zone_by_code(db, zone_code)
    if not zone:
        raise HTTPException(status_code=404, detail="Zone not found")

    patient = (
        db.query(models.Patient)
        .filter(models.Patient.patient_id == patient_id)
        .first()
    )
    if not patient:
        raise HTTPException(status_code=404, detail="Patient not found")

    codes: List[str] = []
    if patient.authorized_zones:
        try:
            loaded = json.loads(patient.authorized_zones)
            if isinstance(loaded, list):
                codes = [str(c) for c in loaded]
        except Exception:
            codes = []

    if zone.zone_code in codes:
        codes = [c for c in codes if c != zone.zone_code]
        patient.authorized_zones = json.dumps(codes) if codes else None
        db.add(patient)
        db.commit()

    return {
        "success": True,
        "message": "Zone access revoked",
        "patient_id": patient.patient_id,
        "authorized_zones": codes,
    }


@app.get("/api/v1/zones/occupancy", response_model=List[schemas.ZoneOccupancyDashboard])
def get_all_zone_occupancy(
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> List[schemas.ZoneOccupancyDashboard]:
    """
    Aggregate, dashboard-friendly view of zone occupancy.

    This endpoint groups patients into high-level zones (general, restricted,
    isolation, exit) using simple heuristics based on room names. It is
    designed specifically for the Zone Management UI rather than for
    audit‑grade reporting.
    """
    zones = db.query(models.Zone).filter(models.Zone.is_active == True).all()
    zone_by_type: Dict[str, models.Zone] = {}
    for z in zones:
        # Prefer the first zone of a given type as the canonical one for the
        # dashboard (e.g. GENERAL, RESTRICTED, ISOLATION, EXIT).
        zone_by_type.setdefault(z.zone_type, z)

    def _pick_zone_for_room(room: models.Room) -> Optional[models.Zone]:
        name = (room.name or room.room_code or "").lower()
        if "isolation" in name:
            return zone_by_type.get("isolation") or zone_by_type.get("ISOLATION")
        if "restricted" in name:
            return zone_by_type.get("restricted") or zone_by_type.get("RESTRICTED")
        if "entry" in name or "exit" in name:
            return zone_by_type.get("exit") or zone_by_type.get("EXIT")
        if "wash" in name or "toilet" in name or "wc" in name:
            # Treat washroom as part of the isolation cluster if present,
            # otherwise fall back to the general zone.
            return (
                zone_by_type.get("isolation")
                or zone_by_type.get("ISOLATION")
                or zone_by_type.get("normal")
                or zone_by_type.get("GENERAL")
            )
        # Default to the main general ward area.
        return (
            zone_by_type.get("normal")
            or zone_by_type.get("GENERAL")
            or next(iter(zones), None)
        )

    # Seed one dashboard record per active zone.
    dashboard: Dict[str, schemas.ZoneOccupancyDashboard] = {}
    for z in zones:
        dashboard[z.zone_code] = schemas.ZoneOccupancyDashboard(
            zoneId=z.zone_code,
            zoneName=z.zone_name,
            currentOccupancy=0,
            maxOccupancy=z.max_occupancy,
        )

    patients = db.query(models.Patient).all()
    rooms_by_code: Dict[str, models.Room] = {
        r.room_code: r for r in db.query(models.Room).all()
    }

    for patient in patients:
        if not patient.room:
            continue
        room = rooms_by_code.get(patient.room)
        if not room:
            continue
        zone = _pick_zone_for_room(room)
        if not zone:
            continue

        # Check authorization for this zone/patient pair.
        access = ZoneManagementService.check_zone_access_authorization(
            db, patient.patient_id, zone.zone_code
        )
        authorized = bool(access.get("authorized"))

        # Ensure a dashboard bucket exists (in case zone was not active when
        # we initialised the mapping above).
        bucket = dashboard.get(zone.zone_code)
        if not bucket:
            bucket = schemas.ZoneOccupancyDashboard(
                zoneId=zone.zone_code,
                zoneName=zone.zone_name,
                currentOccupancy=0,
                maxOccupancy=zone.max_occupancy,
            )
            dashboard[zone.zone_code] = bucket

        bucket.currentOccupancy += 1
        bucket.patients.append(
            schemas.ZoneOccupancyPatient(
                id=patient.patient_id,
                name=patient.name,
                entryTime=patient.last_activity or datetime.utcnow(),
                authorized=authorized,
            )
        )

    # Return zones sorted by name for a stable UI.
    return sorted(dashboard.values(), key=lambda z: z.zoneName)


@app.get("/api/v1/zones/violations", response_model=List[schemas.ZoneViolationRecord])
def get_zone_violations(
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> List[schemas.ZoneViolationRecord]:
    """
    Return a dashboard-friendly list of recent zone violations.

    Violations are derived from alerts created by the location ingestion
    pipeline with ``type == "zone_violation"`` (or a matching title) and the
    metadata stored in ``Alert.alert_metadata``.
    """
    alerts = (
        db.query(models.Alert)
        .filter(models.Alert.type == "zone_violation")
        .order_by(models.Alert.created_at.desc())
        .limit(200)
        .all()
    )

    records: List[schemas.ZoneViolationRecord] = []
    for alert in alerts:
        meta: Dict[str, Any] = {}
        if alert.alert_metadata:
            try:
                import json

                meta = json.loads(alert.alert_metadata)
            except Exception:
                meta = {}

        zone_code = meta.get("zone_code") or (alert.room or "UNKNOWN")
        zone = ZoneManagementService.get_zone_by_code(db, zone_code) if zone_code else None
        patient = (
            db.query(models.Patient)
            .filter(models.Patient.patient_id == alert.patient_id)
            .first()
            if alert.patient_id
            else None
        )

        records.append(
            schemas.ZoneViolationRecord(
                id=alert.alert_code,
                zoneId=zone_code,
                zoneName=meta.get("zone_name") or (zone.zone_name if zone else "Unknown zone"),
                patientId=alert.patient_id or "UNKNOWN",
                patientName=patient.name if patient else (alert.patient_id or "Unknown patient"),
                violationType=meta.get("violation_type", "unauthorized_entry"),
                timestamp=alert.created_at,
                resolved=alert.status == "resolved",
                resolvedAt=alert.resolved_at,
                resolvedBy=alert.resolved_by,
            )
        )

    return records


@app.post("/api/v1/zones/violations/{violation_id}/resolve")
def resolve_zone_violation(
    violation_id: str,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_staff),
) -> Dict[str, Any]:
    """
    Resolve a zone violation by resolving the underlying alert.

    The ``violation_id`` corresponds to the ``alert_code`` for the
    ``zone_violation`` alert created by the ingestion pipeline.
    """
    alert = (
        db.query(models.Alert)
        .filter(models.Alert.alert_code == violation_id)
        .first()
    )
    if not alert:
        raise HTTPException(status_code=404, detail="Violation not found")

    if alert.status != "resolved":
        alert.status = "resolved"
        alert.resolved_at = datetime.utcnow()
        alert.resolved_by = current_user.username
        db.add(alert)
        db.commit()

    return {
        "success": True,
        "message": "Zone violation resolved",
        "violation_id": violation_id,
    }


# ---------- System Monitoring APIs ----------


@app.get("/api/v1/system/health")
def get_system_health(current_user: models.User = Depends(require_any_role)) -> Dict[str, Any]:
    """Get comprehensive system health metrics."""
    return SystemMonitor.get_system_health()


@app.get("/api/v1/system/logs")
def get_system_logs(
    lines: int = 100, 
    level: str = "INFO", 
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Get recent system logs (admin only)."""
    try:
        import os
        log_file = "halo_watch.log"
        
        if not os.path.exists(log_file):
            return {"logs": [], "message": "Log file not found"}
        
        with open(log_file, 'r') as f:
            all_lines = f.readlines()
        
        # Get last N lines
        recent_lines = all_lines[-lines:] if len(all_lines) > lines else all_lines
        
        # Filter by log level if specified
        if level != "ALL":
            filtered_lines = [line for line in recent_lines if level in line]
            recent_lines = filtered_lines
        
        return {
            "logs": [line.strip() for line in recent_lines],
            "total_lines": len(recent_lines),
            "log_level_filter": level
        }
    except Exception as e:
        ErrorHandler.log_error(e)
        raise HTTPException(status_code=500, detail=f"Error reading logs: {str(e)}")


@app.post("/api/v1/system/backup")
def create_system_backup(current_user: models.User = Depends(require_admin)) -> Dict[str, Any]:
    """Create a system backup (admin only)."""
    try:
        result = SystemMonitor.create_backup()
        if not result["success"]:
            raise HTTPException(status_code=500, detail=result["error"])
        return result
    except Exception as e:
        ErrorHandler.log_error(e)
        raise HTTPException(status_code=500, detail=f"Backup failed: {str(e)}")


@app.post("/api/v1/system/cleanup")
def cleanup_old_data(
    days_to_keep: int = 90, 
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Clean up old data (admin only)."""
    try:
        if days_to_keep < 7:
            raise HTTPException(status_code=400, detail="Cannot keep data for less than 7 days")
        
        result = SystemMonitor.cleanup_old_data(days_to_keep)
        if not result["success"]:
            raise HTTPException(status_code=500, detail=result["error"])
        return result
    except Exception as e:
        ErrorHandler.log_error(e)
        raise HTTPException(status_code=500, detail=f"Cleanup failed: {str(e)}")


@app.get("/api/v1/system/status")
def get_system_status() -> Dict[str, Any]:
    """Get basic system status (no authentication required for health checks)."""
    try:
        db = SessionLocal()
        try:
            # Simple database connectivity test
            db.execute(text("SELECT 1"))
            db_status = "connected"
        except Exception:
            db_status = "disconnected"
        finally:
            db.close()
        
        return {
            "status": "online",
            "timestamp": datetime.utcnow().isoformat(),
            "database": db_status,
            "version": "1.0.0"
        }
    except Exception as e:
        return {
            "status": "error",
            "timestamp": datetime.utcnow().isoformat(),
            "error": str(e)
        }



# ---------- Analytics and Reporting APIs ----------

from .analytics import MovementAnalytics, AlertAnalytics, SystemPerformanceAnalytics, ReportingSystem

movement_analytics = MovementAnalytics()
alert_analytics = AlertAnalytics()
performance_analytics = SystemPerformanceAnalytics()
reporting_system = ReportingSystem()


@app.get("/api/v1/analytics/movement/{patient_id}")
def get_patient_movement_analysis(
    patient_id: str, 
    days: int = 7, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get patient movement pattern analysis."""
    pattern = movement_analytics.analyze_patient_movement_patterns(db, patient_id, days)
    zone_analysis = movement_analytics.calculate_zone_dwell_analysis(db, patient_id, days)
    activity_score = movement_analytics.calculate_patient_activity_score(db, patient_id, days)
    
    return {
        "patient_id": patient_id,
        "analysis_period_days": days,
        "movement_pattern": {
            "total_movements": pattern.total_movements,
            "unique_zones": pattern.unique_zones,
            "average_dwell_time": pattern.average_dwell_time,
            "movement_frequency": pattern.movement_frequency,
            "most_visited_zone": pattern.most_visited_zone,
            "activity_score": pattern.activity_score,
            "abnormal_patterns": pattern.abnormal_patterns
        },
        "zone_analysis": [
            {
                "zone": z.zone,
                "total_time_minutes": z.total_time_minutes,
                "visit_count": z.visit_count,
                "average_dwell_minutes": z.average_dwell_minutes,
                "longest_stay_minutes": z.longest_stay_minutes,
                "shortest_stay_minutes": z.shortest_stay_minutes
            } for z in zone_analysis
        ],
        "activity_metrics": activity_score
    }


@app.get("/api/v1/analytics/movement/abnormal/{patient_id}")
def detect_abnormal_movement_patterns(
    patient_id: str, 
    hours: int = 24, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Detect abnormal movement patterns for a patient."""
    patterns = movement_analytics.detect_abnormal_movement_patterns(db, patient_id, hours)
    return {
        "patient_id": patient_id,
        "analysis_period_hours": hours,
        "abnormal_patterns": patterns
    }


@app.get("/api/v1/analytics/alerts/trends")
def get_alert_trends(
    days: int = 30, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get alert trend analysis."""
    return alert_analytics.analyze_alert_trends(db, days)


@app.get("/api/v1/analytics/alerts/false-positives")
def detect_false_positive_alerts(
    alert_type: str = None, 
    days: int = 30, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_staff)
) -> Dict[str, Any]:
    """Detect potential false positive alerts."""
    return alert_analytics.detect_false_positives(db, alert_type, days)


@app.get("/api/v1/analytics/alerts/effectiveness")
def get_alert_effectiveness(
    days: int = 30, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get alert effectiveness analysis."""
    return alert_analytics.analyze_alert_effectiveness(db, days)


@app.get("/api/v1/analytics/alerts/escalation")
def get_escalation_patterns(
    days: int = 30, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get alert escalation pattern analysis."""
    return alert_analytics.analyze_escalation_patterns(db, days)


@app.get("/api/v1/analytics/system/device-health")
def get_device_health_analysis(
    hours: int = 24, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get device health analysis and predictive maintenance."""
    return performance_analytics.analyze_device_health(db, hours)


@app.get("/api/v1/analytics/system/network-performance")
def get_network_performance_analysis(
    hours: int = 24, 
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get network performance analysis."""
    return performance_analytics.analyze_network_performance(db, hours)


@app.get("/api/v1/analytics/system/database-performance")
def get_database_performance_analysis(
    db: Session = Depends(get_db), 
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Get database performance analysis (admin only)."""
    return performance_analytics.analyze_database_performance(db)


@app.get("/api/v1/analytics/system/resources")
def get_system_resource_monitoring(
    current_user: models.User = Depends(require_any_role)
) -> Dict[str, Any]:
    """Get system resource monitoring."""
    return performance_analytics.monitor_system_resources()


@app.get("/api/v1/reports/patient-movement")
def generate_patient_movement_report(
    patient_id: str = None,
    start_date: str = None,
    end_date: str = None,
    format: str = "json",
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role)
) -> Any:
    """Generate patient movement report."""
    start_dt = datetime.fromisoformat(start_date) if start_date else None
    end_dt = datetime.fromisoformat(end_date) if end_date else None
    
    report_data = reporting_system.generate_patient_movement_report(
        db, patient_id, start_dt, end_dt
    )
    
    if format.lower() == "csv":
        from fastapi.responses import Response
        csv_content = reporting_system.export_report_to_csv(report_data)
        return Response(
            content=csv_content,
            media_type="text/csv",
            headers={"Content-Disposition": "attachment; filename=patient_movement_report.csv"}
        )
    
    return report_data


@app.get("/api/v1/reports/alert-summary")
def generate_alert_summary_report(
    start_date: str = None,
    end_date: str = None,
    format: str = "json",
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role)
) -> Any:
    """Generate alert summary report."""
    start_dt = datetime.fromisoformat(start_date) if start_date else None
    end_dt = datetime.fromisoformat(end_date) if end_date else None
    
    report_data = reporting_system.generate_alert_summary_report(db, start_dt, end_dt)
    
    if format.lower() == "csv":
        from fastapi.responses import Response
        csv_content = reporting_system.export_report_to_csv(report_data)
        return Response(
            content=csv_content,
            media_type="text/csv",
            headers={"Content-Disposition": "attachment; filename=alert_summary_report.csv"}
        )
    
    return report_data


@app.get("/api/v1/reports/system-performance")
def generate_system_performance_report(
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_staff)
) -> Dict[str, Any]:
    """Generate system performance report."""
    return reporting_system.generate_system_performance_report(db)


@app.get("/api/v1/reports/zone-occupancy")
def generate_zone_occupancy_report(
    start_date: str = None,
    end_date: str = None,
    format: str = "json",
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role)
) -> Any:
    """Generate zone occupancy report."""
    start_dt = datetime.fromisoformat(start_date) if start_date else None
    end_dt = datetime.fromisoformat(end_date) if end_date else None
    
    report_data = reporting_system.generate_zone_occupancy_report(db, start_dt, end_dt)
    
    if format.lower() == "csv":
        from fastapi.responses import Response
        csv_content = reporting_system.export_report_to_csv(report_data)
        return Response(
            content=csv_content,
            media_type="text/csv",
            headers={"Content-Disposition": "attachment; filename=zone_occupancy_report.csv"}
        )
    
    return report_data


@app.post("/api/v1/reports/schedule")
def schedule_automated_report(
    report_type: str,
    frequency: str = "daily",
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Schedule automated report generation (admin only)."""
    return reporting_system.schedule_report_generation(db, report_type, frequency)


@app.post("/api/v1/system/data-retention")
def apply_data_retention_policy(
    retention_days: int = 90,
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Apply data retention policy and clean up old data (admin only)."""
    if retention_days < 7:
        raise HTTPException(status_code=400, detail="Retention period must be at least 7 days")
    
    return reporting_system.apply_data_retention_policy(db, retention_days)


@app.post("/api/v1/system/backup")
def create_data_backup(
    backup_type: str = "full",
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_admin)
) -> Dict[str, Any]:
    """Create database backup (admin only)."""
    return reporting_system.create_data_backup(db, backup_type)


@app.get("/api/v1/reports/data")
def get_combined_report_data(
    from_date: str = Query(..., alias="from"),
    to_date: str = Query(..., alias="to"),
    metrics: str = Query("activity,alerts,zones,performance", alias="metrics"),
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_any_role),
) -> Dict[str, Any]:
    """
    Aggregate reporting endpoint used by the React ReportingDashboard.

    The response shape mirrors the frontend ``ReportData`` type and combines
    activity, alert, zone and performance metrics into a single payload.
    """
    start, end_exclusive = _parse_report_range(from_date, to_date)
    metric_set = {m.strip() for m in metrics.split(",") if m.strip()}

    data: Dict[str, Any] = {}
    # Patient activity over time
    if "activity" in metric_set:
        data["patientActivity"] = _build_patient_activity_series(
            db, start, end_exclusive
        )
    else:
        data["patientActivity"] = []

    # Alert statistics over time
    if "alerts" in metric_set:
        data["alertStatistics"] = _build_alert_statistics_series(
            db, start, end_exclusive
        )
    else:
        data["alertStatistics"] = []

    # Zone occupancy distribution
    if "zones" in metric_set:
        data["zoneOccupancy"] = _build_zone_occupancy_summary(
            db, start, end_exclusive
        )
    else:
        data["zoneOccupancy"] = []

    # System performance and patient metrics are always included
    data["systemPerformance"] = _build_system_performance_series(
        db, start, end_exclusive
    )
    data["patientMetrics"] = _build_patient_metrics_table(db, start, end_exclusive)
    return data


@app.get("/api/v1/reports/export")
def export_combined_report(
    format: str = Query("csv", alias="format"),
    from_date: str = Query(..., alias="from"),
    to_date: str = Query(..., alias="to"),
    metrics: str = Query("activity,alerts,zones,performance", alias="metrics"),
    report_type: str = Query("summary", alias="type"),
    db: Session = Depends(get_db),
    current_user: models.User = Depends(require_staff),
):
    """
    Export combined report data.

    For ``format=json`` the raw structured data is returned.
    For ``format=csv`` a lightweight CSV file is generated. For
    ``format=pdf`` we render a simple, text‑based PDF representation of the
    same CSV content.
    """
    from fastapi.responses import Response, JSONResponse

    data = get_combined_report_data(
        from_date=from_date,
        to_date=to_date,
        metrics=metrics,
        db=db,
        current_user=current_user,
    )

    fmt = format.lower()

    if fmt == "json":
        return JSONResponse(content=data)

    # Flatten into a very simple multi‑section CSV.
    lines: List[str] = []
    lines.append(f"# Halo Watch report ({report_type}) from {from_date} to {to_date}")

    # Patient activity
    lines.append("")
    lines.append("section=patientActivity,date,totalPatients,activePatients,averageSteps,averageHeartRate")
    for row in data.get("patientActivity", []):
        lines.append(
            f"patientActivity,{row['date']},{row['totalPatients']},"
            f"{row['activePatients']},{row['averageSteps']},{row['averageHeartRate']}"
        )

    # Alert statistics
    lines.append("")
    lines.append("section=alertStatistics,date,totalAlerts,criticalAlerts,resolvedAlerts,averageResponseTime")
    for row in data.get("alertStatistics", []):
        lines.append(
            f"alertStatistics,{row['date']},{row['totalAlerts']},"
            f"{row['criticalAlerts']},{row['resolvedAlerts']},{row['averageResponseTime']}"
        )

    # Zone occupancy
    lines.append("")
    lines.append("section=zoneOccupancy,zoneName,totalVisits,averageDuration,violations")
    for row in data.get("zoneOccupancy", []):
        lines.append(
            f"zoneOccupancy,{row['zoneName']},{row['totalVisits']},"
            f"{row['averageDuration']},{row['violations']}"
        )

    # Patient metrics
    lines.append("")
    lines.append(
        "section=patientMetrics,patientId,patientName,totalSteps,averageHeartRate,averageTemperature,alertsGenerated,zoneViolations"
    )
    for row in data.get("patientMetrics", []):
        lines.append(
            "patientMetrics,"
            f"{row['patientId']},{row['patientName']},{row['totalSteps']},"
            f"{row['averageHeartRate']},{row['averageTemperature']},"
            f"{row['alertsGenerated']},{row['zoneViolations']}"
        )

    csv_content = "\n".join(lines)

    # If PDF was requested, try to render a very simple text‑only PDF using
    # ReportLab. If anything goes wrong we gracefully fall back to CSV.
    if fmt == "pdf":
        try:
            from io import BytesIO
            from reportlab.lib.pagesizes import letter
            from reportlab.pdfgen import canvas

            buffer = BytesIO()
            c = canvas.Canvas(buffer, pagesize=letter)
            width, height = letter

            text_obj = c.beginText()
            text_obj.setTextOrigin(40, height - 40)
            text_obj.setFont("Helvetica", 10)

            for line in csv_content.splitlines():
                text_obj.textLine(line)
                if text_obj.getY() < 40:
                    c.drawText(text_obj)
                    c.showPage()
                    text_obj = c.beginText()
                    text_obj.setTextOrigin(40, height - 40)
                    text_obj.setFont("Helvetica", 10)

            c.drawText(text_obj)
            c.showPage()
            c.save()

            pdf_bytes = buffer.getvalue()
            buffer.close()

            return Response(
                content=pdf_bytes,
                media_type="application/pdf",
                headers={
                    "Content-Disposition": "attachment; filename=halo-watch-report.pdf"
                },
            )
        except Exception:
            # Fall back to CSV if ReportLab is not available or any error occurs.
            fmt = "csv"

    # Default: CSV export
    return Response(
        content=csv_content,
        media_type="text/csv",
        headers={
            "Content-Disposition": "attachment; filename=halo-watch-report.csv"
        },
    )