# 🚑 Smart Ambulance Route Finder (DAA Mini Project)

A visual emergency round-trip simulation dashboard powered by **Dijkstra's Shortest Path Algorithm ($\mathcal{O}(V^2)$)**.

---

## 🌟 Key Features

1. **Strict 11-Node City Network**:
   - **Node 0**: Central Hospital `(250, 300)` — **[HOSPITAL - RED]**
   - **Node 10**: Northside Clinic `(700, 150)` — **[HOSPITAL - RED]**
   - **Node 2**: North Junction `(700, 300)` — **[AMBULANCE STATION - GREEN]**
   - **Node 7**: South Industrial `(400, 700)` — **[AMBULANCE STATION - GREEN]**
   - **Node 8**: Riverside Colony `(680, 700)` — [NORMAL]
   - **Node 6**: Tech Park `(840, 480)` — [NORMAL]
   - **Node 1**: City Square `(450, 220)` — [NORMAL]
   - **Node 3**: East Market `(920, 300)` — [NORMAL]
   - **Node 4**: West Highway `(320, 520)` — [NORMAL]
   - **Node 5**: Metro Hub `(580, 420)` — [NORMAL]
   - **Node 9**: Airport Bypass `(940, 660)` — [NORMAL]

2. **Visual Scaling & Clarity**:
   - **Massive 70% Width Canvas**: High-DPI canvas taking 70% of screen width.
   - **Bold Roads & Clear Weights**: Thick road bodies with high-contrast, rounded travel-time capsules.
   - **Scaled Up Ambulance Station Icons**: Large, prominent ambulance station badges when idle at Node 2 and Node 7, scaling dynamically as the vehicle travels.

3. **Accident Alert & Visual Markers**:
   - **Pulsing RED Warning Triangle (!)**: Bright, pulsating warning triangle rendered directly over the chosen accident site.
   - **Top Emergency Banner**: `🚨 EMERGENCY DISPATCHED: Accident at [Node Name]`.

4. **Emergency Round-Trip Simulation**:
   - **Leg 1**: Nearest Ambulance Station (Node 2 or Node 7) $\rightarrow$ Accident Location.
   - **Leg 2**: Accident Location $\rightarrow$ Closest Hospital (dynamically chosen between Central Hospital Node 0 and Northside Clinic Node 10).
   - **Directional Vehicle Rotation**: Vehicle rotates to face the direction of travel along each road edge.
   - **Dynamic Progressive Light-Up Trail**: The path dynamically lights up with an electric neon cyan glow behind the vehicle as it travels.
   - **Live Monospace HUD Timer**: Real-time `MM:SS` timer in the top-right corner.

5. **4-Stage Step Tracker (Sidebar)**:
   1. `Ambulance dispatched to [Accident Node]`
   2. `Patient picked up`
   3. `Heading to [Hospital Name]`
   4. `Arrived at Hospital`

6. **Broken Road Rerouting**:
   - Toggle to simulate road obstruction (with 🎲 Random Road button).
   - Draws broken road in dashed red with `✖ ROAD CLOSED` marker.
   - Dijkstra automatically reroutes around the broken segment.
   - Alert banner: `⚠️ ROAD BROKEN at [Edge]. Rerouting...`

---

## 🚀 Running the Project

Open **`http://localhost:8080`** in any web browser while the server is active.
Double-click `run.bat` for one-click launch anytime!
