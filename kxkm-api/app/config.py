"""Configuration loaded from environment variables."""

from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # API
    bmu_api_key: str = "change-me"
    bmu_api_host: str = "0.0.0.0"
    bmu_api_port: int = 8400

    # InfluxDB
    influx_url: str = "http://localhost:8086"
    influx_token: str = ""
    influx_org: str = "kxkm"
    influx_bucket: str = "bmu"

    # SQLite
    sqlite_path: str = "/data/bmu_audit.db"

    model_config = {"env_file": ".env", "env_file_encoding": "utf-8"}


settings = Settings()
