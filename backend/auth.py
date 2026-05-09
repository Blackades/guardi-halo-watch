from datetime import datetime, timedelta
from typing import Optional, Dict, Any
from jose import JWTError, jwt
from passlib.context import CryptContext
from fastapi import HTTPException, status, Depends
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from sqlalchemy.orm import Session
from . import models
from .database import SessionLocal

# Configuration
SECRET_KEY = "your-secret-key-change-in-production"  # Should be from environment variable
ALGORITHM = "HS256"
ACCESS_TOKEN_EXPIRE_MINUTES = 30
REFRESH_TOKEN_EXPIRE_DAYS = 7
ACCOUNT_LOCKOUT_MINUTES = 15
MAX_FAILED_ATTEMPTS = 3

# Password hashing
pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")

# JWT Bearer token scheme
# auto_error=False so that we can return a consistent 401 response
# from our own logic instead of FastAPI raising a 403 for missing creds.
security = HTTPBearer(auto_error=False)

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

class AuthenticationService:
    @staticmethod
    def verify_password(plain_password: str, hashed_password: str) -> bool:
        """Verify a plain password against its hash."""
        return pwd_context.verify(plain_password, hashed_password)

    @staticmethod
    def get_password_hash(password: str) -> str:
        """Hash a password."""
        return pwd_context.hash(password)

    @staticmethod
    def create_access_token(data: Dict[str, Any], expires_delta: Optional[timedelta] = None) -> str:
        """
        Create a JWT access token.

        The caller is expected to pass a dictionary containing at least a
        ``sub`` (subject / username) claim. Any additional fields (e.g.
        ``username``, ``full_name``, ``email``, ``role``) are included in the
        token payload so that the frontend can derive the current user without
        making a separate API call.
        """
        to_encode = data.copy()
        if expires_delta:
            expire = datetime.utcnow() + expires_delta
        else:
            expire = datetime.utcnow() + timedelta(minutes=ACCESS_TOKEN_EXPIRE_MINUTES)

        to_encode.update({"exp": expire, "type": "access"})
        encoded_jwt = jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)
        return encoded_jwt

    @staticmethod
    def create_refresh_token(data: Dict[str, Any]) -> str:
        """
        Create a JWT refresh token.

        The payload mirrors the access token (including ``sub`` and user
        details) but with a longer expiration time and a different ``type`` so
        that it can be validated separately.
        """
        to_encode = data.copy()
        expire = datetime.utcnow() + timedelta(days=REFRESH_TOKEN_EXPIRE_DAYS)
        to_encode.update({"exp": expire, "type": "refresh"})
        encoded_jwt = jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)
        return encoded_jwt

    @staticmethod
    def verify_token(token: str, token_type: str = "access") -> Optional[Dict[str, Any]]:
        """Verify and decode a JWT token."""
        try:
            payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
            if payload.get("type") != token_type:
                return None
            return payload
        except JWTError:
            return None

    @staticmethod
    def authenticate_user(db: Session, username: str, password: str) -> Optional[models.User]:
        """Authenticate a user with username and password."""
        user = db.query(models.User).filter(models.User.username == username).first()
        if not user:
            return None
        
        # Check if account is locked
        if user.account_locked_until and user.account_locked_until > datetime.utcnow():
            return None
        
        # Check if account is active
        if not user.is_active:
            return None
        
        # Verify password
        if not AuthenticationService.verify_password(password, user.password_hash):
            # Increment failed attempts
            user.failed_login_attempts += 1
            if user.failed_login_attempts >= MAX_FAILED_ATTEMPTS:
                user.account_locked_until = datetime.utcnow() + timedelta(minutes=ACCOUNT_LOCKOUT_MINUTES)
            db.commit()
            return None
        
        # Reset failed attempts on successful login
        user.failed_login_attempts = 0
        user.account_locked_until = None
        user.last_login = datetime.utcnow()
        db.commit()
        
        return user

    @staticmethod
    def get_current_user(
        credentials: HTTPAuthorizationCredentials = Depends(security),
        db: Session = Depends(get_db),
    ) -> models.User:
        """Get current authenticated user from JWT token."""
        credentials_exception = HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Could not validate credentials",
            headers={"WWW-Authenticate": "Bearer"},
        )

        # If no Authorization header is provided, treat as unauthenticated.
        if credentials is None:
            raise credentials_exception

        payload = AuthenticationService.verify_token(credentials.credentials, "access")
        if payload is None:
            raise credentials_exception

        username: str | None = payload.get("sub")  # type: ignore[assignment]
        if username is None:
            raise credentials_exception

        user = db.query(models.User).filter(models.User.username == username).first()
        if user is None or not user.is_active:
            raise credentials_exception

        return user

def require_role(required_roles: list[str]):
    """Decorator to require specific roles for endpoint access."""
    def role_checker(current_user: models.User = Depends(AuthenticationService.get_current_user)):
        if current_user.role not in required_roles:
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Insufficient permissions"
            )
        return current_user
    return role_checker

# Convenience functions for common role requirements
def require_admin(current_user: models.User = Depends(AuthenticationService.get_current_user)):
    return require_role(["admin"])(current_user)

def require_staff(current_user: models.User = Depends(AuthenticationService.get_current_user)):
    return require_role(["admin", "nurse", "doctor"])(current_user)

def require_any_role(current_user: models.User = Depends(AuthenticationService.get_current_user)):
    return require_role(["admin", "nurse", "doctor", "viewer"])(current_user)