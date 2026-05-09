import pathlib
DEST = r'd:\guardi-halo-watch\.kiro\specs\multi-door-rfid-bt-positioning\design.md'
SECTIONS = []
def S(t): SECTIONS.append(t)
S("""# Design Document

## Feature: multi-door-rfid-bt-positioning

---

## Overview

This feature extends the Guardi Halo Watch system to support simultaneous monitoring of up to 7 RFID door readers on a single ESP32 Unified Node, with human-readable door names configured through an ESP32-hosted web interface. It also adds Bluetooth RSSI-based distance calculation to determine room-level proximity of BLE wristband patients, with per-node calibration parameters stored on the backend.

The design covers three layers:

- **Firmware** (`unified_node_v2.cpp`): Multi-reader RFID polling, RDM6300 frame parsing, debounce, offline buffering, BLE scanning, path-loss distance calculation, calibration wizard, and a local HTTP configuration server.
- **Backend** (`FastAPI + SQLAlchemy`): New and updated REST endpoints for reader sync, door events, location updates with distance, and calibration parameter management.
- **Frontend** (`React + TypeScript`): A read-only door configuration panel and calibration parameter display with polling.

### Key Design Decisions

1. **ESP32-hosted config server** eliminates NAT traversal: door names are assigned locally via `halowatch-{node_id}.local` and then pushed to the backend, rather than pulled from it.
2. **Offline FIFO buffer in NVS**: Door events are buffered in ESP32 non-volatile storage (up to 100 events) so no events are lost during transient connectivity failures.
3. **Stale beacon filtering**: BLE detections older than 30 seconds are excluded from Location_Update payloads to prevent stale data from misleading the dashboard.
4. **Least-squares calibration regression**: The `POST /api/calibration/apply` endpoint computes `referenceRSSI` and `pathLossExponent` from collected samples using ordinary least-squares on the linearised path-loss model.
5. **Existing models reused**: `RFIDReader`, `DoorEvent`, `DistanceCalibration`, and `LocationHistory` (with `estimated_distance`) already exist in `models.py`. No new tables are required.
""")S('## Components and Interfaces\n\n')
