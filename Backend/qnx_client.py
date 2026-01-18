import os
import httpx

QNX_BASE_URL = os.getenv("QNX_BASE_URL", "").strip()
QNX_MODE = os.getenv("QNX_MODE", "mock").strip().lower()  # mock | real
QNX_TIMEOUT_S = float(os.getenv("QNX_TIMEOUT_S", "3.5"))

async def send_servo_command_to_qnx(payload: dict) -> dict:
    """
    In mock mode: returns queued_mock.
    In real mode: forwards to QNX.
    Adjust endpoint path to match your teammate's QNX REST API.
    """
    if QNX_MODE != "real" or not QNX_BASE_URL:
        return {"status": "queued_mock", "forwarded": False, "qnx_base_url": QNX_BASE_URL or None, "payload": payload}

    url = f"{QNX_BASE_URL.rstrip('/')}/api/servo/water"  # TODO: match teammate endpoint
    async with httpx.AsyncClient(timeout=QNX_TIMEOUT_S) as client:
        r = await client.post(url, json=payload)
        r.raise_for_status()
        try:
            data = r.json()
        except Exception:
            data = {"text": r.text}
        return {"status": "sent", "forwarded": True, "qnx_response": data}

