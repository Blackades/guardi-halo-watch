import sys
import os
sys.path.append(os.getcwd())
from backend.database import engine
from backend.models import Base

print("Ensuring tables exist...")
Base.metadata.create_all(bind=engine)
print("Tables updated successfully.")
