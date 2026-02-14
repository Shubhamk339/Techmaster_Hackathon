
# 💧 Smart IoT Water Grid: Decentralized Autonomous Distribution System

> **A Self-Healing, Peer-to-Peer Water Management Solution** simulatable over 20km distances using ESP32 & MQTT.

---

## 👨‍💻 Developers: Team Techmaster
*   **Shubham Kulkarni** - *Lead Developer & Architect*
*   **Vedant Dadhe** - *System Integration & Logic Design*

---

## 🚀 Project Overview
This project simulates a **Next-Generation Smart Water Grid** where primarily two independent water tanks (Node A and Node B), hypothetically separated by 20km, communicate autonomously over the internet to balance water levels.

Unlike traditional master-slave SCADA systems which are prone to single-point failures, this grid is **fully decentralized**. Each node possesses "Edge Intelligence," making its own decisions based on local sensor data and peer requests. This ensures maximum resilience, efficiency, and scalability.

### 🔗 Live Interactive Simulation (Wokwi)
Run these in separate browser tabs to witness the peer-to-peer communication in real-time:
*   **[Node A Simulation](https://wokwi.com/projects/455952502318698497)**
*   **[Node B Simulation](https://wokwi.com/projects/455952532424370177)**

---

## 🧠 Core Features & Scenarios
We have implemented complex, production-grade logic to handle real-world challenges.

### 1. Autonomous Balancing (The "Need" Scenario)
*   **Problem**: Uneven water distribution leads to shortages in some areas while others have surplus.
*   **Smart Solution**: The system constantly monitors tank levels. If a node drops below **40%**, it automatically broadcasts a *Help Request* via MQTT to the grid.
*   **Result**: The peer node receives this request, evaluates its own reserves, and initiates a transfer *only if* it is safe to do so.

### 2. Intelligent Surplus Sharing (The "50% Rule")
*   **Problem**: Blindly helping a neighbor can drain the donor node, causing a cascading failure across the grid.
*   **Smart Solution**: A cryptographic-like "Proof of Surplus" logic is used. A node will **NEVER** drain itself below 50% to fulfill a request.
*   **Example**: If Node A has 60% water and Node B needs 20%, Node A calculates its surplus (60% - 50% = 10%) and transfers **only 10%**.
*   **Impact**: This guarantees grid stability and prevents the "domino effect" of resource depletion.

### 3. Predictive Self-Healing (Pump Failure & Redundancy)
*   **Problem**: Mechanical pumps often fail due to blockages or electrical faults, stopping water supply critical for life.
*   **Smart Solution**: The system simulates an **Active Health Monitoring** system via a Current Sensor.
*   **Scenario**:
    1.  The system detects an "Underload" condition (Current < 20%), indicating a dry pump or broken shaft.
    2.  The **Main Pump** is instantly shut down to prevent thermal damage.
    3.  The **Backup Redundancy Pump** (Red LED) is automatically activated within milliseconds.
    4.  A critical alert **`! Backup Pump !`** flashes on the LCD for maintenance crews.

### 4. Critical Safety Protocols (Dry Run Protection)
*   **Problem**: Running pumps on an empty tank causes cavitation and permanent hardware destruction.
*   **Smart Solution**: A hard-coded safety interruption layer triggers if the water level drops below **10%**. All electromechanical actuators are forcibly disabled until water levels enter a safe range.

### 5. Operator Override (Manual Control)
*   **Feature**: In emergencies (fire fighting, maintenance), human operators can take full control.
*   **Action**: Turning the designated interface knob overrides the AI model, allowing instant setting of tank levels to test specific edge cases like sudden drainage or heavy rain input.

---

## � Technical Architecture & Protocol

### Communication Stack
*   **Protocol**: MQTT (Message Queuing Telemetry Transport) over TCP/IP.
*   **Broker**: HiveMQ Public Broker (`broker.hivemq.com`).
*   **Topology**: Mesh / Peer-to-Peer (No central server required for logic).

### Data Protocol (JSON)
We use a lightweight, structured JSON payload for all communications to ensure extensibility.

**1. Request Payload:**
```json
{
  "from": "NodeA",
  "to": "NodeB",
  "need": 30.0,
  "level": 35.5
}
```

**2. Response Payload:**
```json
{
  "from": "NodeB",
  "to": "NodeA",
  "accepted": true,
  "amount": 10.0
}
```

## 🛠️ Hardware Specifications (Per Node)
| Component | Function | GPIO Pin |
| :--- | :--- | :--- |
| **ESP32 DevKit V1** | Main Application Processor | - |
| **I2C LCD 16x2** | Real-time Status & Alert Display | SDA(21), SCL(22) |
| **Potentiometer 1** | **Water Level Sensor** (Simulated) / Manual Input | GPIO 34 |
| **Potentiometer 2** | **Current Sensor** (Simulated) for Pump Health | GPIO 35 |
| **Blue LED** | **Main Pump** Indication | GPIO 26 |
| **Red LED** | **Backup/Redundancy Pump** Indication | GPIO 27 |

---

## � Future Scope
1.  **Machine Learning Integration**: Predict water usage patterns to pre-fill tanks before peak hours.
2.  **LoRaWAN Integration**: Extend range from "Internet-based" to "Long Range Radio" for off-grid rural deployment.
3.  **Blockchain Ledger**: Record every liter transferred for transparent, automated billing between neighborhoods.

---

## 📊 Evaluation Guide
**To verify the system's robustness:**
1.  **Open both Simulations** and wait for WiFi connection.
2.  **Force a Transfer**: Lower Node A to 30%. Watch Node B (if >50%) start the Blue Pump.
3.  **Trigger a Fault**: While Node B is pumping, turn its **bottom potentiometer** to zero. Watch the **Red LED** kick in without stopping the flow!
4.  **Test the Limit**: Drain Node B to 50% manually. Watch it *stop supplying* immediately to preserve its own reserve.

---
*Built with ❤️ for **Swalambh Hackathon 2026***
