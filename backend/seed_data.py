"""
Enhanced data seeding system for Halo Watch
Provides comprehensive test data and production-ready initial data.
"""

import json
import random
from datetime import datetime, timedelta
from typing import List, Dict, Any, Optional
from sqlalchemy.orm import Session

from models import (
    Patient, Room, Receiver, Alert, AccessLog, LocationHistory, 
    User, Zone, ServerStatus
)
from auth import AuthenticationService


class DataSeeder:
    """Comprehensive data seeding system."""
    
    def __init__(self, db: Session):
        self.db = db
    
    def seed_production_data(self) -> Dict[str, Any]:
        """Seed minimal production-ready data."""
        try:
            # Create admin user
            admin_user = self._create_admin_user()
            
            # Create basic zones
            zones = self._create_basic_zones()
            
            # Create system configuration
            self._create_system_config()
            
            self.db.commit()
            
            return {
                "success": True,
                "message": "Production data seeded successfully",
                "created": {
                    "admin_user": admin_user.username if admin_user else None,
                    "zones": len(zones),
                    "system_config": True
                }
            }
        except Exception as e:
            self.db.rollback()
            return {
                "success": False,
                "error": str(e)
            }
    
    def seed_development_data(self) -> Dict[str, Any]:
        """Seed comprehensive development/demo data."""
        try:
            # Production data first
            prod_result = self.seed_production_data()
            if not prod_result["success"]:
                return prod_result
            
            # Additional development data
            users = self._create_demo_users()
            rooms = self._create_demo_rooms()
            receivers = self._create_demo_receivers()
            patients = self._create_demo_patients()
            alerts = self._create_demo_alerts()
            location_history = self._create_demo_location_history()
            access_logs = self._create_demo_access_logs()
            
            self.db.commit()
            
            return {
                "success": True,
                "message": "Development data seeded successfully",
                "created": {
                    "users": len(users),
                    "rooms": len(rooms),
                    "receivers": len(receivers),
                    "patients": len(patients),
                    "alerts": len(alerts),
                    "location_history": len(location_history),
                    "access_logs": len(access_logs)
                }
            }
        except Exception as e:
            self.db.rollback()
            return {
                "success": False,
                "error": str(e)
            }
    
    def _create_admin_user(self) -> Optional[User]:
        """Create default admin user."""
        existing_admin = self.db.query(User).filter(User.role == "admin").first()
        if existing_admin:
            return existing_admin
        
        admin_user = User(
            username="admin",
            password_hash=AuthenticationService.get_password_hash("admin123"),
            role="admin",
            full_name="System Administrator",
            email="admin@halowatch.com",
            is_active=True
        )
        self.db.add(admin_user)
        return admin_user
    
    def _create_basic_zones(self) -> List[Zone]:
        """Create basic zone configuration."""
        zones_data = [
            {
                "zone_code": "WARD_A",
                "zone_name": "Ward A - General",
                "zone_type": "normal",
                "require_authorization": False,
                "max_occupancy": None,
                "alert_level": "info",
                "coordinates_json": json.dumps({
                    "boundaries": [
                        {"x": 0, "y": 0}, {"x": 100, "y": 0},
                        {"x": 100, "y": 50}, {"x": 0, "y": 50}
                    ]
                })
            },
            {
                "zone_code": "ICU",
                "zone_name": "Intensive Care Unit",
                "zone_type": "restricted",
                "require_authorization": True,
                "max_occupancy": 10,
                "alert_level": "warning",
                "coordinates_json": json.dumps({
                    "boundaries": [
                        {"x": 100, "y": 0}, {"x": 150, "y": 0},
                        {"x": 150, "y": 30}, {"x": 100, "y": 30}
                    ]
                })
            },
            {
                "zone_code": "ISOLATION",
                "zone_name": "Isolation Ward",
                "zone_type": "isolation",
                "require_authorization": True,
                "max_occupancy": 5,
                "alert_level": "critical",
                "coordinates_json": json.dumps({
                    "boundaries": [
                        {"x": 0, "y": 50}, {"x": 50, "y": 50},
                        {"x": 50, "y": 80}, {"x": 0, "y": 80}
                    ]
                })
            },
            {
                "zone_code": "EXIT_MAIN",
                "zone_name": "Main Exit",
                "zone_type": "exit",
                "require_authorization": True,
                "max_occupancy": None,
                "alert_level": "critical",
                "coordinates_json": json.dumps({
                    "boundaries": [
                        {"x": 75, "y": 0}, {"x": 85, "y": 0},
                        {"x": 85, "y": 10}, {"x": 75, "y": 10}
                    ]
                })
            }
        ]
        
        zones = []
        for zone_data in zones_data:
            existing_zone = self.db.query(Zone).filter(Zone.zone_code == zone_data["zone_code"]).first()
            if not existing_zone:
                zone = Zone(**zone_data)
                self.db.add(zone)
                zones.append(zone)
        
        return zones
    
    def _create_system_config(self):
        """Create system configuration entries."""
        # Create server status entry
        existing_status = self.db.query(ServerStatus).first()
        if not existing_status:
            server_status = ServerStatus(started_at=datetime.utcnow())
            self.db.add(server_status)
    
    def _create_demo_users(self) -> List[User]:
        """Create demo users for development."""
        users_data = [
            {
                "username": "nurse1",
                "password": "nurse123",
                "role": "nurse",
                "full_name": "Sarah Johnson",
                "email": "sarah.johnson@hospital.com"
            },
            {
                "username": "doctor1",
                "password": "doctor123",
                "role": "doctor",
                "full_name": "Dr. Michael Chen",
                "email": "michael.chen@hospital.com"
            },
            {
                "username": "viewer1",
                "password": "viewer123",
                "role": "viewer",
                "full_name": "John Observer",
                "email": "john.observer@hospital.com"
            }
        ]
        
        users = []
        for user_data in users_data:
            existing_user = self.db.query(User).filter(User.username == user_data["username"]).first()
            if not existing_user:
                user = User(
                    username=user_data["username"],
                    password_hash=AuthenticationService.get_password_hash(user_data["password"]),
                    role=user_data["role"],
                    full_name=user_data["full_name"],
                    email=user_data["email"],
                    is_active=True
                )
                self.db.add(user)
                users.append(user)
        
        return users
    
    def _create_demo_rooms(self) -> List[Room]:
        """Create demo rooms."""
        rooms_data = [
            {"room_code": "A101", "name": "Room A101", "pos_x": 10, "pos_y": 10, "status": "occupied"},
            {"room_code": "A102", "name": "Room A102", "pos_x": 30, "pos_y": 10, "status": "available"},
            {"room_code": "A103", "name": "Room A103", "pos_x": 50, "pos_y": 10, "status": "occupied"},
            {"room_code": "ICU01", "name": "ICU Room 1", "pos_x": 110, "pos_y": 10, "status": "occupied"},
            {"room_code": "ICU02", "name": "ICU Room 2", "pos_x": 130, "pos_y": 10, "status": "available"},
            {"room_code": "ISO01", "name": "Isolation Room 1", "pos_x": 20, "pos_y": 60, "status": "occupied"},
        ]
        
        rooms = []
        for room_data in rooms_data:
            existing_room = self.db.query(Room).filter(Room.room_code == room_data["room_code"]).first()
            if not existing_room:
                room = Room(**room_data)
                self.db.add(room)
                rooms.append(room)
        
        return rooms
    
    def _create_demo_receivers(self) -> List[Receiver]:
        """Create demo receiver nodes."""
        receivers_data = [
            {
                "receiver_id": "RX001",
                "zone": "WARD_A",
                "room": "A101",
                "coord_x": 15.0,
                "coord_y": 15.0,
                "coord_z": 2.5,
                "zone_type": "normal",
                "last_seen": datetime.utcnow() - timedelta(minutes=2)
            },
            {
                "receiver_id": "RX002",
                "zone": "WARD_A",
                "room": "A103",
                "coord_x": 55.0,
                "coord_y": 15.0,
                "coord_z": 2.5,
                "zone_type": "normal",
                "last_seen": datetime.utcnow() - timedelta(minutes=1)
            },
            {
                "receiver_id": "RX003",
                "zone": "ICU",
                "room": "ICU01",
                "coord_x": 115.0,
                "coord_y": 15.0,
                "coord_z": 2.5,
                "zone_type": "restricted",
                "last_seen": datetime.utcnow() - timedelta(minutes=3)
            },
            {
                "receiver_id": "RX004",
                "zone": "ISOLATION",
                "room": "ISO01",
                "coord_x": 25.0,
                "coord_y": 65.0,
                "coord_z": 2.5,
                "zone_type": "isolation",
                "last_seen": datetime.utcnow() - timedelta(minutes=5)
            },
            {
                "receiver_id": "RX005",
                "zone": "EXIT_MAIN",
                "room": None,
                "coord_x": 80.0,
                "coord_y": 5.0,
                "coord_z": 2.5,
                "zone_type": "exit",
                "last_seen": datetime.utcnow() - timedelta(minutes=1)
            }
        ]
        
        receivers = []
        for receiver_data in receivers_data:
            existing_receiver = self.db.query(Receiver).filter(
                Receiver.receiver_id == receiver_data["receiver_id"]
            ).first()
            if not existing_receiver:
                receiver = Receiver(**receiver_data)
                self.db.add(receiver)
                receivers.append(receiver)
        
        return receivers
    
    def _create_demo_patients(self) -> List[Patient]:
        """Create demo patients."""
        patients_data = [
            {
                "patient_id": "P001",
                "name": "Alice Johnson",
                "age": 45,
                "room": "A101",
                "location": "A101",
                "status": "occupied",
                "heart_rate": 72,
                "temperature": 36.8,
                "spo2": 98,
                "battery_level": 85,
                "strap_intact": True,
                "skin_contact": True,
                "last_activity": datetime.utcnow() - timedelta(minutes=5)
            },
            {
                "patient_id": "P002",
                "name": "Bob Smith",
                "age": 62,
                "room": "A103",
                "location": "A103",
                "status": "occupied",
                "heart_rate": 68,
                "temperature": 37.1,
                "spo2": 96,
                "battery_level": 45,
                "strap_intact": True,
                "skin_contact": True,
                "last_activity": datetime.utcnow() - timedelta(minutes=2)
            },
            {
                "patient_id": "P003",
                "name": "Carol Davis",
                "age": 38,
                "room": "ICU01",
                "location": "ICU01",
                "status": "anomaly",
                "heart_rate": 105,
                "temperature": 38.2,
                "spo2": 94,
                "battery_level": 92,
                "strap_intact": True,
                "skin_contact": True,
                "abnormal_vitals": True,
                "last_activity": datetime.utcnow() - timedelta(minutes=1)
            },
            {
                "patient_id": "P004",
                "name": "David Wilson",
                "age": 55,
                "room": "ISO01",
                "location": "ISO01",
                "status": "occupied",
                "heart_rate": 75,
                "temperature": 36.9,
                "spo2": 97,
                "battery_level": 78,
                "strap_intact": True,
                "skin_contact": True,
                "last_activity": datetime.utcnow() - timedelta(minutes=8)
            }
        ]
        
        patients = []
        for patient_data in patients_data:
            existing_patient = self.db.query(Patient).filter(
                Patient.patient_id == patient_data["patient_id"]
            ).first()
            if not existing_patient:
                patient = Patient(**patient_data)
                self.db.add(patient)
                patients.append(patient)
                
                # Update room occupancy
                room = self.db.query(Room).filter(Room.room_code == patient_data["room"]).first()
                if room:
                    room.patient_id = patient_data["patient_id"]
        
        return patients
    
    def _create_demo_alerts(self) -> List[Alert]:
        """Create demo alerts."""
        alerts_data = [
            {
                "alert_code": "ALT001",
                "type": "critical",
                "title": "Abnormal Vital Signs",
                "message": "Patient P003 (Carol Davis) has elevated heart rate (105 BPM) and temperature (38.2°C)",
                "patient_id": "P003",
                "room": "ICU01",
                "status": "active",
                "escalation_level": 2,
                "created_at": datetime.utcnow() - timedelta(minutes=15)
            },
            {
                "alert_code": "ALT002",
                "type": "warning",
                "title": "Low Battery",
                "message": "Patient P002 (Bob Smith) wristband battery is low (45%)",
                "patient_id": "P002",
                "room": "A103",
                "status": "active",
                "escalation_level": 1,
                "created_at": datetime.utcnow() - timedelta(minutes=30)
            },
            {
                "alert_code": "ALT003",
                "type": "info",
                "title": "Patient Movement",
                "message": "Patient P001 (Alice Johnson) has been inactive for extended period",
                "patient_id": "P001",
                "room": "A101",
                "status": "resolved",
                "escalation_level": 1,
                "created_at": datetime.utcnow() - timedelta(hours=2),
                "resolved_at": datetime.utcnow() - timedelta(hours=1),
                "resolved_by": "nurse1"
            }
        ]
        
        alerts = []
        for alert_data in alerts_data:
            existing_alert = self.db.query(Alert).filter(
                Alert.alert_code == alert_data["alert_code"]
            ).first()
            if not existing_alert:
                alert = Alert(**alert_data)
                self.db.add(alert)
                alerts.append(alert)
        
        return alerts
    
    def _create_demo_location_history(self) -> List[LocationHistory]:
        """Create demo location history."""
        location_data = []
        patients = ["P001", "P002", "P003", "P004"]
        receivers = ["RX001", "RX002", "RX003", "RX004"]
        
        # Generate location history for the past 24 hours
        for i in range(100):
            timestamp = datetime.utcnow() - timedelta(hours=24) + timedelta(minutes=i * 14.4)
            patient_id = random.choice(patients)
            receiver_id = random.choice(receivers)
            
            location_data.append({
                "patient_id": patient_id,
                "receiver_id": receiver_id,
                "zone": "WARD_A" if receiver_id in ["RX001", "RX002"] else "ICU",
                "room": f"A{101 + int(receiver_id[-1])}" if receiver_id in ["RX001", "RX002"] else f"ICU0{receiver_id[-1]}",
                "coord_x": random.uniform(10, 150),
                "coord_y": random.uniform(10, 80),
                "coord_z": 2.5,
                "estimated_distance": random.uniform(1.0, 4.0),
                "timestamp": timestamp
            })
        
        locations = []
        for loc_data in location_data:
            location = LocationHistory(**loc_data)
            self.db.add(location)
            locations.append(location)
        
        return locations
    
    def _create_demo_access_logs(self) -> List[AccessLog]:
        """Create demo access logs."""
        access_data = []
        patients = [
            {"id": "P001", "name": "Alice Johnson"},
            {"id": "P002", "name": "Bob Smith"},
            {"id": "P003", "name": "Carol Davis"},
            {"id": "P004", "name": "David Wilson"}
        ]
        rooms = ["A101", "A102", "A103", "ICU01", "ICU02", "ISO01"]
        actions = ["entry", "exit", "denied"]
        
        # Generate access logs for the past week
        for i in range(200):
            timestamp = datetime.utcnow() - timedelta(days=7) + timedelta(hours=i * 0.84)
            patient = random.choice(patients)
            room = random.choice(rooms)
            action = random.choice(actions)
            
            access_data.append({
                "log_code": f"LOG{i+1:03d}",
                "patient_id": patient["id"],
                "patient_name": patient["name"],
                "room": room,
                "action": action,
                "timestamp": timestamp,
                "rfid_id": f"RFID{patient['id'][-3:]}",
                "duration_seconds": random.randint(300, 7200) if action == "entry" else None
            })
        
        logs = []
        for log_data in access_data:
            log = AccessLog(**log_data)
            self.db.add(log)
            logs.append(log)
        
        return logs


def seed_initial_data(db: Session, environment: str = "development") -> Dict[str, Any]:
    """Main seeding function called from application startup."""
    seeder = DataSeeder(db)
    
    if environment == "production":
        return seeder.seed_production_data()
    else:
        return seeder.seed_development_data()


# CLI interface for seeding
if __name__ == "__main__":
    import argparse
    from database import SessionLocal
    
    parser = argparse.ArgumentParser(description="Halo Watch Data Seeder")
    parser.add_argument("--environment", choices=["development", "production"], 
                       default="development", help="Environment to seed for")
    parser.add_argument("--clear", action="store_true", help="Clear existing data first")
    
    args = parser.parse_args()
    
    db = SessionLocal()
    try:
        if args.clear:
            # Clear existing data (be careful!)
            print("Clearing existing data...")
            # Add clearing logic here if needed
        
        result = seed_initial_data(db, args.environment)
        print(json.dumps(result, indent=2, default=str))
    finally:
        db.close()