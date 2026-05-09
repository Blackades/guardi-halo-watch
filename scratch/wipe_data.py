import sys
import os
sys.path.append(os.getcwd())
from backend.database import SessionLocal
from backend.models import Patient, Alert, AccessLog, LocationHistory, PatientAssignmentHistory

db = SessionLocal()
try:
    print("Deleting all patients, alerts, and access logs...")
    db.query(Alert).delete()
    db.query(AccessLog).delete()
    db.query(LocationHistory).delete()
    db.query(PatientAssignmentHistory).delete()
    db.query(Patient).delete()
    db.commit()
    print("Cleanup complete.")
finally:
    db.close()
