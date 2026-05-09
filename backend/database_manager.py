"""
Database Management System for Halo Watch
Provides utilities for database migrations, seeding, backup, and maintenance.
"""

import os
import shutil
import sqlite3
import subprocess
import logging
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, Any, Optional, List
import json

from sqlalchemy import create_engine, text, inspect
from sqlalchemy.orm import sessionmaker
from alembic.config import Config
from alembic import command
from alembic.runtime.migration import MigrationContext
from alembic.script import ScriptDirectory

from database import SQLALCHEMY_DATABASE_URL, SessionLocal
from models import Base
import seed

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class DatabaseManager:
    """Comprehensive database management system."""
    
    def __init__(self, database_url: Optional[str] = None):
        self.database_url = database_url or SQLALCHEMY_DATABASE_URL
        self.engine = create_engine(self.database_url)
        self.SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=self.engine)
        
        # Alembic configuration
        self.alembic_cfg = Config("alembic.ini")
        self.alembic_cfg.set_main_option("sqlalchemy.url", self.database_url)
    
    def get_current_revision(self) -> Optional[str]:
        """Get the current database revision."""
        try:
            with self.engine.connect() as connection:
                context = MigrationContext.configure(connection)
                return context.get_current_revision()
        except Exception as e:
            logger.error(f"Error getting current revision: {e}")
            return None
    
    def get_head_revision(self) -> Optional[str]:
        """Get the head revision from migration scripts."""
        try:
            script = ScriptDirectory.from_config(self.alembic_cfg)
            return script.get_current_head()
        except Exception as e:
            logger.error(f"Error getting head revision: {e}")
            return None
    
    def is_database_up_to_date(self) -> bool:
        """Check if database is up to date with migrations."""
        current = self.get_current_revision()
        head = self.get_head_revision()
        return current == head and current is not None
    
    def create_migration(self, message: str, autogenerate: bool = True) -> Dict[str, Any]:
        """Create a new migration."""
        try:
            if autogenerate:
                command.revision(self.alembic_cfg, message=message, autogenerate=True)
            else:
                command.revision(self.alembic_cfg, message=message)
            
            return {
                "success": True,
                "message": f"Migration '{message}' created successfully"
            }
        except Exception as e:
            logger.error(f"Error creating migration: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def run_migrations(self, target_revision: Optional[str] = None) -> Dict[str, Any]:
        """Run database migrations."""
        try:
            if target_revision:
                command.upgrade(self.alembic_cfg, target_revision)
            else:
                command.upgrade(self.alembic_cfg, "head")
            
            return {
                "success": True,
                "message": "Migrations completed successfully",
                "current_revision": self.get_current_revision()
            }
        except Exception as e:
            logger.error(f"Error running migrations: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def rollback_migration(self, target_revision: str) -> Dict[str, Any]:
        """Rollback to a specific migration."""
        try:
            command.downgrade(self.alembic_cfg, target_revision)
            return {
                "success": True,
                "message": f"Rolled back to revision {target_revision}",
                "current_revision": self.get_current_revision()
            }
        except Exception as e:
            logger.error(f"Error rolling back migration: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def get_migration_history(self) -> List[Dict[str, Any]]:
        """Get migration history."""
        try:
            script = ScriptDirectory.from_config(self.alembic_cfg)
            revisions = []
            
            for revision in script.walk_revisions():
                revisions.append({
                    "revision": revision.revision,
                    "down_revision": revision.down_revision,
                    "branch_labels": revision.branch_labels,
                    "message": revision.doc,
                    "create_date": getattr(revision, 'create_date', None)
                })
            
            return revisions
        except Exception as e:
            logger.error(f"Error getting migration history: {e}")
            return []
    
    def initialize_database(self) -> Dict[str, Any]:
        """Initialize database with tables and initial data."""
        try:
            # Create all tables
            Base.metadata.create_all(bind=self.engine)
            
            # Run any pending migrations
            migration_result = self.run_migrations()
            if not migration_result["success"]:
                return migration_result
            
            # Seed initial data
            db = self.SessionLocal()
            try:
                seed.seed_initial_data(db)
                db.commit()
            finally:
                db.close()
            
            return {
                "success": True,
                "message": "Database initialized successfully",
                "current_revision": self.get_current_revision()
            }
        except Exception as e:
            logger.error(f"Error initializing database: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def create_backup(self, backup_path: Optional[str] = None) -> Dict[str, Any]:
        """Create database backup."""
        try:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            
            if backup_path is None:
                backup_dir = Path("backups")
                backup_dir.mkdir(exist_ok=True)
                backup_path = backup_dir / f"halo_watch_backup_{timestamp}.sql"
            
            if "sqlite" in self.database_url.lower():
                return self._backup_sqlite(backup_path, timestamp)
            elif "postgresql" in self.database_url.lower():
                return self._backup_postgresql(backup_path, timestamp)
            else:
                return {
                    "success": False,
                    "error": "Unsupported database type for backup"
                }
        except Exception as e:
            logger.error(f"Error creating backup: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def _backup_sqlite(self, backup_path: Path, timestamp: str) -> Dict[str, Any]:
        """Create SQLite backup."""
        try:
            # Extract database path from URL
            db_path = self.database_url.replace("sqlite:///", "")
            
            if os.path.exists(db_path):
                # Copy the SQLite file
                shutil.copy2(db_path, str(backup_path).replace(".sql", ".db"))
                
                # Also create SQL dump
                with sqlite3.connect(db_path) as conn:
                    with open(backup_path, 'w') as f:
                        for line in conn.iterdump():
                            f.write(f"{line}\n")
                
                return {
                    "success": True,
                    "message": "SQLite backup created successfully",
                    "backup_path": str(backup_path),
                    "timestamp": timestamp
                }
            else:
                return {
                    "success": False,
                    "error": f"Database file not found: {db_path}"
                }
        except Exception as e:
            return {
                "success": False,
                "error": f"SQLite backup failed: {str(e)}"
            }
    
    def _backup_postgresql(self, backup_path: Path, timestamp: str) -> Dict[str, Any]:
        """Create PostgreSQL backup."""
        try:
            # Parse database URL for pg_dump
            import urllib.parse
            parsed = urllib.parse.urlparse(self.database_url)
            
            env = os.environ.copy()
            env['PGPASSWORD'] = parsed.password
            
            cmd = [
                'pg_dump',
                '-h', parsed.hostname,
                '-p', str(parsed.port or 5432),
                '-U', parsed.username,
                '-d', parsed.path[1:],  # Remove leading slash
                '-f', str(backup_path),
                '--verbose'
            ]
            
            result = subprocess.run(cmd, env=env, capture_output=True, text=True)
            
            if result.returncode == 0:
                return {
                    "success": True,
                    "message": "PostgreSQL backup created successfully",
                    "backup_path": str(backup_path),
                    "timestamp": timestamp
                }
            else:
                return {
                    "success": False,
                    "error": f"pg_dump failed: {result.stderr}"
                }
        except Exception as e:
            return {
                "success": False,
                "error": f"PostgreSQL backup failed: {str(e)}"
            }
    
    def restore_backup(self, backup_path: str) -> Dict[str, Any]:
        """Restore database from backup."""
        try:
            if not os.path.exists(backup_path):
                return {
                    "success": False,
                    "error": f"Backup file not found: {backup_path}"
                }
            
            if "sqlite" in self.database_url.lower():
                return self._restore_sqlite(backup_path)
            elif "postgresql" in self.database_url.lower():
                return self._restore_postgresql(backup_path)
            else:
                return {
                    "success": False,
                    "error": "Unsupported database type for restore"
                }
        except Exception as e:
            logger.error(f"Error restoring backup: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def _restore_sqlite(self, backup_path: str) -> Dict[str, Any]:
        """Restore SQLite backup."""
        try:
            db_path = self.database_url.replace("sqlite:///", "")
            
            # Create backup of current database
            if os.path.exists(db_path):
                backup_current = f"{db_path}.backup_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
                shutil.copy2(db_path, backup_current)
            
            # Restore from backup
            if backup_path.endswith('.db'):
                # Direct file copy
                shutil.copy2(backup_path, db_path)
            else:
                # SQL dump restore
                with sqlite3.connect(db_path) as conn:
                    with open(backup_path, 'r') as f:
                        conn.executescript(f.read())
            
            return {
                "success": True,
                "message": "SQLite database restored successfully"
            }
        except Exception as e:
            return {
                "success": False,
                "error": f"SQLite restore failed: {str(e)}"
            }
    
    def _restore_postgresql(self, backup_path: str) -> Dict[str, Any]:
        """Restore PostgreSQL backup."""
        try:
            import urllib.parse
            parsed = urllib.parse.urlparse(self.database_url)
            
            env = os.environ.copy()
            env['PGPASSWORD'] = parsed.password
            
            cmd = [
                'psql',
                '-h', parsed.hostname,
                '-p', str(parsed.port or 5432),
                '-U', parsed.username,
                '-d', parsed.path[1:],
                '-f', backup_path
            ]
            
            result = subprocess.run(cmd, env=env, capture_output=True, text=True)
            
            if result.returncode == 0:
                return {
                    "success": True,
                    "message": "PostgreSQL database restored successfully"
                }
            else:
                return {
                    "success": False,
                    "error": f"psql restore failed: {result.stderr}"
                }
        except Exception as e:
            return {
                "success": False,
                "error": f"PostgreSQL restore failed: {str(e)}"
            }
    
    def cleanup_old_data(self, retention_days: int = 90) -> Dict[str, Any]:
        """Clean up old data based on retention policy."""
        try:
            cutoff_date = datetime.utcnow() - timedelta(days=retention_days)
            db = self.SessionLocal()
            
            try:
                # Clean up old location history
                location_deleted = db.execute(
                    text("DELETE FROM location_history WHERE timestamp < :cutoff"),
                    {"cutoff": cutoff_date}
                ).rowcount
                
                # Clean up old access logs
                access_deleted = db.execute(
                    text("DELETE FROM access_logs WHERE timestamp < :cutoff"),
                    {"cutoff": cutoff_date}
                ).rowcount
                
                # Clean up resolved alerts older than retention period
                alerts_deleted = db.execute(
                    text("DELETE FROM alerts WHERE status = 'resolved' AND resolved_at < :cutoff"),
                    {"cutoff": cutoff_date}
                ).rowcount
                
                db.commit()
                
                return {
                    "success": True,
                    "message": "Data cleanup completed successfully",
                    "records_deleted": {
                        "location_history": location_deleted,
                        "access_logs": access_deleted,
                        "alerts": alerts_deleted
                    },
                    "cutoff_date": cutoff_date.isoformat()
                }
            finally:
                db.close()
        except Exception as e:
            logger.error(f"Error during data cleanup: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def get_database_stats(self) -> Dict[str, Any]:
        """Get database statistics and performance metrics."""
        try:
            db = self.SessionLocal()
            try:
                stats = {}
                
                # Table row counts
                tables = ['patients', 'rooms', 'receivers', 'alerts', 'access_logs', 'location_history', 'users', 'zones']
                for table in tables:
                    try:
                        count = db.execute(text(f"SELECT COUNT(*) FROM {table}")).scalar()
                        stats[f"{table}_count"] = count
                    except Exception:
                        stats[f"{table}_count"] = 0
                
                # Database size (SQLite specific)
                if "sqlite" in self.database_url.lower():
                    db_path = self.database_url.replace("sqlite:///", "")
                    if os.path.exists(db_path):
                        stats["database_size_bytes"] = os.path.getsize(db_path)
                        stats["database_size_mb"] = round(stats["database_size_bytes"] / (1024 * 1024), 2)
                
                # Recent activity
                stats["recent_alerts"] = db.execute(
                    text("SELECT COUNT(*) FROM alerts WHERE created_at > datetime('now', '-24 hours')")
                ).scalar()
                
                stats["recent_location_updates"] = db.execute(
                    text("SELECT COUNT(*) FROM location_history WHERE timestamp > datetime('now', '-1 hour')")
                ).scalar()
                
                return {
                    "success": True,
                    "stats": stats,
                    "timestamp": datetime.utcnow().isoformat()
                }
            finally:
                db.close()
        except Exception as e:
            logger.error(f"Error getting database stats: {e}")
            return {
                "success": False,
                "error": str(e)
            }
    
    def optimize_database(self) -> Dict[str, Any]:
        """Optimize database performance."""
        try:
            db = self.SessionLocal()
            try:
                if "sqlite" in self.database_url.lower():
                    # SQLite optimization
                    db.execute(text("VACUUM"))
                    db.execute(text("ANALYZE"))
                    db.commit()
                    
                    return {
                        "success": True,
                        "message": "SQLite database optimized (VACUUM and ANALYZE completed)"
                    }
                elif "postgresql" in self.database_url.lower():
                    # PostgreSQL optimization
                    db.execute(text("VACUUM ANALYZE"))
                    db.commit()
                    
                    return {
                        "success": True,
                        "message": "PostgreSQL database optimized (VACUUM ANALYZE completed)"
                    }
                else:
                    return {
                        "success": False,
                        "error": "Database optimization not supported for this database type"
                    }
            finally:
                db.close()
        except Exception as e:
            logger.error(f"Error optimizing database: {e}")
            return {
                "success": False,
                "error": str(e)
            }


# CLI interface for database management
if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Halo Watch Database Manager")
    parser.add_argument("command", choices=[
        "init", "migrate", "rollback", "backup", "restore", "cleanup", "stats", "optimize"
    ], help="Command to execute")
    parser.add_argument("--target", help="Target revision for rollback")
    parser.add_argument("--backup-path", help="Path for backup/restore operations")
    parser.add_argument("--retention-days", type=int, default=90, help="Data retention period in days")
    parser.add_argument("--database-url", help="Database URL (overrides default)")
    
    args = parser.parse_args()
    
    db_manager = DatabaseManager(args.database_url)
    
    if args.command == "init":
        result = db_manager.initialize_database()
    elif args.command == "migrate":
        result = db_manager.run_migrations()
    elif args.command == "rollback":
        if not args.target:
            print("Error: --target revision required for rollback")
            exit(1)
        result = db_manager.rollback_migration(args.target)
    elif args.command == "backup":
        result = db_manager.create_backup(args.backup_path)
    elif args.command == "restore":
        if not args.backup_path:
            print("Error: --backup-path required for restore")
            exit(1)
        result = db_manager.restore_backup(args.backup_path)
    elif args.command == "cleanup":
        result = db_manager.cleanup_old_data(args.retention_days)
    elif args.command == "stats":
        result = db_manager.get_database_stats()
    elif args.command == "optimize":
        result = db_manager.optimize_database()
    
    print(json.dumps(result, indent=2, default=str))