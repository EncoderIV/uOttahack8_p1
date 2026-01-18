import time
import httpx

CACHE_TTL_S = 600  # 10 minutes
_cache = {"ts": 0.0, "key": None, "data": None}

async def get_weather_snapshot(lat: float, lon: float) -> dict:
    key = (round(lat, 4), round(lon, 4))
    now = time.time()
    if _cache["data"] and _cache["key"] == key and (now - _cache["ts"] < CACHE_TTL_S):
        return {**_cache["data"], "cached": True}

    url = "https://api.open-meteo.com/v1/forecast"
    params = {
        "latitude": lat,
        "longitude": lon,
        "current": "temperature_2m,relative_humidity_2m,pressure_msl,wind_speed_10m,precipitation",
        "daily": "sunrise,sunset",
        "timezone": "auto",
    }

    async with httpx.AsyncClient(timeout=6.0) as client:
        r = await client.get(url, params=params)
        r.raise_for_status()
        j = r.json()

    current = j.get("current", {}) or {}
    daily = j.get("daily", {}) or {}

    data = {
        "location": {"lat": lat, "lon": lon},
        "ts": current.get("time"),
        "outside": {
            "temp_c": current.get("temperature_2m"),
            "humidity_percent": current.get("relative_humidity_2m"),
            "pressure_hpa": current.get("pressure_msl"),
            "wind_kph": current.get("wind_speed_10m"),
            "precip_mm": current.get("precipitation"),
        },
        "daylight": {
            "sunrise": (daily.get("sunrise") or [None])[0],
            "sunset": (daily.get("sunset") or [None])[0],
        },
        "cached": False,
    }

    _cache.update({"ts": now, "key": key, "data": data})
    return data
