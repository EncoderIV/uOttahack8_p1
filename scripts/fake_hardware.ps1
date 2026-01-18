param(
  [string]$BackendUrl = "http://localhost:9000"
)

$ErrorActionPreference = "Stop"

function PostJson($url, $obj) {
  $json = $obj | ConvertTo-Json -Depth 10
  Invoke-RestMethod -Method Post -Uri $url -ContentType "application/json" -Body $json | Out-Null
}

# 1) Location (so weather works even if browser blocks geo)
PostJson "$BackendUrl/location" @{
  lat = 45.4765
  lon = -75.7013
  source = "manual_test"
  label = "Gatineau, QC"
}

# 2) Fake telemetry (temp/humidity/pressure + vision)
PostJson "$BackendUrl/ingest/telemetry" @{
  plant_id = "basil_01"
  ts = (Get-Date).ToString("s") + "Z"
  sensors = @{
    humidity_percent = 28
    temperature_c = 38
    temperature_uncertainty_c = 2
    pressure_hpa = 1012.4
  }
  vision = @{
    keeps_form = $false
    wilt_score = 0.72
    lean_angle_deg = 18
  }
}

# 3) Fake Solace decision (so UI shows it even before Orchestrator posts)
PostJson "$BackendUrl/ingest/decision" @{
  summary = "ESCALATE: heat + low humidity + form loss (demo payload)"
  water_recommendation_ml = 3
  servo_angle_deg = 12.4
  plant_state = @{
    state = "ESCALATE"
    confidence = 0.95
  }
  ts = (Get-Date).ToString("s") + "Z"
}

Write-Host "Injected fake location, telemetry, and decision into backend."
