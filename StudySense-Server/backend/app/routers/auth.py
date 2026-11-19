"""认证：登录获取 JWT"""
from fastapi import APIRouter, Depends, HTTPException
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import AdminUser
from app.schemas import LoginBody, TokenOut
from app.security import create_token, decode_token, verify_password

router = APIRouter(prefix="/api/auth", tags=["auth"])
bearer = HTTPBearer(auto_error=False)


async def require_admin(
    cred: HTTPAuthorizationCredentials | None = Depends(bearer),
) -> str:
    """所有需要登录的接口统一走这个依赖"""
    if cred is None:
        raise HTTPException(401, "未登录")
    username = decode_token(cred.credentials)
    if not username:
        raise HTTPException(401, "登录已过期，请重新登录")
    return username


@router.post("/login", response_model=TokenOut)
async def login(body: LoginBody, db: AsyncSession = Depends(get_db)):
    user = (await db.execute(
        select(AdminUser).where(AdminUser.username == body.username)
    )).scalar_one_or_none()
    if not user or not verify_password(body.password, user.password_hash):
        raise HTTPException(401, "用户名或密码错误")
    return TokenOut(access_token=create_token(user.username))


@router.get("/me")
async def me(username: str = Depends(require_admin)):
    return {"username": username}
