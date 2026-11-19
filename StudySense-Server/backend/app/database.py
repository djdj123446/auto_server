"""数据库引擎与会话"""
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine
from sqlalchemy.orm import DeclarativeBase

from app.config import settings

engine = create_async_engine(
    settings.mysql_dsn,
    echo=False,
    pool_pre_ping=True,   # 连接前探活，避免 MySQL 8 小时断连
    pool_recycle=3600,
)

SessionLocal = async_sessionmaker(engine, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


async def get_db():
    async with SessionLocal() as session:
        yield session
