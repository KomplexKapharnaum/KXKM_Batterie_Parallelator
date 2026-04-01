"""API key authentication dependency."""

from fastapi import Depends, HTTPException, status
from fastapi.security import APIKeyHeader

from .config import settings

_api_key_header = APIKeyHeader(name="Authorization", auto_error=False)


async def require_api_key(
    api_key: str | None = Depends(_api_key_header),
) -> str:
    """Validate API key from Authorization header.

    Expected format: 'Bearer <key>' or just '<key>'.
    """
    if api_key is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing Authorization header",
        )
    # Strip optional 'Bearer ' prefix
    token = api_key.removeprefix("Bearer ").strip()
    if token != settings.bmu_api_key:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Invalid API key",
        )
    return token
