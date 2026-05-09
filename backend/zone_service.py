import json
from typing import List, Optional, Dict, Any
from sqlalchemy.orm import Session
from fastapi import HTTPException, status
from . import models, schemas


class ZoneManagementService:
    """Service for managing zones and patient zone restrictions."""
    
    @staticmethod
    def create_zone(db: Session, zone_data: 'schemas.ZoneCreate') -> models.Zone:
        """Create a new zone."""
        # Check if zone code already exists
        existing_zone = db.query(models.Zone).filter(models.Zone.zone_code == zone_data.zone_code).first()
        if existing_zone:
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="Zone code already exists"
            )
        
        # Create zone
        zone = models.Zone(
            zone_code=zone_data.zone_code,
            zone_name=zone_data.zone_name,
            zone_type=zone_data.zone_type,
            require_authorization=zone_data.require_authorization,
            max_occupancy=zone_data.max_occupancy,
            alert_level=zone_data.alert_level,
            coordinates_json=json.dumps(zone_data.coordinates) if zone_data.coordinates else None
        )
        
        db.add(zone)
        db.commit()
        db.refresh(zone)
        return zone
    
    @staticmethod
    def get_zones(db: Session, include_inactive: bool = False) -> List[models.Zone]:
        """Get all zones."""
        query = db.query(models.Zone)
        if not include_inactive:
            query = query.filter(models.Zone.is_active == True)
        return query.all()
    
    @staticmethod
    def get_zone_by_code(db: Session, zone_code: str) -> Optional[models.Zone]:
        """Get zone by code."""
        return db.query(models.Zone).filter(models.Zone.zone_code == zone_code).first()
    
    @staticmethod
    def update_zone(db: Session, zone_code: str, zone_data: 'schemas.ZoneUpdate') -> models.Zone:
        """Update an existing zone."""
        zone = ZoneManagementService.get_zone_by_code(db, zone_code)
        if not zone:
            raise HTTPException(status_code=404, detail="Zone not found")
        
        # Update fields if provided
        if zone_data.zone_name is not None:
            zone.zone_name = zone_data.zone_name
        if zone_data.zone_type is not None:
            zone.zone_type = zone_data.zone_type
        if zone_data.require_authorization is not None:
            zone.require_authorization = zone_data.require_authorization
        if zone_data.max_occupancy is not None:
            zone.max_occupancy = zone_data.max_occupancy
        if zone_data.alert_level is not None:
            zone.alert_level = zone_data.alert_level
        if zone_data.coordinates is not None:
            zone.coordinates_json = json.dumps(zone_data.coordinates)
        if zone_data.is_active is not None:
            zone.is_active = zone_data.is_active
        
        db.commit()
        db.refresh(zone)
        return zone
    
    @staticmethod
    def delete_zone(db: Session, zone_code: str) -> bool:
        """Delete a zone (soft delete by setting is_active to False)."""
        zone = ZoneManagementService.get_zone_by_code(db, zone_code)
        if not zone:
            raise HTTPException(status_code=404, detail="Zone not found")
        
        zone.is_active = False
        db.commit()
        return True
    
    @staticmethod
    def assign_patient_zone_restrictions(db: Session, patient_id: str, zone_restrictions: 'schemas.PatientZoneRestrictions') -> models.Patient:
        """Assign zone restrictions to a patient."""
        patient = db.query(models.Patient).filter(models.Patient.patient_id == patient_id).first()
        if not patient:
            raise HTTPException(status_code=404, detail="Patient not found")
        
        # Validate that all authorized zones exist
        if zone_restrictions.authorized_zones:
            for zone_code in zone_restrictions.authorized_zones:
                zone = ZoneManagementService.get_zone_by_code(db, zone_code)
                if not zone:
                    raise HTTPException(
                        status_code=status.HTTP_400_BAD_REQUEST,
                        detail=f"Zone '{zone_code}' not found"
                    )
        
        # Update patient restrictions
        patient.isolation_level = zone_restrictions.isolation_level
        patient.authorized_zones = json.dumps(zone_restrictions.authorized_zones) if zone_restrictions.authorized_zones else None
        patient.emergency_contact = zone_restrictions.emergency_contact
        patient.medical_notes = zone_restrictions.medical_notes
        
        db.commit()
        db.refresh(patient)
        return patient
    
    @staticmethod
    def check_zone_access_authorization(db: Session, patient_id: str, zone_code: str) -> Dict[str, Any]:
        """Check if a patient is authorized to access a specific zone."""
        patient = db.query(models.Patient).filter(models.Patient.patient_id == patient_id).first()
        if not patient:
            return {"authorized": False, "reason": "Patient not found"}
        
        zone = ZoneManagementService.get_zone_by_code(db, zone_code)
        if not zone:
            return {"authorized": False, "reason": "Zone not found"}
        
        # If zone doesn't require authorization, allow access
        if not zone.require_authorization:
            return {"authorized": True, "reason": "Zone does not require authorization"}
        
        # Check if patient has authorized zones configured
        if not patient.authorized_zones:
            return {"authorized": False, "reason": "No zone authorizations configured for patient"}
        
        try:
            authorized_zones = json.loads(patient.authorized_zones)
        except (json.JSONDecodeError, TypeError):
            return {"authorized": False, "reason": "Invalid zone authorization data"}
        
        # Check if patient is authorized for this zone
        if zone_code in authorized_zones:
            return {"authorized": True, "reason": "Patient authorized for zone"}
        
        return {"authorized": False, "reason": "Patient not authorized for this zone"}
    
    @staticmethod
    def detect_zone_violation(db: Session, patient_id: str, zone_code: str) -> Optional[Dict[str, Any]]:
        """Detect if a patient's presence in a zone constitutes a violation."""
        access_check = ZoneManagementService.check_zone_access_authorization(db, patient_id, zone_code)
        
        if not access_check["authorized"]:
            zone = ZoneManagementService.get_zone_by_code(db, zone_code)
            patient = db.query(models.Patient).filter(models.Patient.patient_id == patient_id).first()
            
            return {
                "violation_type": "unauthorized_access",
                "patient_id": patient_id,
                "patient_name": patient.name if patient else "Unknown",
                "zone_code": zone_code,
                "zone_name": zone.zone_name if zone else "Unknown",
                "zone_type": zone.zone_type if zone else "unknown",
                "alert_level": zone.alert_level if zone else "warning",
                "reason": access_check["reason"]
            }
        
        return None
    
    @staticmethod
    def get_zone_occupancy(db: Session, zone_code: str) -> Dict[str, Any]:
        """Get current occupancy information for a zone."""
        zone = ZoneManagementService.get_zone_by_code(db, zone_code)
        if not zone:
            raise HTTPException(status_code=404, detail="Zone not found")
        
        # Count patients currently in this zone (based on room mapping)
        # This is a simplified approach - in a real system you'd have more sophisticated location tracking
        current_occupancy = db.query(models.Patient).filter(
            models.Patient.location == zone_code
        ).count()
        
        return {
            "zone_code": zone_code,
            "zone_name": zone.zone_name,
            "current_occupancy": current_occupancy,
            "max_occupancy": zone.max_occupancy,
            "occupancy_exceeded": zone.max_occupancy is not None and current_occupancy > zone.max_occupancy,
            "occupancy_percentage": (current_occupancy / zone.max_occupancy * 100) if zone.max_occupancy else None
        }