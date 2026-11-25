# 📑 DAA Project Report: Smart Ambulance Finder

**Course:** Design and Analysis of Algorithms (DAA)  
**Project Title:** Smart Ambulance Finder & Dynamic Emergency Dispatch System  
**Implementation Language:** Pure C (Core Engine) + HTML5/CSS3/Vanilla JS (Visual Dashboard)

---

## 1. Introduction & Motivation

In acute medical emergencies (e.g., myocardial infarction, traumatic brain injury, stroke), every elapsed minute drastically affects patient survival probability—a medical concept known as the **"Golden Hour"**. Conventional emergency dispatch systems face two critical challenges:
1. **Dynamic Congestion and Blockades:** Urban roads frequently suffer from construction, waterlogging, fallen debris, or traffic accidents. Traditional dispatchers often direct ambulances toward blocked or impassable locations.
2. **First-Come-First-Serve Allocation Inefficiency:** Assigning the first available ambulance without optimizing for spatial network topology and travel time leads to suboptimal response times.

**Smart Ambulance Finder** solves these problems using formal graph algorithms:
- Computes the minimum travel-time route using **Dijkstra’s Algorithm** with an adjacency matrix ($\mathcal{O}(V^2)$).
- Reconstructs the exact transit route using a **Parent Pointer array** ($\mathcal{O}(V)$).
- Implements a novel **Rendezvous Point Discovery Algorithm** ($\mathcal{O}(V)$) when a patient's junction is completely blocked or vehicle-inaccessible, guiding both the patient and ambulance to the optimal safe meeting point.

---

## 2. System Architecture

The project adheres to a clean 5-layer decoupled architecture:

```
+--------------------------------------------------------------+
|             PRESENTATION LAYER (HTML5 + CSS + JS)            |
| - Stat Cards: Fleet availability, active dispatches, requests |
| - Dynamic Graph Canvas: Live node/edge weights, route pulses |
| - Emergency Form: Patient junction selector & access toggle  |
| - Fleet Status Table: Unit availability & trip completion    |
+--------------------------------------------------------------+
                               |
               Polling state.json (every 2s)
               POST /api/command (or file bridge)
                               v
+--------------------------------------------------------------+
|            FILE BRIDGE & SERIALIZATION LAYER (C)             |
| - state.json: Handcrafted C output using fprintf only        |
| - command.json: Processed on arrival, atomic remove()        |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|              APPLICATION LAYER (DISPATCHER & REQ MGR)         |
| - Validates coordinates and coordinates multi-unit fleet     |
| - State Machine: IDLE -> DISPATCHED -> RENDEZVOUS -> COMPLETE|
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|               CORE ALGORITHM LAYER (DAA MARKS)               |
| - Dijkstra's Single Source Shortest Path: O(V^2)             |
| - Path Reconstruction via Parent Backtracking: O(V)          |
| - Rendezvous Point Scan: O(V) neighborhood scan              |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                         DATA LAYER                            |
| - graph[V][V]: Adjacency matrix of edge travel times (mins)  |
| - isAccessible[V]: Binary accessibility vector (1/0)         |
| - ambulances[3]: Fleet structures with current junctions     |
+--------------------------------------------------------------+
```

---

## 3. Algorithm Specifications & Theoretical Analysis

### 3.1 Dijkstra's Algorithm ($\mathcal{O}(V^2)$ Matrix Formulation)

#### Why Adjacency Matrix $\mathcal{O}(V^2)$ over Min-Heap $\mathcal{O}((V + E)\log V)$?
1. **Dense Urban Topography:** In city arterial road networks, $E \approx V^2 / 2$. When the graph is moderately dense, $(V + E)\log V \approx V^2 \log V$, which has a higher constant factor than the simple, cache-friendly array scan of the $\mathcal{O}(V^2)$ matrix implementation.
2. **Zero External Overhead:** Eliminates dynamic pointer-heavy priority queue data structures, optimizing cache locality in embedded systems.

#### Formal Mathematical Formulation
Let $G = (V, E)$ be a directed/undirected graph with weight function $w: E \to \mathbb{R}^+$.
Let $S$ be the set of vertices whose shortest distances from source $s$ have been finalized.

1. **Initialization:**
   $$\forall v \in V: \text{dist}[v] \leftarrow \infty, \quad \text{visited}[v] \leftarrow \text{false}, \quad \text{parent}[v] \leftarrow -1$$
   $$\text{dist}[s] \leftarrow 0$$

2. **Greedy Selection:**
   In each iteration $i \in \{0, \dots, V-1\}$:
   $$u = \arg\min_{v \in V \setminus S} \text{dist}[v]$$
   $$S \leftarrow S \cup \{u\}, \quad \text{visited}[u] \leftarrow \text{true}$$

3. **Pruned Edge Relaxation:**
   For each adjacent vertex $v$ of $u$:
   $$\text{If } \text{isAccessible}[v] = 1 \text{ and } \text{dist}[u] + w(u, v) < \text{dist}[v]:$$
   $$\text{dist}[v] \leftarrow \text{dist}[u] + w(u, v)$$
   $$\text{parent}[v] \leftarrow u$$

#### Complexity Analysis:
- **Inner Loop 1 (Vertex Selection):** Scans $V$ elements $\implies \mathcal{O}(V)$. Executed $V$ times $\implies \mathcal{O}(V^2)$.
- **Inner Loop 2 (Edge Relaxation):** Inspects row `graph[u][*]` of length $V \implies \mathcal{O}(V)$. Executed $V$ times $\implies \mathcal{O}(V^2)$.
- **Total Time Complexity:** $\mathcal{O}(V^2)$.
- **Auxiliary Space Complexity:** $\mathcal{O}(V)$ for `dist[]`, `visited[]`, and `parent[]`.

---

### 3.2 Path Reconstruction Algorithm ($\mathcal{O}(V)$)

To provide turn-by-turn navigation for the ambulance driver, the path must be reconstructed from the `parent[]` array:

```c
int reconstructPath(int src, int dest, const int parent[], int outPath[]) {
    if (src == dest) { outPath[0] = src; return 1; }
    if (parent[dest] == -1) return 0; // Unreachable

    int temp[MAX_V];
    int count = 0, curr = dest;
    while (curr != -1) {
        temp[count++] = curr;
        if (curr == src) break;
        curr = parent[curr];
    }
    for (int i = 0; i < count; i++) {
        outPath[i] = temp[count - 1 - i]; // Reverse sequence
    }
    return count;
}
```
- **Time Complexity:** $\mathcal{O}(V)$ (at most $V$ predecessor hops).
- **Space Complexity:** $\mathcal{O}(V)$.

---

### 3.3 Rendezvous Point Algorithm ($\mathcal{O}(V)$ Neighborhood Scan)

#### The Problem:
If a patient is at junction $P$ where $\text{isAccessible}[P] = 0$ (e.g. road submerged in floodwater, fallen electric pole), an ambulance cannot drive into junction $P$.

#### The Algorithmic Solution:
1. Scan all immediate neighbors $j \in \text{Adj}(P)$ where $\text{isAccessible}[j] = 1$:
   $$R = \arg\min_{j \in \text{Adj}(P), \text{isAccessible}[j]=1} w(P, j)$$
   This scan checks $V$ elements in $\mathcal{O}(V)$ time.
2. If all direct neighbors are also blocked, execute an evacuation distance search from $P$ to find the nearest accessible junction across the connected component.
3. The chosen junction $R$ becomes the **Rendezvous Point**.
4. The Dispatcher routes the ambulance to $R$ and instructs the patient:
   *"Proceed to [Junction R]. Ambulance ETA: X mins. Walking time: Y mins."*

---

## 4. Complexity Summary

| Operation | Best Case | Average Case | Worst Case | Space Complexity |
|---|---|---|---|---|
| **Dijkstra Vertex Scan** | $\mathcal{O}(V^2)$ | $\mathcal{O}(V^2)$ | $\mathcal{O}(V^2)$ | $\mathcal{O}(V)$ |
| **Path Reversal** | $\mathcal{O}(1)$ | $\mathcal{O}(V)$ | $\mathcal{O}(V)$ | $\mathcal{O}(V)$ |
| **Rendezvous Search** | $\mathcal{O}(1)$ | $\mathcal{O}(V)$ | $\mathcal{O}(V)$ | $\mathcal{O}(1)$ |
| **Fleet Evaluation ($K=3$)** | $\mathcal{O}(K \cdot V^2)$ | $\mathcal{O}(K \cdot V^2)$ | $\mathcal{O}(K \cdot V^2)$ | $\mathcal{O}(V)$ |

---

## 5. Sample Execution Trace (Viva Demonstration)

### Scenario A: Normal Emergency Request
- **Input:** Patient at Junction 3 (*East Market*), Road is Accessible (`isAccessible = 1`).
- **Fleet State:**
  - `AMB-ALPHA`: Stationed at Junction 0 (*Central Hospital*)
  - `AMB-BRAVO`: Stationed at Junction 2 (*North Junction*)
  - `AMB-CHARLIE`: Stationed at Junction 7 (*South Industrial*)
- **Computation:**
  - Dijkstra from Junction 0 to 3: Distance = 20 mins (`0 -> 1 -> 2 -> 3`)
  - Dijkstra from Junction 2 to 3: Distance = 9 mins (`2 -> 3`)
  - Dijkstra from Junction 7 to 3: Distance = 23 mins (`7 -> 8 -> 9 -> 6 -> 3`)
- **Outcome:** `AMB-BRAVO` dispatched with ETA 9 mins. Route: `[2, 3]`.

### Scenario B: Blocked Road & Rendezvous Activation
- **Input:** Patient at Junction 3 (*East Market*), Road is Blocked (`isAccessible = 0`).
- **Computation:**
  - Rendezvous Engine detects Junction 3 is blocked.
  - Direct accessible neighbors of 3: Junction 2 (dist 9) and Junction 6 (*Tech Park*, dist 8).
  - Minimum safe distance: Junction 6 with walking time 8 minutes.
  - Rendezvous Point set to Junction 6 (*Tech Park*).
  - Closest available ambulance dispatched to Junction 6.
- **Outcome:** Ambulance arrives at Tech Park. Patient notified to walk 8 minutes to meet ambulance.

---

## 6. Viva Voce Q&A Cheat Sheet

**Q1: Why did you choose Dijkstra instead of Bellman-Ford or Floyd-Warshall?**  
> *Answer:* Road transit times between city junctions are strictly non-negative ($w(e) > 0$). Bellman-Ford ($\mathcal{O}(V \cdot E)$) is designed for graphs with negative weights, which do not exist in physical travel distances. Floyd-Warshall ($\mathcal{O}(V^3)$) solves all-pairs shortest paths, which is unnecessary when we only need single-source shortest path from candidate ambulance stations to the emergency node.

**Q2: What is the Rendezvous algorithm and why is it innovative for DAA?**  
> *Answer:* Standard Dijkstra assumes the destination is always reachable. In real-world urban disasters (floods, landslides), the target node itself is inaccessible. The Rendezvous algorithm models constrained graph optimization: it computes a dual-agent meeting point by minimizing the patient's foot transit distance $\mathcal{O}(V)$ and chaining it into the vehicle's Dijkstra shortest path.

**Q3: How does your File Bridge operate without heavy frameworks?**  
> *Answer:* To maintain extreme performance and zero dependencies, the C application serializes the graph and fleet state into `state.json` using pure `fprintf()`. The frontend dashboard polls `state.json` via asynchronous `fetch()`. The C binary also features a built-in Winsock socket server to accept dispatches directly via HTTP POST.
