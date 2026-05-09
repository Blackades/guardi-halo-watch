"""
Location Intelligence Service for combining door access events and Bluetooth
distance measurements to infer patient room locations with confidence scores.

Implements Requirements 6.1, 6.2, 6.3, 6.4.
"""

from __future__ import annotations

import logging
from datetime import datetime, timedelta
from typing import List, Optional

from sqlalchemy.orm import Session

from . import models

logger = logging.getLogger(__name__)

# ── Tuning constants ──────────────────────────────────────────────────────────

# Maximum age of a door event before it stops contributing to confidence.
DOOR_EVENT_MAX_AGE_SECONDS: float = 300.0  # 5 minutes

# Distance threshold (metres) above which a patient is considered to have
# moved significantly from their last known position.
SIGNIFICANT_MOVEMENT_THRESHOLD_METRES: float = 3.0

# Distance that is considered "close" to a room (patient likely inside).
CLOSE_DISTANCE_METRES: float = 2.0

# Distance that is considered "far" from a room (patient likely elsewhere).
FAR_DISTANCE_METRES: float = 5.0

# Weight given to the door-event component when fusing confidence.
DOOR_EVENT_WEIGHT: float = 0.7

# Weight given to the distance component when fusing confidence.
DISTANCE_WEIGHT: float = 0.3


# ── Data classes ──────────────────────────────────────────────────────────────


class PatientLocationState:
    """
    Transient snapshot of a patient's inferred location.

    This is returned by the service methods and is *not* persisted directly;
    callers are responsible for writing the relevant fields to the database.
    """

    def __init__(
        self,
        patient_id: str,
        current_room: Optional[str],
        previous_room: Optional[str],
        estimated_distance: Optional[float],
        last_door_event: Optional[models.DoorEvent],
        location_confidence: float,
        significant_movement_flagged: bool,
        last_updated: datetime,
    ) -> None:
        self.patient_id = patient_id
        self.current_room = current_room
        self.previous_room = previous_room
        self.estimated_distance = estimated_distance
        self.last_door_event = last_door_event
        self.location_confidence = location_confidence
        self.significant_movement_flagged = significant_movement_flagged
        self.last_updated = last_updated

    def __repr__(self) -> str:  # pragma: no cover
        return (
            f"PatientLocationState(patient_id={self.patient_id!r}, "
            f"current_room={self.current_room!r}, "
            f"confidence={self.location_confidence:.2f})"
        )


# ── Service ───────────────────────────────────────────────────────────────────


class LocationIntelligenceService:
    """
    Combines door access events and Bluetooth distance measurements to
    determine the most likely room location for each patient.

    All public methods accept a SQLAlchemy ``Session`` as their first argument
    so that the service is stateless and safe to use in concurrent request
    handlers.
    """

    # ------------------------------------------------------------------
    # Requirement 6.1 – update patient room assignment on door event
    # ------------------------------------------------------------------

    def process_door_event(
        self,
        db: Session,
        event: models.DoorEvent,
    ) -> Optional[PatientLocationState]:
        """
        Process a door access event and update the patient's room assignment.

        When a patient passes through a door the system:
        - Sets ``current_room`` to the room associated with the door (entry)
          or to ``"corridor"`` (exit).
        - Persists the change to the ``patients`` table.
        - Returns a :class:`PatientLocationState` snapshot.

        Args:
            db:    Active database session.
            event: A persisted :class:`models.DoorEvent` instance.

        Returns:
            A :class:`PatientLocationState` snapshot, or ``None`` if the
            patient referenced by the event cannot be found.
        """
        if not event.patient_id:
            # Try to resolve patient from rfid_uid
            patient = (
                db.query(models.Patient)
                .filter(models.Patient.rfid_uid == event.rfid_uid)
                .first()
            )
            if patient:
                # Back-fill the patient_id on the event so future queries work
                event.patient_id = patient.patient_id
                db.add(event)
        else:
            patient = (
                db.query(models.Patient)
                .filter(models.Patient.patient_id == event.patient_id)
                .first()
            )

        if patient is None:
            logger.warning(
                "process_door_event: no patient found for rfid_uid=%s patient_id=%s",
                event.rfid_uid,
                event.patient_id,
            )
            return None

        previous_room = patient.room

        if event.action == "entry":
            new_room = self._get_room_for_door(event.door_name)
            patient.room = new_room
            patient.location = new_room
        elif event.action == "exit":
            new_room = "corridor"
            patient.room = new_room
            patient.location = new_room
        else:
            logger.warning(
                "process_door_event: unknown action %r for event id=%s",
                event.action,
                event.id,
            )
            new_room = patient.room

        patient.last_activity = datetime.utcnow()
        db.add(patient)
        db.commit()
        db.refresh(patient)

        confidence = self.calculate_location_confidence(
            db=db,
            patient=patient,
            current_distance=None,
        )

        state = PatientLocationState(
            patient_id=patient.patient_id,
            current_room=patient.room,
            previous_room=previous_room,
            estimated_distance=None,
            last_door_event=event,
            location_confidence=confidence,
            significant_movement_flagged=False,
            last_updated=datetime.utcnow(),
        )

        logger.info(
            "process_door_event: patient=%s moved %s → %s via door=%r (confidence=%.2f)",
            patient.patient_id,
            previous_room,
            patient.room,
            event.door_name,
            confidence,
        )
        return state

    # ------------------------------------------------------------------
    # Requirement 6.2 / 6.4 – distance-based room inference
    # ------------------------------------------------------------------

    def infer_room_from_distance(
        self,
        db: Session,
        patient_id: str,
        distance: float,
        door_history: Optional[List[models.DoorEvent]] = None,
    ) -> PatientLocationState:
        """
        Use Bluetooth distance and door history to infer the most likely room.

        Algorithm:
        1. If the patient recently entered a door *and* the distance is small
           (≤ ``CLOSE_DISTANCE_METRES``), they are likely still in that room.
        2. If the distance is large (≥ ``FAR_DISTANCE_METRES``), they may have
           moved to the corridor or a different room.
        3. Flags significant movement when the distance has increased beyond
           ``SIGNIFICANT_MOVEMENT_THRESHOLD_METRES`` relative to the last
           known distance stored in ``location_history``.

        Args:
            db:           Active database session.
            patient_id:   Public patient identifier.
            distance:     Estimated distance from the central node (metres).
            door_history: Optional pre-fetched list of recent door events.
                          When ``None`` the service queries the database.

        Returns:
            A :class:`PatientLocationState` snapshot.
        """
        patient = (
            db.query(models.Patient)
            .filter(models.Patient.patient_id == patient_id)
            .first()
        )
        if patient is None:
            logger.warning(
                "infer_room_from_distance: patient %s not found", patient_id
            )
            # Return a minimal state so callers don't have to handle None
            return PatientLocationState(
                patient_id=patient_id,
                current_room=None,
                previous_room=None,
                estimated_distance=distance,
                last_door_event=None,
                location_confidence=0.0,
                significant_movement_flagged=False,
                last_updated=datetime.utcnow(),
            )

        # Fetch recent door history if not supplied
        if door_history is None:
            door_history = self._get_recent_door_history(db, patient_id)

        last_door_event: Optional[models.DoorEvent] = (
            door_history[0] if door_history else None
        )

        # ── Infer room ────────────────────────────────────────────────
        inferred_room = patient.room  # default: keep current assignment

        if last_door_event is not None:
            age_seconds = self._event_age_seconds(last_door_event.timestamp)
            if (
                last_door_event.action == "entry"
                and age_seconds <= DOOR_EVENT_MAX_AGE_SECONDS
                and distance <= CLOSE_DISTANCE_METRES
            ):
                # Patient recently entered a room and is still close → confirm
                inferred_room = self._get_room_for_door(last_door_event.door_name)
            elif distance >= FAR_DISTANCE_METRES:
                # Patient is far from the node; they may be in the corridor
                inferred_room = "corridor"
        elif distance >= FAR_DISTANCE_METRES:
            inferred_room = "corridor"

        # ── Significant movement detection ────────────────────────────
        significant_movement = self._detect_significant_movement(
            db, patient_id, distance
        )

        if significant_movement:
            logger.info(
                "infer_room_from_distance: significant movement detected for "
                "patient=%s (distance=%.2fm)",
                patient_id,
                distance,
            )

        # ── Persist inferred room if it changed ───────────────────────
        if inferred_room and inferred_room != patient.room:
            patient.room = inferred_room
            patient.location = inferred_room
            patient.last_activity = datetime.utcnow()
            db.add(patient)
            db.commit()
            db.refresh(patient)

        confidence = self.calculate_location_confidence(
            db=db,
            patient=patient,
            current_distance=distance,
        )

        return PatientLocationState(
            patient_id=patient_id,
            current_room=patient.room,
            previous_room=None,
            estimated_distance=distance,
            last_door_event=last_door_event,
            location_confidence=confidence,
            significant_movement_flagged=significant_movement,
            last_updated=datetime.utcnow(),
        )

    # ------------------------------------------------------------------
    # Requirement 6.3 / 6.4 – confidence calculation
    # ------------------------------------------------------------------

    def calculate_location_confidence(
        self,
        db: Session,
        patient: models.Patient,
        current_distance: Optional[float],
    ) -> float:
        """
        Calculate a confidence score (0.0 – 1.0) for the patient's current
        location estimate.

        The score is a weighted combination of:

        * **Door-event component** (weight ``DOOR_EVENT_WEIGHT``):
          Decays linearly from 1.0 to 0.0 as the time since the last door
          event increases from 0 to ``DOOR_EVENT_MAX_AGE_SECONDS``.

        * **Distance component** (weight ``DISTANCE_WEIGHT``):
          1.0 when the patient is within ``CLOSE_DISTANCE_METRES``, 0.0 when
          beyond ``FAR_DISTANCE_METRES``, and linearly interpolated between.

        Args:
            db:               Active database session.
            patient:          The patient ORM instance.
            current_distance: Latest distance estimate (metres), or ``None``
                              if no distance data is available.

        Returns:
            Confidence score in [0.0, 1.0].
        """
        door_component = self._door_event_confidence(db, patient.patient_id)
        distance_component = self._distance_confidence(current_distance)

        confidence = (
            DOOR_EVENT_WEIGHT * door_component
            + DISTANCE_WEIGHT * distance_component
        )

        # Clamp to [0, 1] to guard against floating-point edge cases
        return max(0.0, min(1.0, confidence))

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _get_room_for_door(door_name: Optional[str]) -> str:
        """
        Map a door name to a room identifier.

        The door name itself is used as the room identifier so that the system
        works with any door configuration without requiring a separate mapping
        table.  Callers that need a more structured mapping can override this
        behaviour by subclassing.
        """
        if not door_name:
            return "unknown"
        return door_name

    @staticmethod
    def _event_age_seconds(event_timestamp: Optional[datetime]) -> float:
        """Return the age of an event in seconds (0 if timestamp is None)."""
        if event_timestamp is None:
            return 0.0
        delta = datetime.utcnow() - event_timestamp
        return max(0.0, delta.total_seconds())

    @staticmethod
    def _get_recent_door_history(
        db: Session,
        patient_id: str,
        limit: int = 10,
    ) -> List[models.DoorEvent]:
        """Fetch the most recent door events for a patient, newest first."""
        return (
            db.query(models.DoorEvent)
            .filter(models.DoorEvent.patient_id == patient_id)
            .order_by(models.DoorEvent.timestamp.desc())
            .limit(limit)
            .all()
        )

    def _door_event_confidence(
        self,
        db: Session,
        patient_id: str,
    ) -> float:
        """
        Compute the door-event component of the confidence score.

        Returns a value in [0.0, 1.0] that decays linearly with the age of
        the most recent door event.
        """
        recent = self._get_recent_door_history(db, patient_id, limit=1)
        if not recent:
            return 0.0

        age = self._event_age_seconds(recent[0].timestamp)
        if age >= DOOR_EVENT_MAX_AGE_SECONDS:
            return 0.0

        return 1.0 - (age / DOOR_EVENT_MAX_AGE_SECONDS)

    @staticmethod
    def _distance_confidence(distance: Optional[float]) -> float:
        """
        Compute the distance component of the confidence score.

        Returns 1.0 when the patient is within ``CLOSE_DISTANCE_METRES``,
        0.0 when beyond ``FAR_DISTANCE_METRES``, and linearly interpolated
        between those bounds.  Returns 0.0 when no distance is available.
        """
        if distance is None:
            return 0.0

        if distance <= CLOSE_DISTANCE_METRES:
            return 1.0

        if distance >= FAR_DISTANCE_METRES:
            return 0.0

        # Linear interpolation between close and far thresholds
        span = FAR_DISTANCE_METRES - CLOSE_DISTANCE_METRES
        return 1.0 - (distance - CLOSE_DISTANCE_METRES) / span

    def _detect_significant_movement(
        self,
        db: Session,
        patient_id: str,
        current_distance: float,
    ) -> bool:
        """
        Return ``True`` if the patient's distance has changed significantly
        compared to the most recent location history entry.

        Requirement 6.3: flag potential room changes when Bluetooth distance
        indicates significant movement.
        """
        last_location = (
            db.query(models.LocationHistory)
            .filter(models.LocationHistory.patient_id == patient_id)
            .filter(models.LocationHistory.estimated_distance.isnot(None))
            .order_by(models.LocationHistory.timestamp.desc())
            .first()
        )

        if last_location is None or last_location.estimated_distance is None:
            return False

        delta = abs(current_distance - last_location.estimated_distance)
        return delta >= SIGNIFICANT_MOVEMENT_THRESHOLD_METRES
