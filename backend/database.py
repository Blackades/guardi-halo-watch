from sqlalchemy import create_engine
from sqlalchemy.engine.url import make_url
from sqlalchemy.orm import sessionmaker, declarative_base
import os
import sqlite3


# Default path for the main SQLite database file.
DEFAULT_SQLITE_URL = "sqlite:///./halo_watch.db"


def _get_sqlite_database_url() -> str:
    """
    Determine the database URL, enforcing SQLite even in production.

    If a DATABASE_URL/SQLALCHEMY_DATABASE_URL environment variable is provided
    and it points to a SQLite database, it will be used. Any non-SQLite URL
    is ignored and we fall back to the default SQLite file.
    """
    env_url = os.getenv("DATABASE_URL") or os.getenv("SQLALCHEMY_DATABASE_URL")
    if env_url:
        try:
            url = make_url(env_url)
            if url.drivername.startswith("sqlite"):
                return str(url)
        except Exception:
            # Fall back to default on any parsing error
            pass
    return DEFAULT_SQLITE_URL


def ensure_sqlite_schema() -> None:
    """
    Lightweight SQLite schema patching for existing databases.

    In development and demo environments it's common to reuse an existing
    SQLite file that predates newer columns. This helper adds any missing
    columns used by the ORM so that queries don't fail with
    "no such column" errors.
    """
    try:
        url = make_url(SQLALCHEMY_DATABASE_URL)
        if not url.drivername.startswith("sqlite"):
            return

        db_path = url.database
        if not db_path or not os.path.exists(db_path):
            # Nothing to patch yet; a fresh database will be created by
            # Base.metadata.create_all.
            return

        conn = sqlite3.connect(db_path)
        try:
            cur = conn.cursor()

            # ---- Patients table patches ----
            cur.execute("PRAGMA table_info(patients)")
            patient_columns = {row[1] for row in cur.fetchall()}

            statements = []
            if "isolation_level" not in patient_columns:
                statements.append(
                    "ALTER TABLE patients "
                    "ADD COLUMN isolation_level TEXT DEFAULT 'none'"
                )
            if "authorized_zones" not in patient_columns:
                statements.append(
                    "ALTER TABLE patients "
                    "ADD COLUMN authorized_zones TEXT"
                )
            if "emergency_contact" not in patient_columns:
                statements.append(
                    "ALTER TABLE patients "
                    "ADD COLUMN emergency_contact TEXT"
                )
            if "medical_notes" not in patient_columns:
                statements.append(
                    "ALTER TABLE patients "
                    "ADD COLUMN medical_notes TEXT"
                )

            # ---- Alerts table patches ----
            cur.execute("PRAGMA table_info(alerts)")
            alert_columns = {row[1] for row in cur.fetchall()}

            if "escalation_level" not in alert_columns:
                statements.append(
                    "ALTER TABLE alerts "
                    "ADD COLUMN escalation_level INTEGER DEFAULT 1"
                )
            if "acknowledged_at" not in alert_columns:
                statements.append(
                    "ALTER TABLE alerts "
                    "ADD COLUMN acknowledged_at DATETIME"
                )
            if "acknowledged_by" not in alert_columns:
                statements.append(
                    "ALTER TABLE alerts "
                    "ADD COLUMN acknowledged_by TEXT"
                )
            if "resolved_at" not in alert_columns:
                statements.append(
                    "ALTER TABLE alerts "
                    "ADD COLUMN resolved_at DATETIME"
                )
            if "resolved_by" not in alert_columns:
                statements.append(
                    "ALTER TABLE alerts "
                    "ADD COLUMN resolved_by TEXT"
                )
            if "alert_metadata" not in alert_columns:
                statements.append(
                    "ALTER TABLE alerts "
                    "ADD COLUMN alert_metadata TEXT"
                )

            # ---- LocationHistory table patches ----
            cur.execute("PRAGMA table_info(location_history)")
            location_columns = {row[1] for row in cur.fetchall()}

            if "confidence_score" not in location_columns:
                statements.append(
                    "ALTER TABLE location_history "
                    "ADD COLUMN confidence_score REAL"
                )
            if "inferred_room" not in location_columns:
                statements.append(
                    "ALTER TABLE location_history "
                    "ADD COLUMN inferred_room TEXT"
                )

            for stmt in statements:
                cur.execute(stmt)
            if statements:
                conn.commit()
        finally:
            conn.close()
    except Exception:
        # Schema patching is best-effort; any failures are logged by callers
        # if desired but should not prevent the app from starting.
        return


# Public constant used throughout the backend (and by Alembic) so that
# all environments – including production – are pinned to SQLite.
SQLALCHEMY_DATABASE_URL = _get_sqlite_database_url()

engine = create_engine(
    SQLALCHEMY_DATABASE_URL, connect_args={"check_same_thread": False}
)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

Base = declarative_base()


 