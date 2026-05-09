"""
Door Configuration Service for managing RFID readers and door name assignments.
"""

from datetime import datetime
from typing import Dict, List, Optional
import re

from sqlalchemy.orm import Session
from sqlalchemy import and_

from . import models


class DoorConfigurationService:
    """
    Service for managing RFID reader configuration and door name assignments.
    """

    @staticmethod
    def sync_reader_config(
        db: Session,
        node_id: str,
        reader_id: str,
        door_name: str,
        gpio_pin: Optional[int] = None
    ) -> models.RFIDReader:
        """
        Receive a configuration sync from an ESP32 and upsert the reader record.

        If the reader does not yet exist it is created (gpio_pin defaults to 0
        when not provided).  The door name is validated and stored.

        Args:
            db: Database session
            node_id: Unified node identifier
            reader_id: Reader hardware identifier
            door_name: Human-readable door name
            gpio_pin: Optional GPIO pin number

        Returns:
            The created or updated RFIDReader instance

        Raises:
            ValueError: If door_name is invalid
        """
        # Validate door name
        if not door_name or not door_name.strip():
            raise ValueError("Door name cannot be empty")

        if not re.match(r'^[a-zA-Z0-9\s\-_]+$', door_name):
            raise ValueError(
                "Door name must contain only alphanumeric characters, spaces, hyphens, and underscores"
            )

        # Upsert the reader
        reader = db.query(models.RFIDReader).filter(
            and_(
                models.RFIDReader.node_id == node_id,
                models.RFIDReader.reader_id == reader_id
            )
        ).first()

        if reader:
            reader.door_name = door_name.strip()
            reader.status = "active"
            reader.last_seen = datetime.utcnow()
            if gpio_pin is not None:
                reader.gpio_pin = gpio_pin
        else:
            reader = models.RFIDReader(
                node_id=node_id,
                reader_id=reader_id,
                gpio_pin=gpio_pin if gpio_pin is not None else 0,
                door_name=door_name.strip(),
                status="active",
                last_seen=datetime.utcnow()
            )
            db.add(reader)

        db.commit()
        db.refresh(reader)
        return reader

    @staticmethod
    def register_reader(
        db: Session,
        node_id: str,
        reader_id: str,
        gpio_pin: int
    ) -> models.RFIDReader:
        """
        Register a new RFID reader with the system.
        
        Args:
            db: Database session
            node_id: Unified node identifier
            reader_id: Reader hardware identifier
            gpio_pin: GPIO pin number for the reader
            
        Returns:
            The created or updated RFIDReader instance
        """
        # Check if reader already exists
        existing_reader = db.query(models.RFIDReader).filter(
            and_(
                models.RFIDReader.node_id == node_id,
                models.RFIDReader.reader_id == reader_id
            )
        ).first()
        
        if existing_reader:
            # Update existing reader
            existing_reader.gpio_pin = gpio_pin
            existing_reader.status = "active"
            existing_reader.last_seen = datetime.utcnow()
            db.commit()
            db.refresh(existing_reader)
            return existing_reader
        
        # Create new reader
        new_reader = models.RFIDReader(
            node_id=node_id,
            reader_id=reader_id,
            gpio_pin=gpio_pin,
            status="active",
            last_seen=datetime.utcnow()
        )
        db.add(new_reader)
        db.commit()
        db.refresh(new_reader)
        return new_reader

    @staticmethod
    def assign_door_name(
        db: Session,
        node_id: str,
        reader_id: str,
        door_name: str
    ) -> Optional[models.RFIDReader]:
        """
        Assign a human-readable name to a reader.
        
        Args:
            db: Database session
            node_id: Unified node identifier
            reader_id: Reader hardware identifier
            door_name: Human-readable door name
            
        Returns:
            The updated RFIDReader instance or None if not found
            
        Raises:
            ValueError: If door_name is invalid
        """
        # Validate door name
        if not door_name or not door_name.strip():
            raise ValueError("Door name cannot be empty")
        
        # Validate alphanumeric with spaces (and common punctuation)
        if not re.match(r'^[a-zA-Z0-9\s\-_]+$', door_name):
            raise ValueError(
                "Door name must contain only alphanumeric characters, spaces, hyphens, and underscores"
            )
        
        # Find the reader
        reader = db.query(models.RFIDReader).filter(
            and_(
                models.RFIDReader.node_id == node_id,
                models.RFIDReader.reader_id == reader_id
            )
        ).first()
        
        if not reader:
            return None
        
        # Update door name
        reader.door_name = door_name.strip()
        db.commit()
        db.refresh(reader)
        return reader

    @staticmethod
    def get_door_mappings(db: Session, node_id: str) -> Dict[str, str]:
        """
        Get all door name mappings for a node.
        
        Args:
            db: Database session
            node_id: Unified node identifier
            
        Returns:
            Dictionary mapping reader_id to door_name
        """
        readers = db.query(models.RFIDReader).filter(
            models.RFIDReader.node_id == node_id
        ).all()
        
        return {
            reader.reader_id: reader.door_name or ""
            for reader in readers
        }

    @staticmethod
    def list_all_readers(
        db: Session,
        node_id: str
    ) -> List[Dict[str, any]]:
        """
        List all registered readers with their current configuration.
        
        Args:
            db: Database session
            node_id: Unified node identifier
            
        Returns:
            List of reader information dictionaries
        """
        readers = db.query(models.RFIDReader).filter(
            models.RFIDReader.node_id == node_id
        ).order_by(models.RFIDReader.gpio_pin).all()
        
        result = []
        for reader in readers:
            result.append({
                "reader_id": reader.reader_id,
                "gpio_pin": reader.gpio_pin,
                "door_name": reader.door_name,
                "status": reader.status,
                "last_seen": reader.last_seen,
                "created_at": reader.created_at
            })
        
        return result

    @staticmethod
    def update_reader_status(
        db: Session,
        node_id: str,
        reader_id: str,
        status: str
    ) -> Optional[models.RFIDReader]:
        """
        Update reader connection status.
        
        Args:
            db: Database session
            node_id: Unified node identifier
            reader_id: Reader hardware identifier
            status: New status (active, disconnected, error)
            
        Returns:
            The updated RFIDReader instance or None if not found
        """
        # Validate status
        valid_statuses = ["active", "disconnected", "error"]
        if status not in valid_statuses:
            raise ValueError(f"Status must be one of: {', '.join(valid_statuses)}")
        
        # Find the reader
        reader = db.query(models.RFIDReader).filter(
            and_(
                models.RFIDReader.node_id == node_id,
                models.RFIDReader.reader_id == reader_id
            )
        ).first()
        
        if not reader:
            return None
        
        # Update status
        reader.status = status
        if status == "active":
            reader.last_seen = datetime.utcnow()
        
        db.commit()
        db.refresh(reader)
        return reader
