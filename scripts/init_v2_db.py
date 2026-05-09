from backend.database import SessionLocal, engine
from backend import models, seed

def init_db():
    print("Resetting and initializing database...")
    models.Base.metadata.drop_all(bind=engine)
    models.Base.metadata.create_all(bind=engine)
    db = SessionLocal()
    try:
        seed.seed_initial_data(db)
        print("Database initialized and seeded successfully.")
    finally:
        db.close()

if __name__ == "__main__":
    init_db()
