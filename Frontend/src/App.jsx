import React, { useEffect, useMemo, useState } from "react";

const API = "/api"; // proxied to backend

async function jget(path) {
  const r = await fetch(`${API}${path}`);
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j?.message || j?.error || `HTTP ${r.status}`);
  return j;
}

async function jpost(path, body) {
  const r = await fetch(`${API}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j?.message || j?.error || `HTTP ${r.status}`);
  return j;
}

const COLORS = {
  bg: "#061a12",               // deep green-black
  panel: "rgba(255,255,255,0.06)",
  panel2: "rgba(255,255,255,0.08)",
  border: "rgba(255,255,255,0.10)",
  text: "rgba(255,255,255,0.92)",
  subtext: "rgba(255,255,255,0.70)",
  green: "#22c55e",
  sky: "#38bdf8",
  amber: "#fbbf24",
  red: "#fb7185"
};

function Chip({ tone = "ok", children }) {
  const map = {
    ok: { a: COLORS.green, bg: "rgba(34,197,94,0.14)" },
    warn: { a: COLORS.amber, bg: "rgba(251,191,36,0.14)" },
    err: { a: COLORS.red, bg: "rgba(251,113,133,0.14)" },
    info: { a: COLORS.sky, bg: "rgba(56,189,248,0.14)" }
  };
  const c = map[tone] || map.info;
  return (
    <span
      style={{
        display: "inline-flex",
        alignItems: "center",
        gap: 8,
        padding: "8px 12px",
        borderRadius: 999,
        border: `1px solid rgba(255,255,255,0.10)`,
        background: c.bg,
        color: COLORS.text,
        fontSize: 12,
        fontWeight: 700,
        letterSpacing: "0.01em"
      }}
    >
      <span
        style={{
          width: 8,
          height: 8,
          borderRadius: 999,
          background: c.a,
          boxShadow: "0 0 0 3px rgba(255,255,255,0.06)"
        }}
      />
      {children}
    </span>
  );
}

function Card({ title, subtitle, accent = COLORS.green, children }) {
  return (
    <div
      style={{
        borderRadius: 22,
        background: COLORS.panel,
        border: `1px solid ${COLORS.border}`,
        overflow: "hidden",
        position: "relative"
      }}
    >
      {/* Accent top line (poppy but minimal) */}
      <div
        style={{
          height: 3,
          background: `linear-gradient(90deg, ${accent}, rgba(255,255,255,0))`
        }}
      />
      <div style={{ padding: 16 }}>
        <div style={{ display: "flex", justifyContent: "space-between", gap: 12 }}>
          <div>
            <div style={{ fontSize: 15, fontWeight: 900, color: COLORS.text }}>
              {title}
            </div>
            {subtitle ? (
              <div style={{ marginTop: 6, fontSize: 12, color: COLORS.subtext }}>
                {subtitle}
              </div>
            ) : null}
          </div>
        </div>
        <div style={{ marginTop: 14 }}>{children}</div>
      </div>
    </div>
  );
}

function KV({ label, value, mono = false }) {
  return (
    <div style={{ display: "grid", gridTemplateColumns: "140px 1fr", gap: 10, padding: "5px 0" }}>
      <div style={{ fontSize: 12, color: COLORS.subtext, fontWeight: 800 }}>
        {label}
      </div>
      <div
        style={{
          fontSize: 14,
          color: COLORS.text,
          fontFamily: mono ? "ui-monospace, Menlo, Consolas, monospace" : "inherit"
        }}
      >
        {value ?? "-"}
      </div>
    </div>
  );
}

export default function App() {
  const [locationStatus, setLocationStatus] = useState("pending");
  const [telemetry, setTelemetry] = useState(null);
  const [weather, setWeather] = useState(null);
  const [lastCmd, setLastCmd] = useState(null);
  const [decision, setDecision] = useState(null);
  const [health, setHealth] = useState({ backend: "pending" });
  const [err, setErr] = useState("");

  const mjpegUrl = useMemo(() => `${API}/video/stream.mjpeg`, []);

  // Health
  useEffect(() => {
    let cancelled = false;
    const t = setInterval(async () => {
      try {
        await jget("/health");
        if (!cancelled) setHealth({ backend: "ok" });
      } catch {
        if (!cancelled) setHealth({ backend: "failed" });
      }
    }, 3000);
    return () => { cancelled = true; clearInterval(t); };
  }, []);

  // Geolocation -> backend
  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const pos = await new Promise((resolve, reject) => {
          navigator.geolocation.getCurrentPosition(resolve, reject, {
            enableHighAccuracy: true,
            timeout: 8000,
            maximumAge: 60_000
          });
        });

        await jpost("/location", {
          lat: pos.coords.latitude,
          lon: pos.coords.longitude,
          source: "browser_geolocation"
        });

        if (!cancelled) setLocationStatus("ok");
      } catch (e) {
        if (!cancelled) {
          setLocationStatus("failed");
          setErr(`Location blocked/unavailable: ${e.message}`);
        }
      }
    })();
    return () => { cancelled = true; };
  }, []);

  // Poll telemetry + command + decision
  useEffect(() => {
    let cancelled = false;
    const t = setInterval(async () => {
      try {
        const [a, b, c] = await Promise.all([
          jget("/plant/latest"),
          jget("/qnx/last_command"),
          jget("/decision/latest").catch(() => ({ decision: null }))
        ]);
        if (!cancelled) {
          setTelemetry(a.telemetry || null);
          setLastCmd(b.last_water_command || null);
          setDecision(c.decision || null);
        }
      } catch (e) {
        if (!cancelled) setErr(e.message);
      }
    }, 1200);
    return () => { cancelled = true; clearInterval(t); };
  }, []);

  // Poll weather
  useEffect(() => {
    let cancelled = false;
    const fetchWeather = async () => {
      try {
        const w = await jget("/weather");
        if (!cancelled) setWeather(w);
      } catch {
        // no_location early is fine
      }
    };
    fetchWeather();
    const t = setInterval(fetchWeather, 30_000);
    return () => { cancelled = true; clearInterval(t); };
  }, []);

  const sensors = telemetry?.sensors || {};
  const vision = telemetry?.vision || {};

  const backendTone = health.backend === "ok" ? "ok" : health.backend === "failed" ? "err" : "info";
  const locTone = locationStatus === "ok" ? "ok" : locationStatus === "failed" ? "warn" : "info";

  const state =
    decision?.plant_state?.state ||
    decision?.state ||
    (telemetry ? "LIVE" : "NO DATA");

  const stateTone =
    state === "OK" || state === "LIVE" ? "ok" :
    state === "ESCALATE" ? "err" :
    state === "CHECK_SENSOR" ? "warn" :
    "info";

  const waterMl =
    decision?.water_recommendation_ml ??
    decision?.command?.water_ml ??
    null;

  const servoDeg =
    decision?.servo_angle_deg ??
    decision?.command?.servo_angle_deg ??
    lastCmd?.servo_angle_deg ??
    null;

  const summary =
    decision?.summary ??
    (decision?.plant_state ? "Solace pipeline decision received." : null);

  return (
    <div
      style={{
        minHeight: "100vh",
        background: COLORS.bg,
        color: COLORS.text,
        fontFamily: "system-ui",
        margin: 0
      }}
    >
      {/* Subtle vibrant background (no images, no heavy shadow) */}
      <div
        style={{
          position: "fixed",
          inset: 0,
          pointerEvents: "none",
          background:
            "radial-gradient(700px 320px at 10% 5%, rgba(34,197,94,0.20) 0%, rgba(0,0,0,0) 60%)," +
            "radial-gradient(650px 300px at 92% 8%, rgba(56,189,248,0.18) 0%, rgba(0,0,0,0) 55%)," +
            "radial-gradient(700px 340px at 40% 115%, rgba(251,191,36,0.14) 0%, rgba(0,0,0,0) 55%)"
        }}
      />

      {/* Full-bleed layout (no big side whitespace) */}
      <div style={{ position: "relative", padding: 18 }}>
        {/* Header */}
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "flex-end",
            flexWrap: "wrap",
            gap: 12,
            marginBottom: 14
          }}
        >
          <div>
            <div style={{ fontSize: 26, fontWeight: 950, letterSpacing: "-0.03em" }}>
              Crop Survey
            </div>
            <div style={{ marginTop: 6, fontSize: 12, color: COLORS.subtext }}>
              Modern field view • camera-first • minimal input • vibrant status cues
            </div>
          </div>

          <div style={{ display: "flex", gap: 10, flexWrap: "wrap" }}>
            <Chip tone={backendTone}>Backend: {health.backend}</Chip>
            <Chip tone={locTone}>Location: {locationStatus}</Chip>
            <Chip tone={stateTone}>Status: {state}</Chip>
          </div>
        </div>

        {err ? (
          <div style={{ marginBottom: 14 }}>
            <Chip tone="warn">{err}</Chip>
          </div>
        ) : null}

        {/* Main grid: camera dominates */}
        <div
          style={{
            display: "grid",
            gridTemplateColumns: "1.7fr 0.9fr",
            gap: 14
          }}
        >
          <Card
            title="Live Camera"
            subtitle="Primary field view (streamed from backend)"
            accent={COLORS.amber}
          >
            <div
              style={{
                borderRadius: 18,
                overflow: "hidden",
                border: `1px solid ${COLORS.border}`,
                background: COLORS.panel2
              }}
            >
              <img
                src={mjpegUrl}
                alt="mjpeg stream"
                style={{ width: "100%", display: "block" }}
              />
            </div>
            <div style={{ marginTop: 10, fontSize: 12, color: COLORS.subtext }}>
              <span style={{ fontFamily: "ui-monospace, Menlo, Consolas, monospace" }}>
                {mjpegUrl}
              </span>
            </div>
          </Card>

          <div style={{ display: "grid", gap: 14 }}>
            <Card title="Atmosphere" subtitle="Humidity • Temp • Pressure" accent={COLORS.green}>
              <KV label="humidity (%)" value={sensors.humidity_percent} />
              <KV label="temp (°C)" value={sensors.temperature_c} />
              <KV label="pressure (hPa)" value={sensors.pressure_hpa} />
            </Card>

            <Card title="Plant Form" subtitle="Camera-derived signals" accent={COLORS.green}>
              <KV label="keeps_form" value={String(vision.keeps_form ?? "-")} />
              <KV label="wilt_score" value={vision.wilt_score ?? "-"} />
              <KV label="lean_angle" value={vision.lean_angle_deg ?? "-"} />
            </Card>
          </div>
        </div>

        {/* Secondary grid */}
        <div
          style={{
            display: "grid",
            gridTemplateColumns: "1fr 1fr 1fr",
            gap: 14,
            marginTop: 14
          }}
        >
          <Card title="Weather" subtitle="Context from backend" accent={COLORS.sky}>
            {weather ? (
              <>
                <KV label="outside temp" value={`${weather?.outside?.temp_c ?? "-"} °C`} />
                <KV label="outside humidity" value={`${weather?.outside?.humidity_percent ?? "-"} %`} />
                <KV label="pressure" value={`${weather?.outside?.pressure_hpa ?? "-"} hPa`} />
                <KV label="sunrise" value={weather?.daylight?.sunrise ?? "-"} mono />
                <KV label="sunset" value={weather?.daylight?.sunset ?? "-"} mono />
                <div style={{ marginTop: 8, fontSize: 12, color: COLORS.subtext }}>
                  cached: <span style={{ fontFamily: "ui-monospace, Menlo, Consolas, monospace" }}>{String(weather.cached)}</span>
                </div>
              </>
            ) : (
              <div style={{ fontSize: 13, color: COLORS.subtext }}>
                Waiting for weather… (allow location)
              </div>
            )}
          </Card>

          <Card title="Solace Decision" subtitle="Reasoning output (no user input)" accent={COLORS.amber}>
            {decision ? (
              <>
                <KV label="summary" value={summary || "-"} />
                <KV label="water rec." value={waterMl != null ? `${waterMl} mL` : "-"} />
                <KV label="servo angle" value={servoDeg != null ? `${Number(servoDeg).toFixed(1)}°` : "-"} />
                <div style={{ marginTop: 8, fontSize: 12, color: COLORS.subtext }}>
                  (posted via <span style={{ fontFamily: "ui-monospace, Menlo, Consolas, monospace" }}>/ingest/decision</span>)
                </div>
              </>
            ) : (
              <div style={{ fontSize: 13, color: COLORS.subtext }}>
                No decision yet — run the pipeline in Solace.
              </div>
            )}
          </Card>

          <Card title="Irrigation Command" subtitle="Backend computes servo mapping" accent={COLORS.green}>
            {lastCmd ? (
              <>
                <KV label="water_ml" value={lastCmd.water_ml} />
                <KV label="servo_angle" value={`${Number(lastCmd.servo_angle_deg).toFixed(1)}°`} />
                <KV label="fill_fraction" value={`${(lastCmd.fill_fraction * 100).toFixed(0)}%`} />
                <KV label="source" value={lastCmd.source} />
              </>
            ) : (
              <div style={{ fontSize: 13, color: COLORS.subtext }}>
                No command yet.
              </div>
            )}
          </Card>
        </div>

        <div style={{ marginTop: 12, fontSize: 12, color: "rgba(255,255,255,0.55)" }}>
          Camera-first layout • rounded minimal cards • vibrant accents • field-friendly contrast
        </div>
      </div>
    </div>
  );
}
