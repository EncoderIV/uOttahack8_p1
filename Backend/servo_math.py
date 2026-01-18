import math
from dataclasses import dataclass

@dataclass
class BucketSpec:
    diameter_cm: float = 2.7
    height_cm: float = 3.8

    @property
    def capacity_ml(self) -> float:
        r = self.diameter_cm / 2.0
        return math.pi * (r ** 2) * self.height_cm  # ~21.76 mL

@dataclass
class ServoSpec:
    min_angle_deg: float = 0.0   # upright (no pour)
    max_angle_deg: float = 90.0  # tipped (pour)

def clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))

def water_ml_to_servo_angle(water_ml: float, bucket: BucketSpec, servo: ServoSpec) -> dict:
    cap = bucket.capacity_ml
    w = clamp(water_ml, 0.0, cap)
    frac = 0.0 if cap <= 0 else (w / cap)
    angle = servo.min_angle_deg + frac * (servo.max_angle_deg - servo.min_angle_deg)
    return {
        "requested_water_ml": water_ml,
        "clamped_water_ml": w,
        "bucket_capacity_ml": cap,
        "fill_fraction": frac,
        "servo_angle_deg": angle,
        "note": "Linear map placeholder. Replace with calibrated curve (angle/time -> mL) when hardware is ready."
    }
