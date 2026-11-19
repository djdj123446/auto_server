"""密码哈希（PBKDF2，无第三方依赖）与 JWT 签发/校验"""
import hashlib
import os
import time

import jwt

from app.config import settings


def hash_password(password: str, salt: str | None = None) -> str:
    salt = salt or os.urandom(16).hex()
    dk = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), bytes.fromhex(salt), 100_000)
    return f"{salt}${dk.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        salt, _ = stored.split("$", 1)
        return hash_password(password, salt) == stored
    except ValueError:
        return False


def create_token(username: str) -> str:
    payload = {
        "sub": username,
        "exp": int(time.time()) + settings.JWT_EXPIRE_HOURS * 3600,
    }
    return jwt.encode(payload, settings.JWT_SECRET, algorithm=settings.JWT_ALGORITHM)


def decode_token(token: str) -> str | None:
    try:
        payload = jwt.decode(token, settings.JWT_SECRET, algorithms=[settings.JWT_ALGORITHM])
        return payload.get("sub")
    except jwt.PyJWTError:
        return None
