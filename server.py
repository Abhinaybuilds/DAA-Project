"""
Smart Ambulance Route Finder - Python Backend
----------------------------------------------
Emergency Round-Trip Simulation:
  Leg 1: Nearest Ambulance (Node 2 or 7) -> Accident Location
  Leg 2: Accident Location -> Nearest Hospital (Node 0 or Node 10)
  Broken Road Rerouting via Dijkstra O(V^2)
"""

import json
import os
import sys
from http.server import SimpleHTTPRequestHandler, HTTPServer

PORT = 8080
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# 1. STRICT 11-NODE GRAPH (2 HOSPITALS, 2 AMBULANCE STATIONS, 7 JUNCTIONS)
NODES = [
    {"id": 0, "name": "Central Hospital", "x": 250, "y": 300, "type": "hospital"},
    {"id": 1, "name": "City Square", "x": 450, "y": 220, "type": "junction"},
    {"id": 2, "name": "North Junction", "x": 700, "y": 300, "type": "ambulance_station"},
    {"id": 3, "name": "East Market", "x": 920, "y": 300, "type": "junction"},
    {"id": 4, "name": "West Highway", "x": 320, "y": 520, "type": "junction"},
    {"id": 5, "name": "Metro Hub", "x": 580, "y": 420, "type": "junction"},
    {"id": 6, "name": "Tech Park", "x": 840, "y": 480, "type": "junction"},
    {"id": 7, "name": "South Industrial", "x": 400, "y": 700, "type": "ambulance_station"},
    {"id": 8, "name": "Riverside Colony", "x": 680, "y": 700, "type": "junction"},
    {"id": 9, "name": "Airport Bypass", "x": 940, "y": 660, "type": "junction"},
    {"id": 10, "name": "Northside Clinic", "x": 700, "y": 150, "type": "hospital"}
]

HOSPITAL_NODES = [0, 10]

EDGES = [
    {"u": 0, "v": 1, "w": 6},
    {"u": 1, "v": 2, "w": 5},
    {"u": 0, "v": 4, "w": 8},
    {"u": 2, "v": 5, "w": 6},
    {"u": 2, "v": 3, "w": 9},
    {"u": 1, "v": 5, "w": 7},
    {"u": 3, "v": 6, "w": 8},
    {"u": 4, "v": 7, "w": 7},
    {"u": 4, "v": 5, "w": 9},
    {"u": 5, "v": 7, "w": 10},
    {"u": 5, "v": 6, "w": 5},
    {"u": 5, "v": 8, "w": 8},
    {"u": 6, "v": 9, "w": 6},
    {"u": 8, "v": 9, "w": 7},
    {"u": 7, "v": 8, "w": 6},
    {"u": 2, "v": 10, "w": 4},
    {"u": 1, "v": 10, "w": 6},
    {"u": 3, "v": 10, "w": 7}
]

# Ambulances
ambulances = [
    {"id": 1, "name": "Ambulance 1", "stationNode": 2, "currentNode": 2, "status": "Available"},
    {"id": 2, "name": "Ambulance 2", "stationNode": 7, "currentNode": 7, "status": "Available"}
]

# 2. DIJKSTRA ENGINE
def dijkstra(start_node, broken_edge=None):
    num_nodes = len(NODES)
    dist = {i: float('inf') for i in range(num_nodes)}
    parent = {i: None for i in range(num_nodes)}
    visited = set()

    adj = {i: [] for i in range(num_nodes)}
    for e in EDGES:
        u, v, w = e["u"], e["v"], e["w"]
        if broken_edge:
            bu, bv = broken_edge[0], broken_edge[1]
            if (u == bu and v == bv) or (u == bv and v == bu):
                continue
        adj[u].append((v, w))
        adj[v].append((u, w))

    dist[start_node] = 0

    for _ in range(num_nodes):
        u = -1
        min_d = float('inf')
        for i in range(num_nodes):
            if i not in visited and dist[i] < min_d:
                min_d = dist[i]
                u = i

        if u == -1 or dist[u] == float('inf'):
            break

        visited.add(u)

        for v, weight in adj[u]:
            if v not in visited:
                new_d = dist[u] + weight
                if new_d < dist[v]:
                    dist[v] = new_d
                    parent[v] = u

    return dist, parent

def reconstruct_path(parent, target):
    path = []
    curr = target
    while curr is not None:
        path.append(curr)
        curr = parent[curr]
    path.reverse()
    return path

def run_roundtrip_dispatch(accident_node, broken_edge=None):
    if accident_node < 0 or accident_node >= len(NODES):
        return {"error": "Invalid accident node specified."}

    # Step A: Find nearest ambulance to accident
    best_amb = None
    best_leg1_eta = float('inf')
    best_leg1_path = []

    for amb in ambulances:
        dist, parent = dijkstra(amb["currentNode"], broken_edge=broken_edge)
        if dist[accident_node] < best_leg1_eta:
            best_leg1_eta = dist[accident_node]
            best_amb = amb
            best_leg1_path = reconstruct_path(parent, accident_node)

    if best_amb is None or best_leg1_eta == float('inf'):
        return {"error": "No accessible route found to the accident location."}

    # Step B: Find nearest hospital from accident
    dist_acc, parent_acc = dijkstra(accident_node, broken_edge=broken_edge)
    best_hosp = None
    best_leg2_eta = float('inf')
    best_leg2_path = []

    for h in HOSPITAL_NODES:
        if dist_acc[h] < best_leg2_eta:
            best_leg2_eta = dist_acc[h]
            best_hosp = h
            best_leg2_path = reconstruct_path(parent_acc, h)

    if best_hosp is None or best_leg2_eta == float('inf'):
        return {"error": "No accessible route found from accident to any hospital."}

    full_path = best_leg1_path + best_leg2_path[1:]
    total_eta = best_leg1_eta + best_leg2_eta

    acc_name = NODES[accident_node]["name"]
    hosp_name = NODES[best_hosp]["name"]
    amb_name = best_amb["name"]

    is_broken = False
    alert_msg = ""
    if broken_edge:
        is_broken = True
        u_name = NODES[broken_edge[0]]["name"]
        v_name = NODES[broken_edge[1]]["name"]
        alert_msg = f"⚠️ ROAD BROKEN at [{u_name} ↔ {v_name}]. Rerouting..."

    emergency_banner = f"🚨 EMERGENCY DISPATCHED: Accident at {acc_name}"

    return {
        "status": "success",
        "ambulance": {
            "id": best_amb["id"],
            "name": best_amb["name"],
            "stationNode": best_amb["stationNode"],
            "startNode": best_leg1_path[0]
        },
        "accidentNode": accident_node,
        "accidentName": acc_name,
        "hospitalNode": best_hosp,
        "hospitalName": hosp_name,
        "brokenEdge": broken_edge,
        "isBrokenRoadActive": is_broken,
        "alertMessage": alert_msg,
        "emergencyBanner": emergency_banner,
        "totalEtaMinutes": total_eta,
        "leg1": {
            "from": best_leg1_path[0],
            "to": accident_node,
            "eta": best_leg1_eta,
            "path": best_leg1_path
        },
        "leg2": {
            "from": accident_node,
            "to": best_hosp,
            "eta": best_leg2_eta,
            "path": best_leg2_path
        },
        "fullPath": full_path
    }

def reset_simulation():
    ambulances[0]["currentNode"] = 2
    ambulances[0]["status"] = "Available"
    ambulances[1]["currentNode"] = 7
    ambulances[1]["status"] = "Available"
    return {
        "status": "success",
        "message": "All ambulances reset to starting stations.",
        "ambulances": ambulances
    }

# 3. HTTP HANDLER
class AmbulanceRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=BASE_DIR, **kwargs)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path == "/api/nodes":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            data = {
                "nodes": NODES,
                "edges": EDGES,
                "hospitals": HOSPITAL_NODES,
                "ambulances": ambulances
            }
            self.wfile.write(json.dumps(data).encode("utf-8"))
        else:
            super().do_GET()

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8") if content_length > 0 else "{}"
        try:
            payload = json.loads(body)
        except Exception:
            payload = {}

        if self.path == "/api/dispatch":
            accident_node = int(payload.get("accidentNode", 6))
            is_broken = bool(payload.get("isBrokenRoad", False))
            broken_edge = None
            if is_broken:
                b_u = payload.get("brokenEdgeU")
                b_v = payload.get("brokenEdgeV")
                if b_u is not None and b_v is not None:
                    broken_edge = (int(b_u), int(b_v))

            result = run_roundtrip_dispatch(accident_node, broken_edge=broken_edge)
            self.send_response(200 if "error" not in result else 400)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode("utf-8"))

        elif self.path == "/api/reset":
            result = reset_simulation()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode("utf-8"))

        else:
            self.send_response(404)
            self.end_headers()

def run(port=PORT):
    server = HTTPServer(("0.0.0.0", port), AmbulanceRequestHandler)
    print(f"===============================================================")
    print(f" SMART AMBULANCE ROUTE FINDER - BACKEND ACTIVE (Python)")
    print(f" Listening on http://localhost:{port}")
    print(f"===============================================================")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()

if __name__ == "__main__":
    p = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    run(p)
