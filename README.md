# TwinGrid IoT / EnergyTrack — Resilient Edge-to-Cloud Telemetry Suite

[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](https://www.python.org/)
[![MicroPython](https://img.shields.io/badge/MicroPython-ESP32-green.svg)](https://micropython.org/)
[![Hardware](https://img.shields.io/badge/Hardware-ESP32%20%7C%20PZEM--004T-orange.svg)]()
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)]()

End-to-end **Edge-to-Cloud electrical and thermal telemetry architecture** designed for industrial and agro-industrial energy efficiency monitoring. Built on **ESP32 (MicroPython)**, the system guarantees **zero data loss** and **zero duplicate records** during severe network outages through a persistent binary ring buffer in flash memory (`FlashRingBuffer`), deterministic idempotency keys, and NTP-synchronized timestamps.

---

## 🎯 Engineering Problem & Solution

Industrial environments (such as thermal furnaces, motor rooms, and rural facilities) frequently experience wireless interference, metallic shielding, and intermittent connectivity. Standard IoT nodes drop telemetry packets during disconnections or corrupt time-series alignment when clocks drift.

**TwinGrid Edge Telemetry** solves this at the edge through four core mechanisms:
1. **Deterministic Time Synchronization:** Syncs internal RTC via NTP (`a.st1.ntp.br`) prior to acquisition, ensuring traceable timestamps for multi-sensor fusion and digital twin ingestion.
2. **Fault-Tolerant Flash Persistence (`FlashRingBuffer`):** Implements a low-overhead binary circular queue directly on the ESP32 flash storage to buffer measurements offline when HTTPS/LoRaWAN uplinks fail, automatically draining records once connectivity recovers.
3. **End-to-End Idempotency:** Generates deterministic `idempotency_key` hashes per telemetry frame so cloud ingestion pipelines safely deduplicate retransmissions after network timeouts.
4. **Power & Brownout Protection:** Applies controlled Wi-Fi `txpower` limits and boot safety windows to prevent voltage sags during RF bursts, housed inside a custom 3D-printed ABS enclosure with high-voltage isolation barriers.

---

## 🏗️ System Architecture

```mermaid
flowchart LR
    subgraph Field["Industrial Asset (Edge Node)"]
        S[PZEM-004T v3 & Thermal Sensors] -->|UART / ADC| MCU[ESP32 MicroPython]
        NTP[NTP Server: a.st1.ntp.br] -.->|RTC Sync| MCU
        MCU -->|Write on Outage| FRB[(FlashRingBuffer\nBinary Circular Queue)]
        FRB -->|Drain on Recovery| MCU
    end

    subgraph Cloud["Ingestion & Audit Pipeline"]
        MCU -->|HTTPS POST + Idempotency Key| API[Cloud Ingestion Endpoint]
        MCU -.->|UART Serial Debug| HOST[Linux Audit Suite\ncoletar_ensaio_eap52.py]
        HOST --> PLOTS[Matplotlib Performance & Queue Plots]
    end
