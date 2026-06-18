from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS
from datetime import datetime
import os
import httpx

# ── Configuration InfluxDB ───────────────────────────────────
INFLUX_URL    = "http://localhost:8086"
INFLUX_TOKEN  = "-------------------"
INFLUX_ORG    = "energy_org"
INFLUX_BUCKET = "energy_data"

# ── Utilisateurs (simple — pas de base de données) ──────────
USERS = {
    "--------": "---------",
}


TELEGRAM_TOKEN   = "-------------------"     
TELEGRAM_CHAT_ID = "-------------------"  

# Labels lisibles pour chaque alarme
ALARM_LABELS = {
    "alarm_pm"          : "⚡ Surcharge puissance PM5100",
    "alarm_overcurrent" : "⚡ Surintensité (I > 80 A)",
    "alarm_low_pf"      : "⚡ Facteur de puissance faible (FP < 0.8)",
    "alarm_frequency"   : "⚡ Fréquence anormale (< 49 ou > 51 Hz)",
    "alarm_pmp51"       : "💧 Pression eau anormale (PMP51)",
    "alarm_prowirl"     : "🌬️ Débit air anormal (Prowirl 200)",
    "alarm_temp"        : "🌡️ Température hors plage (DS18B20)",
    "alarm_gas_debit"   : "🔥 Débit gaz ZENNER > 5.0 m³/h",
    "alarm_gas_total"   : "📦 Index gaz ZENNER > 2000 m³",
    "alarm_flow"        : "💧 Débit eau anormal (YF-S201)",
    "alarm_comm"        : "📡 Perte communication RS485",
}


_alarm_state: dict = {}

# ── Connexion InfluxDB ───────────────────────────────────────
client    = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = client.write_api(write_options=SYNCHRONOUS)

# ── Application FastAPI ──────────────────────────────────────
app = FastAPI(
    title="SoussCamp — Supervision Énergétique",
    version="4.1",
    docs_url="/api/docs",
)

# ── CORS — autorise le navigateur à appeler l'API ────────────
app.add_middleware(
    CORSMiddleware,
    allow_origins=["https://sousscamp.duckdns.org"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ════════════════════════════════════════════════════════════
#  MODÈLES
# ════════════════════════════════════════════════════════════

class LoginRequest(BaseModel):
    username: str
    password: str

class UnifiedData(BaseModel):
    voltage_v     : float
    current_a     : float
    power_kw      : float
    energy_kwh    : float
    power_factor  : float
    frequency_hz  : float
    cost_dh       : float
    pression_bar  : float
    debit_m3h     : float
    temp_c        : float
    temp_valid    : bool
    gas_index_m3  : float              # Index total gaz ZENNER (m³, 1 décimale)
    gas_flow_m3h  : float              # Débit instantané gaz ZENNER (m³/h, 2 décimales)
    flow_lpm      : float
    volume_l      : float
    alarm_pm          : bool = False
    alarm_overcurrent : bool = False   # Surintensité  (I > 80 A)
    alarm_low_pf      : bool = False   # Facteur de puissance faible (FP < 0.8)
    alarm_frequency   : bool = False   # Fréquence anormale (< 49 Hz ou > 51 Hz)
    alarm_pmp51       : bool = False
    alarm_prowirl     : bool = False
    alarm_temp        : bool = False
    alarm_gas_debit   : bool = False   # Débit gaz > GAS_ALARM_FLOW (5.0 m³/h)
    alarm_gas_total   : bool = False   # Index gaz > GAS_ALARM_INDEX (2000.0 m³)
    alarm_flow        : bool = False   # YF-S201 (débitmètre eau)
    alarm_comm        : bool = False   # communication RS485 (PM5100 + PMP51 + Prowirl + ZENNER tous ensemble)
    device_id         : str  = "UNIFIED_01"

# ════════════════════════════════════════════════════════════
#  TELEGRAM — envoi d'un message
# ════════════════════════════════════════════════════════════

async def telegram_send(message: str):
    url = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage"
    try:
        async with httpx.AsyncClient(timeout=10) as http:
            await http.post(url, json={
                "chat_id"    : TELEGRAM_CHAT_ID,
                "text"       : message,
                "parse_mode" : "HTML",
            })
    except Exception as e:
        print(f"[TELEGRAM ERROR] {e}")


async def check_and_notify_alarms(data):
    
    alarms = {
        "alarm_pm"          : data.alarm_pm,
        "alarm_overcurrent" : data.alarm_overcurrent,
        "alarm_low_pf"      : data.alarm_low_pf,
        "alarm_frequency"   : data.alarm_frequency,
        "alarm_pmp51"       : data.alarm_pmp51,
        "alarm_prowirl"     : data.alarm_prowirl,
        "alarm_temp"        : data.alarm_temp,
        "alarm_gas_debit"   : data.alarm_gas_debit,
        "alarm_gas_total"   : data.alarm_gas_total,
        "alarm_flow"        : data.alarm_flow,
        "alarm_comm"        : data.alarm_comm,
    }
    now_str = datetime.now().strftime("%H:%M:%S")
    for key, active in alarms.items():
        prev = _alarm_state.get(key, False)
        if active and not prev:
        
            label = ALARM_LABELS.get(key, key)
            msg = (
                f"🚨 <b>ALARME DÉCLENCHÉE</b>\n"
                f"📍 {label}\n"
                f"🕐 {now_str}\n"
                f"─────────────────\n"
                f"⚡ P={data.power_kw:.2f} kW  I={data.current_a:.1f} A\n"
                f"💧 Pression={data.pression_bar:.2f} bar\n"
                f"🌡️ Temp={data.temp_c:.1f} °C\n"
                f"🔥 Gaz={data.gas_flow_m3h:.2f} m³/h  Idx={data.gas_index_m3:.1f} m³"
            )
            await telegram_send(msg)
        elif not active and prev:
            # Alarme résolue → notification retour normal
            label = ALARM_LABELS.get(key, key)
            msg = (
                f"✅ <b>ALARME RÉSOLUE</b>\n"
                f"📍 {label}\n"
                f"🕐 {now_str}"
            )
            await telegram_send(msg)
        _alarm_state[key] = active


# ════════════════════════════════════════════════════════════
#  AUTH — POST /auth/login
# ════════════════════════════════════════════════════════════

@app.post("/auth/login")
async def login(req: LoginRequest):
    pwd = USERS.get(req.username)
    if not pwd or pwd != req.password:
        raise HTTPException(status_code=401, detail="Identifiants incorrects")
    # Rôle selon utilisateur
    role = "admin"
    return {
        "status":   "ok",
        "username": req.username,
        "role":     role,
        "token":    f"demo-token-{req.username}",   # token simple pour démo
    }

# ════════════════════════════════════════════════════════════
#  POST /unified_data  ← ESP32
# ════════════════════════════════════════════════════════════

@app.post("/unified_data")
async def receive_unified_data(data: UnifiedData):
    try:
        now = datetime.utcnow()
        p_energy = (
            Point("energy_meter").tag("device", data.device_id)
            .field("voltage_v",    data.voltage_v)
            .field("current_a",    data.current_a)
            .field("power_kw",     data.power_kw)
            .field("energy_kwh",   data.energy_kwh)
            .field("power_factor", data.power_factor)
            .field("frequency_hz", data.frequency_hz)
            .field("cost_dh",      data.cost_dh)
            .time(now, WritePrecision.NS)
        )
        p_process = (
            Point("process").tag("device", data.device_id)
            .field("pression_bar", data.pression_bar)
            .field("debit_m3h",    data.debit_m3h)
            .time(now, WritePrecision.NS)
        )
        p_env = Point("environment").tag("device", data.device_id) \
            .field("gas_index_m3", data.gas_index_m3) \
            .field("gas_flow_m3h", data.gas_flow_m3h) \
            .field("flow_lpm", data.flow_lpm) \
            .field("volume_l", data.volume_l) \
            .time(now, WritePrecision.NS)
        if data.temp_valid:
            p_env = p_env.field("temp_c", data.temp_c)

        p_alarms = (
            Point("alarms").tag("device", data.device_id)
            .field("alarm_pm",          int(data.alarm_pm))
            .field("alarm_overcurrent", int(data.alarm_overcurrent))
            .field("alarm_low_pf",      int(data.alarm_low_pf))
            .field("alarm_frequency",   int(data.alarm_frequency))
            .field("alarm_pmp51",       int(data.alarm_pmp51))
            .field("alarm_prowirl",     int(data.alarm_prowirl))
            .field("alarm_temp",        int(data.alarm_temp))
            .field("alarm_gas_debit",   int(data.alarm_gas_debit))
            .field("alarm_gas_total",   int(data.alarm_gas_total))
            .field("alarm_flow",        int(data.alarm_flow))
            .field("alarm_comm",        int(data.alarm_comm))
            .field("any_alarm",         int(any([data.alarm_pm, data.alarm_overcurrent,
                data.alarm_low_pf, data.alarm_frequency, data.alarm_pmp51,
                data.alarm_prowirl, data.alarm_temp, data.alarm_gas_debit,
                data.alarm_gas_total, data.alarm_flow, data.alarm_comm])))
            .time(now, WritePrecision.NS)
        )
        write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG,
                        record=[p_energy, p_process, p_env, p_alarms])
        # Vérification alarmes → notification Telegram si changement d'état
        await check_and_notify_alarms(data)
        print(f"[OK] {data.device_id} | P={data.power_kw:.2f}kW T={data.temp_c:.1f}°C")
        return {"status": "ok", "timestamp": now.isoformat()}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# ════════════════════════════════════════════════════════════
#  GET /latest  ← Dashboard temps réel
# ════════════════════════════════════════════════════════════

@app.get("/latest")
async def get_latest():
    try:
        query_api = client.query_api()
        result = {}
        for measurement in ["energy_meter", "process", "environment", "alarms"]:
            
            q = f'''
                from(bucket: "{INFLUX_BUCKET}")
                  |> range(start: -45s)
                  |> filter(fn: (r) => r._measurement == "{measurement}")
                  |> last()
            '''
            data = {}
            try:
                for table in query_api.query(q, org=INFLUX_ORG):
                    for record in table.records:
                        data[record.get_field()] = record.get_value()
            except Exception as e_inner:
                print(f"[WARN] Measurement {measurement} query failed: {e_inner}")
            result[measurement] = data
        return {"status": "ok", "data": result}
    except Exception as e:
        import traceback
        print(f"[ERROR] /latest: {traceback.format_exc()}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/debug")
async def debug():
    """Endpoint de diagnostic pour tester la connexion InfluxDB"""
    import traceback
    results = {}
    # Test ping
    try:
        ok = client.ping()
        results["ping"] = "ok" if ok else "failed"
    except Exception as e:
        results["ping"] = f"ERROR: {e}"
    # Test simple query
    try:
        query_api = client.query_api()
        q = f'''
            from(bucket: "{INFLUX_BUCKET}")
              |> range(start: -1h)
              |> limit(n: 1)
        '''
        tables = query_api.query(q, org=INFLUX_ORG)
        count = sum(len(t.records) for t in tables)
        results["query_test"] = f"ok - {count} records found in last 1h"
    except Exception as e:
        results["query_test"] = f"ERROR: {traceback.format_exc()}"
    # Test with wider range
    try:
        query_api = client.query_api()
        q2 = f'''
            from(bucket: "{INFLUX_BUCKET}")
              |> range(start: -24h)
              |> filter(fn: (r) => r._measurement == "energy_meter")
              |> limit(n: 3)
        '''
        tables2 = query_api.query(q2, org=INFLUX_ORG)
        pts = []
        for t in tables2:
            for r in t.records:
                pts.append({"field": r.get_field(), "value": r.get_value(), "time": str(r.get_time())})
        results["energy_last_24h"] = pts if pts else "no data"
    except Exception as e:
        results["energy_last_24h"] = f"ERROR: {str(e)}"
    return {"status": "ok", "influx_url": INFLUX_URL, "bucket": INFLUX_BUCKET, "org": INFLUX_ORG, "results": results}

# ════════════════════════════════════════════════════════════
#  GET /history?field=power_kw&minutes=60
#  Historique d'un champ sur N minutes
# ════════════════════════════════════════════════════════════

@app.get("/history")
async def get_history(field: str = "power_kw", minutes: int = 60, start: str = None, stop: str = None):
    field_map = {
        "power_kw":"energy_meter","voltage_v":"energy_meter",
        "current_a":"energy_meter","energy_kwh":"energy_meter",
        "power_factor":"energy_meter","frequency_hz":"energy_meter",
        "cost_dh":"energy_meter",
        "pression_bar":"process","debit_m3h":"process",
        "temp_c":"environment","gas_index_m3":"environment",
        "gas_flow_m3h":"environment","flow_lpm":"environment","volume_l":"environment",
        "any_alarm":"alarms",
    }
    measurement = field_map.get(field, "energy_meter")
    try:
        query_api = client.query_api()

        # Plage absolue : timestamps ISO entre guillemets dans Flux
        if start and stop:
            range_expr = f'start: time(v: "{start}"), stop: time(v: "{stop}")'
            try:
                dt_start = datetime.fromisoformat(start.replace("Z", "+00:00"))
                dt_stop  = datetime.fromisoformat(stop.replace("Z",  "+00:00"))
                duration_mins = int((dt_stop - dt_start).total_seconds() / 60)
            except Exception:
                duration_mins = minutes
        else:
            range_expr = f'start: -{minutes}m'
            duration_mins = minutes

        # Adapter la fenetre d'agregation selon la duree
        if duration_mins <= 30:
            window = "30s"
        elif duration_mins <= 120:
            window = "1m"
        elif duration_mins <= 360:
            window = "3m"
        elif duration_mins <= 1440:
            window = "15m"
        elif duration_mins <= 10080:
            window = "1h"
        else:
            window = "6h"

        q = f'''
            from(bucket: "{INFLUX_BUCKET}")
              |> range({range_expr})
              |> filter(fn: (r) => r._measurement == "{measurement}")
              |> filter(fn: (r) => r._field == "{field}")
              |> aggregateWindow(every: {window}, fn: mean, createEmpty: false)
        '''
        points = []
        for table in query_api.query(q, org=INFLUX_ORG):
            for record in table.records:
                val = record.get_value()
                points.append({
                    "time": record.get_time().strftime("%Y-%m-%d %H:%M:%S"),
                    "value": round(val, 3) if val is not None else 0,
                })
        return {"status": "ok", "field": field, "data": points}
    except Exception as e:
        import traceback
        err = traceback.format_exc()
        print(f"[ERROR] /history: {err}")
        raise HTTPException(status_code=500, detail=f"{str(e)} | TRACE: {err[:300]}")

# ════════════════════════════════════════════════════════════
#  GET /health
# ════════════════════════════════════════════════════════════

@app.get("/health")
async def health():
    try:
        client.ping()
        return {"status": "ok", "influxdb": "connected"}
    except Exception as e:
        return {"status": "error", "detail": str(e)}

# ════════════════════════════════════════════════════════════
#  Servir le frontend React depuis /static
# ════════════════════════════════════════════════════════════

static_dir = os.path.join(os.path.dirname(__file__), "static")
if os.path.exists(static_dir):
    app.mount("/static", StaticFiles(directory=static_dir), name="static")

@app.get("/")
async def serve_frontend():
    index = os.path.join(static_dir, "index.html")
    if os.path.exists(index):
        return FileResponse(index)
    return {"message": "Placez index.html dans le dossier static/"}
