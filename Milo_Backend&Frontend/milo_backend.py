# ================================================================
# Milo Backend Server v1.5
# HTTP Ingestion (no MQTT) + SQLite Storage + Flask REST API
# ================================================================
#
# Replaces the MQTT/HiveMQ Cloud pipeline entirely. The ESP32 now
# POSTs a single JSON reading once a second to /api/ingest instead
# of holding a persistent MQTT/TLS connection open. This trades true
# pub/sub for a much simpler, much more reliable transport that
# matches the actual use case (periodic short reports, not a
# continuous stream needing multiple subscribers).

import sqlite3
import threading
import os
import time
from datetime import datetime, timedelta
from flask import Flask, jsonify, request
from flask_cors import CORS
from config import INGEST_API_KEY  # kept out of git — see config.py.example

# ── Config ────────────────────────────────────────────────────────
# Absolute path — under PythonAnywhere's WSGI process, the working
# directory isn't guaranteed to be this file's folder the way it is
# when you run `python3 milo_backend.py` locally.
DB_PATH   = os.path.join(os.path.dirname(os.path.abspath(__file__)), "milo.db")
API_PORT  = 5000

# If the device hasn't posted in this long, /api/latest reports it offline.
OFFLINE_AFTER_SECONDS = 10

# ── Flask app ─────────────────────────────────────────────────────
app = Flask(__name__)
CORS(app)

# ── Latest values (in memory) ─────────────────────────────────────
latest = {
    "temp":     "--",
    "humidity": "--",
    "air":      "--",
    "motion":   "--",
    "face":     "--",
    "audio":    "--",
    "timestamp": "--"
}
last_seen_at = None  # datetime of the last successful ingest
lock = threading.Lock()

# ================================================================
# DATABASE SETUP
# ================================================================
def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT,
            temp      REAL,
            humidity  REAL,
            air       INTEGER,
            motion    TEXT,
            face      TEXT,
            audio     TEXT
        )
    ''')
    conn.commit()
    conn.close()
    print("Database ready.")

# Called at import time (not just under `if __name__ == "__main__"`),
# since a WSGI server like PythonAnywhere's imports this module and
# reads `app` directly — it never runs this file as __main__, so the
# table creation has to happen unconditionally here or it never runs.
init_db()

def save_to_db(temp, humidity, air, motion, face, audio):
    try:
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute('''
            INSERT INTO sensor_readings
            (timestamp, temp, humidity, air, motion, face, audio)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        ''', (
            datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            temp, humidity, air, motion, face, audio
        ))
        conn.commit()
        conn.close()
    except Exception as e:
        print(f"DB error: {e}")

# ================================================================
# FLASK REST API
# ================================================================
@app.route("/api/ingest", methods=["POST"])
def ingest():
    global latest, last_seen_at

    # Simple shared-secret check — the ngrok URL is public, so this
    # stops randoms from posting fake readings into your dashboard.
    if request.headers.get("X-API-Key") != INGEST_API_KEY:
        return jsonify({"error": "unauthorized"}), 401

    data = request.get_json(silent=True)
    if data is None:
        return jsonify({"error": "invalid or missing JSON body"}), 400

    required = {"temp", "humidity", "air", "motion", "face", "audio"}
    if not required.issubset(data.keys()):
        missing = required - data.keys()
        return jsonify({"error": f"missing fields: {missing}"}), 400

    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    motion_str = "true" if data["motion"] else "false"

    with lock:
        latest = {
            "temp":      data["temp"],
            "humidity":  data["humidity"],
            "air":       data["air"],
            "motion":    motion_str,
            "face":      data["face"],
            "audio":     data["audio"],
            "timestamp": ts
        }
        last_seen_at = datetime.now()

    save_to_db(
        data["temp"], data["humidity"], data["air"],
        motion_str, data["face"], data["audio"]
    )

    print(f"[{ts}] {data['face']} | {data['temp']}C | "
          f"{data['humidity']}% | air:{data['air']} | audio:{data['audio']}")

    return jsonify({"status": "ok"}), 200

@app.route("/api/latest")
def get_latest():
    with lock:
        resp = dict(latest)
        online = (
            last_seen_at is not None
            and datetime.now() - last_seen_at < timedelta(seconds=OFFLINE_AFTER_SECONDS)
        )
    resp["device_online"] = online
    return jsonify(resp)

@app.route("/api/history")
def get_history():
    try:
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute('''
            SELECT timestamp, temp, humidity, air, motion, face, audio
            FROM sensor_readings
            ORDER BY id DESC
            LIMIT 100
        ''')
        rows = c.fetchall()
        conn.close()
        data = []
        for row in rows:
            data.append({
                "timestamp": row[0],
                "temp":      row[1],
                "humidity":  row[2],
                "air":       row[3],
                "motion":    row[4],
                "face":      row[5],
                "audio":     row[6]
            })
        return jsonify(data)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/face/history")
def get_face_history():
    try:
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute('''
            SELECT face, COUNT(*) as count
            FROM sensor_readings
            GROUP BY face
            ORDER BY count DESC
        ''')
        rows = c.fetchall()
        conn.close()
        data = [{"face": row[0], "count": row[1]} for row in rows]
        return jsonify(data)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/")
def index():
    return jsonify({
        "status": "Milo backend running (HTTP ingestion, v1.5)",
        "endpoints": [
            "POST /api/ingest",
            "GET  /api/latest",
            "GET  /api/history",
            "GET  /api/face/history"
        ]
    })

# ================================================================
# MAIN
# ================================================================
if __name__ == "__main__":
    print(f"API running at http://localhost:{API_PORT}")
    print("Endpoints:")
    print(f"  POST http://localhost:{API_PORT}/api/ingest")
    print(f"  GET  http://localhost:{API_PORT}/api/latest")
    print(f"  GET  http://localhost:{API_PORT}/api/history")
    print(f"  GET  http://localhost:{API_PORT}/api/face/history")

    app.run(host="0.0.0.0", port=API_PORT, debug=False)
