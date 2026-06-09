import sys
import os
from datetime import datetime, timedelta

# Add parent directory to path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from backend.database import SessionLocal
from backend import models

def test_queries():
    db = SessionLocal()
    try:
        print("Testing DoorEvent query...")
        start_dt = datetime.utcnow() - timedelta(days=90)
        end_dt = datetime.utcnow()
        
        events = db.query(models.DoorEvent).outerjoin(
            models.Patient, models.DoorEvent.patient_id == models.Patient.patient_id
        ).filter(
            models.DoorEvent.timestamp >= start_dt,
            models.DoorEvent.timestamp <= end_dt
        ).all()
        print(f"Successfully queried {len(events)} door events.")
        
        print("Testing PatientAssignmentHistory query...")
        hist = db.query(models.PatientAssignmentHistory).filter(
            models.PatientAssignmentHistory.assigned_at >= start_dt,
            models.PatientAssignmentHistory.assigned_at <= end_dt
        ).all()
        print(f"Successfully queried {len(hist)} historical assignments.")

        print("Testing Active Patient query...")
        active = db.query(models.Patient).filter(
            (models.Patient.rfid_uid.isnot(None)) | (models.Patient.ble_minor.isnot(None))
        ).filter(
            models.Patient.last_activity >= start_dt,
            models.Patient.last_activity <= end_dt
        ).all()
        print(f"Successfully queried {len(active)} active patients.")
        
        print("All queries completed successfully!")
    except Exception as e:
        print(f"Query test failed: {e}")
        sys.exit(1)
    finally:
        db.close()

if __name__ == "__main__":
    test_queries()
