import time
from typing import Optional, Dict, Any

from fastapi import FastAPI, UploadFile, File, Body
from fastapi.responses import Response, StreamingResponse, JSONResponse
from pydantic import BaseModel, Field

from servo_math import BucketSpec, ServoSpec, water_ml_to_servo_angle
from weather_client import get_weather_snapshot
from qnx_client import send_servo_command_to_qnx

app = FastAPI(title="Plant Bridge Backend", version="0.1.1")

STATE: Dict[str, Any] = {
    "location": None,            # {"lat":..., "lon":..., "source":...}
    "latest_telemetry": None,    # dict
    "latest_frame": None,        # bytes (jpeg)
    "latest_frame_ts": None,     # float
    "last_water_command": None,  # dict
    "latest_decision": None,     # dict posted by Solace (orchestrator/advisor)
}

BUCKET = BucketSpec(diameter_cm=2.7, height_cm=3.8)
SERVO = ServoSpec(min_angle_deg=0.0, max_angle_deg=90.0)

# ---------------- Models ----------------

class LocationIn(BaseModel):
    lat: float
    lon: float
    source: str = Field(default="browser_geolocation")  # browser_geolocation | ip_fallback
    label: Optional[str] = None

class TelemetryIn(BaseModel):
    plant_id: str
    ts: str  # ISO8601
    sensors: Dict[str, Any]  # expects humidity_percent, temperature_c, pressure_hpa, etc.
    vision: Optional[Dict[str, Any]] = None  # keeps_form, wilt_score, lean_angle_deg

class WaterCommandIn(BaseModel):
    plant_id: str
    water_ml: float
    reason: Optional[str] = None
    source: str = "sam"

# ---------------- Health ----------------

@app.get("/health")
def health():
    return {"ok": True, "time": time.time()}

# ---------------- Location + Weather ----------------

@app.post("/location")
def set_location(loc: LocationIn):
    STATE["location"] = loc.model_dump()
    return {"status": "ok", "location": STATE["location"]}

@app.get("/location")
def get_location():
    return {"location": STATE["location"]}

@app.get("/weather")
async def weather():
    loc = STATE["location"]
    if not loc:
        return JSONResponse(
            status_code=400,
            content={"error": "no_location", "message": "POST /location with {lat, lon} first."},
        )
    return await get_weather_snapshot(lat=loc["lat"], lon=loc["lon"])

# ---------------- Telemetry ingest ----------------

@app.post("/ingest/telemetry")
def ingest_telemetry(t: TelemetryIn):
    STATE["latest_telemetry"] = t.model_dump()
    return {"status": "ok", "stored": True}

@app.get("/plant/latest")
def plant_latest():
    return {"telemetry": STATE["latest_telemetry"]}

# ---------------- Camera ingest ----------------

@app.post("/ingest/frame")
async def ingest_frame(file: UploadFile = File(...)):
    data = await file.read()
    STATE["latest_frame"] = data
    STATE["latest_frame_ts"] = time.time()
    return {"status": "ok", "bytes": len(data)}

@app.get("/video/latest.jpg")
def latest_jpg():
    if not STATE["latest_frame"]:
        return Response(status_code=404)
    return Response(content=STATE["latest_frame"], media_type="image/jpeg")

@app.get("/video/stream.mjpeg")
def mjpeg_stream():
    boundary = "frame"

    def gen():
        last_sent = 0.0
        while True:
            time.sleep(0.2)  # cap fps
            ts = STATE["latest_frame_ts"] or 0.0
            frame = STATE["latest_frame"]
            if not frame or ts == last_sent:
                continue
            last_sent = ts
            yield (
                b"--" + boundary.encode() + b"\r\n"
                b"Content-Type: image/jpeg\r\n"
                b"Content-Length: " + str(len(frame)).encode() + b"\r\n\r\n" +
                frame + b"\r\n"
            )

    return StreamingResponse(gen(), media_type=f"multipart/x-mixed-replace; boundary={boundary}")

# ---------------- Solace decision ingest (Solace -> Backend -> Frontend) ----------------

@app.post("/ingest/decision")
def ingest_decision(payload: Dict[str, Any] = Body(...)):
    """
    Solace Orchestrator (or any agent) can POST its final decision here.
    The frontend reads it from GET /decision/latest.
    """
    STATE["latest_decision"] = payload
    return {"status": "ok", "stored": True, "ts_server": time.time()}

@app.get("/decision/latest")
def decision_latest():
    return {"decision": STATE["latest_decision"]}

# ---------------- Water command (SAM -> Backend -> QNX later) ----------------

@app.post("/qnx/command/water")
async def command_water(cmd: WaterCommandIn):
    mapping = water_ml_to_servo_angle(cmd.water_ml, BUCKET, SERVO)

    payload = {
        "plant_id": cmd.plant_id,
        "water_ml": mapping["clamped_water_ml"],
        "servo_angle_deg": mapping["servo_angle_deg"],
        "bucket_capacity_ml": mapping["bucket_capacity_ml"],
        "fill_fraction": mapping["fill_fraction"],
        "reason": cmd.reason,
        "source": cmd.source,
        "ts_server": time.time(),
    }

    STATE["last_water_command"] = payload
    qnx_result = await send_servo_command_to_qnx(payload)
    return {"status": "ok", "command": payload, "qnx": qnx_result}

@app.get("/qnx/last_command")
def last_command():
    return {"last_water_command": STATE["last_water_command"]}
