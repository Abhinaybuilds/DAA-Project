/**
 * Smart Ambulance Route Finder - Node.js Backend
 * ----------------------------------------------
 * Emergency Round-Trip Simulation:
 *   Leg 1: Nearest Ambulance (Node 2 or 7) -> Accident Location
 *   Leg 2: Accident Location -> Nearest Hospital (Node 0 or Node 10)
 *   Broken Road Rerouting via Dijkstra O(V^2)
 */

const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = 8080;
const BASE_DIR = __dirname;

// 1. STRICT 11-NODE GRAPH (2 HOSPITALS, 2 AMBULANCE STATIONS, 7 JUNCTIONS)
const NODES = [
  { id: 0, name: "Central Hospital", x: 250, y: 300, type: "hospital" },
  { id: 1, name: "City Square", x: 450, y: 220, type: "junction" },
  { id: 2, name: "North Junction", x: 700, y: 300, type: "ambulance_station" },
  { id: 3, name: "East Market", x: 920, y: 300, type: "junction" },
  { id: 4, name: "West Highway", x: 320, y: 520, type: "junction" },
  { id: 5, name: "Metro Hub", x: 580, y: 420, type: "junction" },
  { id: 6, name: "Tech Park", x: 840, y: 480, type: "junction" },
  { id: 7, name: "South Industrial", x: 400, y: 700, type: "ambulance_station" },
  { id: 8, name: "Riverside Colony", x: 680, y: 700, type: "junction" },
  { id: 9, name: "Airport Bypass", x: 940, y: 660, type: "junction" },
  { id: 10, name: "Northside Clinic", x: 700, y: 150, type: "hospital" }
];

const HOSPITAL_NODES = [0, 10];

const EDGES = [
  { u: 0, v: 1, w: 6 },
  { u: 1, v: 2, w: 5 },
  { u: 0, v: 4, w: 8 },
  { u: 2, v: 5, w: 6 },
  { u: 2, v: 3, w: 9 },
  { u: 1, v: 5, w: 7 },
  { u: 3, v: 6, w: 8 },
  { u: 4, v: 7, w: 7 },
  { u: 4, v: 5, w: 9 },
  { u: 5, v: 7, w: 10 },
  { u: 5, v: 6, w: 5 },
  { u: 5, v: 8, w: 8 },
  { u: 6, v: 9, w: 6 },
  { u: 8, v: 9, w: 7 },
  { u: 7, v: 8, w: 6 },
  { u: 2, v: 10, w: 4 },
  { u: 1, v: 10, w: 6 },
  { u: 3, v: 10, w: 7 }
];

const ambulances = [
  { id: 1, name: "Ambulance 1", stationNode: 2, currentNode: 2, status: "Available" },
  { id: 2, name: "Ambulance 2", stationNode: 7, currentNode: 7, status: "Available" }
];

// 2. DIJKSTRA ENGINE
function dijkstra(startNode, brokenEdge = null) {
  const n = NODES.length;
  const dist = Array(n).fill(Infinity);
  const parent = Array(n).fill(null);
  const visited = new Set();

  const adj = Array.from({ length: n }, () => []);
  EDGES.forEach(e => {
    if (brokenEdge) {
      const bu = brokenEdge[0], bv = brokenEdge[1];
      if ((e.u === bu && e.v === bv) || (e.u === bv && e.v === bu)) return;
    }
    adj[e.u].push({ v: e.v, w: e.w });
    adj[e.v].push({ v: e.u, w: e.w });
  });

  dist[startNode] = 0;

  for (let count = 0; count < n; count++) {
    let u = -1, minD = Infinity;
    for (let i = 0; i < n; i++) {
      if (!visited.has(i) && dist[i] < minD) {
        minD = dist[i];
        u = i;
      }
    }

    if (u === -1 || dist[u] === Infinity) break;
    visited.add(u);

    for (const edge of adj[u]) {
      const v = edge.v;
      if (!visited.has(v)) {
        const newD = dist[u] + edge.w;
        if (newD < dist[v]) {
          dist[v] = newD;
          parent[v] = u;
        }
      }
    }
  }

  return { dist, parent };
}

function reconstructPath(parent, target) {
  const pathArr = [];
  let curr = target;
  while (curr !== null) {
    pathArr.push(curr);
    curr = parent[curr];
  }
  pathArr.reverse();
  return pathArr;
}

function runRoundTripDispatch(accidentNode, brokenEdge = null) {
  if (accidentNode < 0 || accidentNode >= NODES.length) {
    return { error: "Invalid accident node specified." };
  }

  // Step A: Nearest Ambulance to Accident
  let bestAmb = null;
  let bestLeg1Eta = Infinity;
  let bestLeg1Path = [];

  for (const amb of ambulances) {
    const { dist, parent } = dijkstra(amb.currentNode, brokenEdge);
    if (dist[accidentNode] < bestLeg1Eta) {
      bestLeg1Eta = dist[accidentNode];
      bestAmb = amb;
      bestLeg1Path = reconstructPath(parent, accidentNode);
    }
  }

  if (!bestAmb || bestLeg1Eta === Infinity) {
    return { error: "No accessible route found to the accident location." };
  }

  // Step B: Accident to Nearest Hospital
  const { dist: distAcc, parent: parentAcc } = dijkstra(accidentNode, brokenEdge);
  let bestHosp = null;
  let bestLeg2Eta = Infinity;
  let bestLeg2Path = [];

  for (const h of HOSPITAL_NODES) {
    if (distAcc[h] < bestLeg2Eta) {
      bestLeg2Eta = distAcc[h];
      bestHosp = h;
      bestLeg2Path = reconstructPath(parentAcc, h);
    }
  }

  if (bestHosp === null || bestLeg2Eta === Infinity) {
    return { error: "No accessible route found from accident to any hospital." };
  }

  const fullPath = [...bestLeg1Path, ...bestLeg2Path.slice(1)];
  const totalEta = bestLeg1Eta + bestLeg2Eta;

  const accName = NODES[accidentNode].name;
  const hospName = NODES[bestHosp].name;

  let isBroken = false;
  let alertMsg = "";
  if (brokenEdge) {
    isBroken = true;
    const uName = NODES[brokenEdge[0]].name;
    const vName = NODES[brokenEdge[1]].name;
    alertMsg = `⚠️ ROAD BROKEN at [${uName} ↔ ${vName}]. Rerouting...`;
  }

  const emergencyBanner = `🚨 EMERGENCY DISPATCHED: Accident at ${accName}`;

  return {
    status: "success",
    ambulance: {
      id: bestAmb.id,
      name: bestAmb.name,
      stationNode: bestAmb.stationNode,
      startNode: bestLeg1Path[0]
    },
    accidentNode,
    accidentName: accName,
    hospitalNode: bestHosp,
    hospitalName: hospName,
    brokenEdge,
    isBrokenRoadActive: isBroken,
    alertMessage: alertMsg,
    emergencyBanner,
    totalEtaMinutes: totalEta,
    leg1: {
      from: bestLeg1Path[0],
      to: accidentNode,
      eta: bestLeg1Eta,
      path: bestLeg1Path
    },
    leg2: {
      from: accidentNode,
      to: bestHosp,
      eta: bestLeg2Eta,
      path: bestLeg2Path
    },
    fullPath
  };
}

function resetSimulation() {
  ambulances[0].currentNode = 2;
  ambulances[0].status = "Available";
  ambulances[1].currentNode = 7;
  ambulances[1].status = "Available";
  return {
    status: "success",
    message: "All ambulances reset to starting stations.",
    ambulances
  };
}

// 3. HTTP SERVER
const MIME_TYPES = {
  ".html": "text/html",
  ".css": "text/css",
  ".js": "application/javascript",
  ".json": "application/json"
};

const server = http.createServer((req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");

  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return;
  }

  const parsedUrl = new URL(req.url, `http://${req.headers.host}`);
  const pathname = parsedUrl.pathname;

  if (req.method === "GET") {
    if (pathname === "/api/nodes") {
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ nodes: NODES, edges: EDGES, hospitals: HOSPITAL_NODES, ambulances }));
      return;
    }

    let filePath = path.join(BASE_DIR, pathname === "/" ? "index.html" : pathname);
    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || "text/plain";

    fs.readFile(filePath, (err, content) => {
      if (err) {
        res.writeHead(404, { "Content-Type": "text/plain" });
        res.end("404 Not Found");
      } else {
        res.writeHead(200, { "Content-Type": contentType });
        res.end(content);
      }
    });
    return;
  }

  if (req.method === "POST") {
    let body = "";
    req.on("data", chunk => { body += chunk; });
    req.on("end", () => {
      let payload = {};
      try { payload = JSON.parse(body || "{}"); } catch (e) { payload = {}; }

      if (pathname === "/api/dispatch") {
        const accidentNode = parseInt(payload.accidentNode, 10) || 6;
        const isBroken = Boolean(payload.isBrokenRoad);
        let brokenEdge = null;
        if (isBroken && payload.brokenEdgeU !== undefined && payload.brokenEdgeV !== undefined) {
          brokenEdge = [parseInt(payload.brokenEdgeU, 10), parseInt(payload.brokenEdgeV, 10)];
        }

        const result = runRoundTripDispatch(accidentNode, brokenEdge);
        res.writeHead(result.error ? 400 : 200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(result));
      } else if (pathname === "/api/reset") {
        const result = resetSimulation();
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(result));
      } else {
        res.writeHead(404, { "Content-Type": "text/plain" });
        res.end("Not Found");
      }
    });
    return;
  }

  res.writeHead(405);
  res.end();
});

server.listen(PORT, () => {
  console.log("===============================================================");
  console.log(" SMART AMBULANCE ROUTE FINDER - BACKEND ACTIVE (Node.js)");
  console.log(` Listening on http://localhost:${PORT}`);
  console.log("===============================================================");
});
