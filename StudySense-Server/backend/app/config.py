"""全局配置：从 backend/.env 读取（按文件位置定位，不受启动目录影响），提供默认值"""
from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict

# backend/ 目录（config.py 的上一级）
BASE_DIR = Path(__file__).resolve().parent.parent


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=str(BASE_DIR / ".env"), env_file_encoding="utf-8"
    )

    # HTTP 服务（8000 落在 Windows Hyper-V/WinNAT 保留段 7918-8017 内会绑定失败）
    APP_HOST: str = "0.0.0.0"
    APP_PORT: int = 8800

    # MySQL
    MYSQL_HOST: str = "127.0.0.1"
    MYSQL_PORT: int = 3306
    MYSQL_USER: str = "studysense"
    MYSQL_PASSWORD: str = "studysense123"
    MYSQL_DB: str = "studysense"

    # EMQX
    EMQX_HOST: str = "127.0.0.1"
    EMQX_PORT: int = 1883

    # 管理员
    ADMIN_USERNAME: str = "admin"
    ADMIN_PASSWORD: str = "admin123"

    # JWT
    JWT_SECRET: str = "change-me"
    JWT_ALGORITHM: str = "HS256"
    JWT_EXPIRE_HOURS: int = 72

    # 数据保留
    DATA_RETENTION_DAYS: int = 7

    # 设备
    DEVICE_ID: str = "esp32_01"
    DEVICE_OFFLINE_TIMEOUT: int = 30

    @property
    def mysql_dsn(self) -> str:
        return (
            f"mysql+aiomysql://{self.MYSQL_USER}:{self.MYSQL_PASSWORD}"
            f"@{self.MYSQL_HOST}:{self.MYSQL_PORT}/{self.MYSQL_DB}?charset=utf8mb4"
        )


settings = Settings()
