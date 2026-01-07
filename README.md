# FUSION SMART - Solar Monitor AI ☀️

A comprehensive solar panel monitoring system that leverages IoT and AI to track performance metrics like Voltage, Temperature, and Humidity in real-time.

## 🚀 Overview

This project consists of three main components:
1.  **IoT Node (ESP32):** Collects sensor data (Voltage, Temp, Humidity) and sends it to the Cloud (ThingSpeak).
2.  **Data Sync Engine (Python):** Fetches cloud data, synchronizes it with a local SQLite database, and exports it to CSV for analysis.
3.  **Web Dashboard:** A modern, responsive web interface to visualize real-time data, historical trends, and AI-driven insights.

---

## 📂 Project Structure

```
├── Esp32_Solar/
│   └── Esp32_Solar.ino       # Arduino C++ code for ESP32
├── Main web/
│   ├── index.html            # Login/Landing page
│   ├── dashboard.html        # Main monitoring dashboard
│   ├── analysis.html         # Data analysis view
│   ├── ai_trends.html        # AI predictive insights
│   ├── charts.js             # Charting logic
│   └── index.css             # Global styling
└── database/
    ├── sync_db.py            # Python script for data synchronization
    ├── solar_details.db      # SQLite database (generated)
    └── solar_data.csv        # CSV export (generated)
```

---

## 🛠️ Hardware Requirements

- **ESP32 Development Board**
- **DHT11 Sensor** (Temperature & Humidity)
- **Voltage Sensor Module** (0-25V)
- **Solar Panel** (connected to load/battery)

### Pin Configuration
| Sensor | ESP32 Pin |
|---|---|
| DHT11 Data | `GPIO 4` |
| Voltage Sensor (Signal) | `GPIO 34` |

---

## ⚙️ Setup & Installation

### 1. Firmware (ESP32)
1.  Open `Esp32_Solar/Esp32_Solar.ino` in Arduino IDE.
2.  Install required libraries:
    -   `DHT sensor library` by Adafruit
    -   `ThingSpeak` by MathWorks
3.  Configure your credentials in the code:
    ```cpp
    const char* ssid = "YOUR_WIFI_SSID";
    const char* password = "YOUR_WIFI_PASSWORD";
    unsigned long channelID = YOUR_THINGSPEAK_CHANNEL_ID;
    const char* writeAPIKey = "YOUR_WRITE_API_KEY";
    ```
4.  Upload to your ESP32 board.

### 2. Database Synchronization
To pull the latest data from ThingSpeak to your local environment:
1.  Ensure you have **Python 3.x** installed.
2.  Install dependencies:
    ```bash
    pip install requests
    ```
3.  Run the sync script:
    ```bash
    python database/sync_db.py
    ```
    *This will create/update `solar_details.db` and `solar_data.csv`.*

### 3. Web Dashboard
1.  Typically, you can simply open `Main web/index.html` in any modern web browser.
2.  **Login Credentials (Default):**
    -   **Email:** `admin@fusion.io`
    -   **Password:** `admin123`

---

## 📊 Features

-   **Real-time Monitoring:** View live voltage, temperature, and humidity.
-   **Cloud Integration:** Seamless data logging to ThingSpeak.
-   **Offline Storage:** Auto-syncs cloud data to local SQLite/CSV.
-   **AI Insights:** "Powered by Gemini AI Predictive Engine" for trend analysis.
-   **Responsive Design:** Beautiful dark-mode interface with glassmorphism effects.

---

## 📝 License

This project is open-source and free to use for educational and personal projects.
