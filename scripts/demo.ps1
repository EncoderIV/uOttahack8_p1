# One-click local demo launcher for uOttahack8_p1
# Starts: Backend (9000), Frontend (5173), Solace SAM (8000)
# Then injects: fake telemetry + fake decision (+ optional fake camera frame)

$ErrorActionPreference = "Stop"

function Assert-Command($name) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
    throw "Missing command '$name'. Install it and retry."
  }
}

Assert-Command python
Assert-Command docker
Assert-Command node
Assert-Command npm

$ROOT = Split-Path -Parent $PSScriptRoot
$BACKEND = Join-Path $ROOT "Backend"
$FRONTEND = Join-Path $ROOT "Frontend"
$SOLACE = Join-Path $ROOT "solace-agent-mesh-hackathon-quickstart"

Write-Host "Repo root: $ROOT"

# --- Start Backend ---
Write-Host "`n[1/4] Starting Backend..." -ForegroundColor Cyan
if (-not (Test-Path (Join-Path $BACKEND ".venv"))) {
  Write-Host "Creating backend venv..." -ForegroundColor Yellow
  Push-Location $BACKEND
  python -m venv .venv
  Pop-Location
}
Push-Location $BACKEND
& .\.venv\Scripts\Activate.ps1
if (-not (Test-Path ".\requirements.txt")) {
  throw "Backend/requirements.txt not found. Create it first."
}
pip install -r requirements.txt | Out-Null
Start-Process -WindowStyle Minimized powershell -ArgumentList @(
  "-NoExit",
  "-Command",
  "cd `"$BACKEND`"; .\.venv\Scripts\Activate.ps1; uvicorn main:app --host 0.0.0.0 --port 9000"
) | Out-Null
Pop-Location

Start-Sleep -Seconds 2

# --- Start Frontend ---
Write-Host "`n[2/4] Starting Frontend..." -ForegroundColor Cyan
Push-Location $FRONTEND
if (-not (Test-Path ".\package.json")) {
  throw "Frontend/package.json not found."
}
Start-Process -WindowStyle Minimized powershell -ArgumentList @(
  "-NoExit",
  "-Command",
  "cd `"$FRONTEND`"; npm install; npm run dev"
) | Out-Null
Pop-Location

Start-Sleep -Seconds 2

# --- Start Solace (Docker) ---
Write-Host "`n[3/4] Starting Solace (SAM)..." -ForegroundColor Cyan
Push-Location $SOLACE
if (-not (Test-Path ".\.env")) {
  Write-Host "No .env found in solace-agent-mesh-hackathon-quickstart. Creating from .env.example..." -ForegroundColor Yellow
  Copy-Item ".\.env.example" ".\.env" -Force
  Write-Host "IMPORTANT: Put your Cerebras key + BACKEND_BASE_URL in .env" -ForegroundColor Yellow
}
docker build -t sam-quickstart . | Out-Null
Start-Process -WindowStyle Minimized powershell -ArgumentList @(
  "-NoExit",
  "-Command",
  "cd `"$SOLACE`"; docker run --rm --env-file .env -v `"${PWD}\.env:/app/.env`" -p 8000:8000 -p 5002:5002 sam-quickstart"
) | Out-Null
Pop-Location

Start-Sleep -Seconds 3

# --- Inject fake data ---
Write-Host "`n[4/4] Injecting fake telemetry + decision..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot "fake_hardware.ps1") -BackendUrl "http://localhost:9000" | Out-Null

Write-Host "`nDONE ✅ Open these:" -ForegroundColor Green
Write-Host "Frontend Dashboard: http://localhost:5173"
Write-Host "Solace UI:          http://localhost:8000"
Write-Host "Backend health:     http://localhost:9000/health"
Write-Host "`nTip: If camera is blank, run scripts\fake_camera.ps1 -ImagePath .\scripts\sample.jpg"
