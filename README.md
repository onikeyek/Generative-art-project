# Milo — AI-Powered Smart Desk Companion

Milo is a 3D-printed desk companion built on the ESP32-S3 that senses the
condition of your workspace — temperature, humidity, air quality, motion,
and ambient sound — and reflects it back through an animated pixel-style
face, a live web dashboard, and a wellness score.

---

## What Milo does

- Monitors **temperature, humidity, air quality, motion, and audio** in
  real time using onboard sensors and a custom on-device machine learning
  model
- Reacts through an **animated OLED expression system** — big expressive
  eyes with natural blinking and a subtle "looking around" drift, plus a
  small mouth and accent details that shift between states like `content`,
  `warm`, `stuffy`, `alert`, `stressed`, and `sleepy`
- Classifies ambient sound (keyboard typing, background noise, silence)
  using a **TinyML audio model trained and deployed via Edge Impulse**,
  running entirely on-device
- Streams live readings to a cloud backend and visualizes them on a **web
  dashboard** with atmospheric trend charts, a mood timeline, and a
  computed wellness score

## Architecture

```
ESP32-S3 (firmware)
   │  reads 5 sensors + runs on-device audio classification
   │  HTTPS POST, once per second
   ▼
Flask backend (PythonAnywhere)
   │  validates + stores readings in SQLite
   │  serves REST API
   ▼
Web dashboard (Netlify)
   fetches /api/latest and /api/history, renders live charts + mood face
```

The device communicates with the backend over a simple, stateless HTTPS
POST rather than a persistent connection (MQTT was evaluated and dropped
during development — see [Engineering notes](#engineering-notes) below).
Each reading is a single short-lived request: connect, send, done. No
session state to maintain, no broker to keep alive.

## Tech stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32-S3-WROOM-2 |
| Firmware | Arduino/C++, FreeRTOS |
| On-device ML | Edge Impulse (TensorFlow Lite), 91.8% accuracy, 6ms inference |
| Backend | Python, Flask, SQLite |
| Backend hosting | PythonAnywhere |
| Dashboard | HTML, JavaScript, Chart.js |
| Dashboard hosting | Netlify |

## Hardware

- ESP32-S3-WROOM-2
- DHT22 (temperature + humidity)
- MQ-135 (air quality)
- SPH0645 I2S MEMS microphone
- PIR motion sensor
- DFR0650 0.96" OLED display (128×64, monochrome)

## Repository structure

```
├── milo_firmware.ino       # ESP32-S3 firmware
├── secrets.h.example       # WiFi + backend credentials template (copy to secrets.h)
├── milo_backend.py         # Flask REST API + SQLite storage
├── config.py.example       # Backend API key template (copy to config.py)
├── milo_dashboard.html     # Static web dashboard (deploy as index.html)
├── requirements.txt        # Backend Python dependencies
└── .gitignore
```

## Getting started

### Firmware
1. Copy `secrets.h.example` to `secrets.h` in the same folder as the
   `.ino` file, and fill in your own WiFi credentials, backend URL, and
   API key.
2. Open in Arduino IDE — **the sketch folder name must match the `.ino`
   filename exactly**, or the board won't compile.
3. Install the required libraries (Adafruit_GFX, Adafruit_SSD1306, DHT
   sensor library, plus your exported Edge Impulse model library) and
   flash to the ESP32-S3.

### Backend
1. Copy `config.py.example` to `config.py` and set your own
   `INGEST_API_KEY` (must match the `api_key` in `secrets.h`).
2. `pip install -r requirements.txt`
3. Deploy to a WSGI host (this project runs on PythonAnywhere's free
   tier). Note: if deploying under WSGI, make sure database
   initialization runs at **import time**, not just under
   `if __name__ == "__main__":` — WSGI servers only import the module,
   they never execute it directly.

### Dashboard
1. Update the `API` constant near the top of `milo_dashboard.html` to
   point at your deployed backend URL.
2. Rename to `index.html` before deploying — most static hosts (Netlify
   included) only auto-serve a root file with that exact name.

## Engineering notes

This project originally used MQTT over TLS (HiveMQ Cloud) for
device-to-backend communication, adopted specifically to work around
iOS's strict AP isolation on Personal Hotspots. After that was working,
a separate and much harder problem emerged: persistent MQTT disconnects
that survived ten independent, systematically tested fixes — buffer
sizing, reconnect backoff, CPU core pinning, TLS certificate mode,
duplicate client-ID elimination, and more — none of which resolved it.

Rather than keep chasing an increasingly elusive root cause, the decision
was made to drop MQTT's persistent-connection model entirely in favor of
a stateless HTTPS POST — which matched the actual requirement (report a
few sensor values once a second) far better than a persistent pub/sub
connection ever did, and eliminated the entire class of failures being
fought. Full write-up of the diagnostic process is documented internally.

## Known limitations / roadmap

- Anomaly detection model (planned, not yet implemented — the dashboard
  and wellness score are architecturally ready to receive it)
- Face states currently cover `content`, `warm`, `stuffy`, `alert`,
  `stressed`, and `sleepy`; additional expressive states (`happy`,
  `curious`, `neutral`) are designed but not yet wired into the sensor
  logic

## Credits

Built as part of an internship project.

- **Firmware, machine learning, backend, and dashboard** — Naimot Yekini
- **Hardware enclosure/assembly and creative direction/branding** — Sadunni Edirisinghe
- **Technical documentation, OLED face, and product website** — Jo-Ann Obewe
- **Project Supervisor** - Osman Torunoglu
- **Savonia University of Applied Science**

