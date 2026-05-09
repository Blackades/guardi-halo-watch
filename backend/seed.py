from datetime import datetime, timedelta

from sqlalchemy.orm import Session

from . import models
from .auth import AuthenticationService


def _ensure_server_status(db: Session) -> None:
    if not db.query(models.ServerStatus).first():
        status = models.ServerStatus(started_at=datetime.utcnow())
        db.add(status)
        db.commit()


def _ensure_default_admin(db: Session) -> None:
    """Create default admin user if no users exist."""
    if not db.query(models.User).first():
        admin_user = models.User(
            username="admin",
            password_hash=AuthenticationService.get_password_hash("admin123"),
            role="admin",
            full_name="System Administrator",
            email="admin@halowatch.local",
            is_active=True
        )
        db.add(admin_user)
        db.commit()


def seed_initial_data(db: Session) -> None:
    """
    Ensure core tables exist and seed only structural ward layout data.

    All domain data (patients, vitals, alerts, access logs) is now expected
    to come from real devices or API calls, not from hardcoded demo records.
    """
    _ensure_server_status(db)
    _ensure_default_admin(db)

    # Only create the ward rooms once; do not seed any fake patients/alerts.
    if db.query(models.Room).first():
        return

    rooms = [
        # Left block (symmetrical 3 compartments)
        models.Room(
            room_code="REST",
            name="Restricted Area",
            ward="Ward A",
            status="normal",
            pos_x=23,
            pos_y=20,
        ),
        models.Room(
            room_code="R01",
            name="Room 1",
            ward="Ward A",
            status="normal",
            pos_x=23,
            pos_y=50,
        ),
        models.Room(
            room_code="R02",
            name="Room 2",
            ward="Ward A",
            status="normal",
            pos_x=23,
            pos_y=80,
        ),
        # Center corridor block
        models.Room(
            room_code="CORR",
            name="Corridor",
            ward="Ward A",
            status="normal",
            pos_x=50,
            pos_y=45,
        ),
        models.Room(
            room_code="ENTRY",
            name="Entry",
            ward="Ward A",
            status="normal",
            pos_x=50,
            pos_y=85,
        ),
        # Right block (symmetrical 3 compartments)
        models.Room(
            room_code="NURSE",
            name="Nurses Station",
            ward="Ward A",
            status="normal",
            pos_x=77,
            pos_y=20,
        ),
        models.Room(
            room_code="ISO",
            name="Isolation Room",
            ward="Ward A",
            status="normal",
            pos_x=77,
            pos_y=50,
        ),
        models.Room(
            room_code="R03",
            name="Room 3",
            ward="Ward A",
            status="normal",
            pos_x=77,
            pos_y=80,
        ),
    ]
    db.add_all(rooms)
    
    # Create default zones if they don't exist
    if not db.query(models.Zone).first():
        zones = [
            models.Zone(
                zone_code="GENERAL",
                zone_name="General Ward Area",
                zone_type="normal",
                require_authorization=False,
                alert_level="info"
            ),
            models.Zone(
                zone_code="RESTRICTED",
                zone_name="Restricted Area",
                zone_type="restricted",
                require_authorization=True,
                alert_level="alert"
            ),
            models.Zone(
                zone_code="ISOLATION",
                zone_name="Isolation Zone",
                zone_type="isolation",
                require_authorization=True,
                max_occupancy=1,
                alert_level="critical"
            ),
            models.Zone(
                zone_code="EXIT",
                zone_name="Exit Points",
                zone_type="exit",
                require_authorization=True,
                alert_level="alert"
            ),
        ]
        db.add_all(zones)

    # No pre-registered patients for v2.0 hardware
    
    db.commit()



