/**
 * Smart Ambulance Route Finder - Frontend Controller & Visualizer
 * ---------------------------------------------------------------
 * Full-stack vanilla JS implementation.
 * - 11 Strict Nodes (2 Hospitals, 2 Ambulance Stations, 7 Junctions)
 * - Emergency Round-Trip Simulation:
 *     Leg 1: Station -> Accident
 *     Leg 2: Accident -> Nearest Hospital (Node 0 or 10)
 * - Direction-oriented rotating vehicle animation
 * - Progressive glowing path trail
 * - Pulsing Red Warning Triangle on Accident Node
 * - Live HUD Timer (MM:SS) & 4-Step Tracker
 * - Broken Road Detection & Rerouting
 */

// 1. EXACT GRAPH LAYOUT (11 NODES: 2 HOSPITALS, 2 AMBULANCE STATIONS, 7 JUNCTIONS)
const CITY_NODES = [
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

const CITY_EDGES = [
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

const ADJ_MATRIX = Array.from({ length: 11 }, () => Array(11).fill(Infinity));
for (let i = 0; i < 11; i++) ADJ_MATRIX[i][i] = 0;

CITY_EDGES.forEach(e => {
  ADJ_MATRIX[e.u][e.v] = e.w;
  ADJ_MATRIX[e.v][e.u] = e.w;
});

// Ambulances
let ambulances = [
  { id: 1, name: "Ambulance 1", stationNode: 2, currentNode: 2, status: "Available" },
  { id: 2, name: "Ambulance 2", stationNode: 7, currentNode: 7, status: "Available" }
];

// App State
let activeAccidentNode = 6; // Default: Tech Park
let isBrokenRoadActive = false;
let brokenEdgeU = 5; // Default: Metro Hub <-> Tech Park
let brokenEdgeV = 6;
let activeDispatchRecord = null;
let isEmergencyActive = false;

// Animation & Live Timer state
let isAnimating = false;
let animationLeg = 1; // 1 = Leg1 (Station -> Accident), 2 = Scene pause, 3 = Leg2 (Accident -> Hospital)
let currentLegPath = [];
let animEdgeIndex = 0;
let animSegmentProgress = 0;
let animAmbulanceId = null;
let animAmbulancePos = { x: 0, y: 0 };
let animAmbulanceAngle = 0;
let timerIntervalId = null;
let timerSeconds = 0;
let pauseTimeoutId = null;

// Progressive Traveled Path History for Dynamic Light-Up Trail
let traveledPoints = [];

// Canvas setup
let canvas, ctx;
let hoveredNodeId = -1;

// 2. INITIALIZATION
window.addEventListener("DOMContentLoaded", () => {
  canvas = document.getElementById("cityGraphCanvas");
  ctx = canvas.getContext("2d");

  initDropdowns();
  renderAmbulanceList();
  setupEventListeners();

  drawGraph();
  requestAnimationFrame(renderLoop);
});

function initDropdowns() {
  const accSelect = document.getElementById("accidentNodeSelect");
  accSelect.innerHTML = "";

  CITY_NODES.forEach(node => {
    if (node.type === "hospital") return;
    const opt = document.createElement("option");
    opt.value = node.id;
    let label = `[Node ${node.id}] ${node.name}`;
    if (node.id === 2 || node.id === 7) label += " (Ambulance Station)";
    opt.textContent = label;
    accSelect.appendChild(opt);
  });
  accSelect.value = activeAccidentNode;

  populateBrokenRoadDropdown();
}

function populateBrokenRoadDropdown() {
  const edgeSelect = document.getElementById("brokenEdgeSelect");
  edgeSelect.innerHTML = "";

  CITY_EDGES.forEach(e => {
    const opt = document.createElement("option");
    opt.value = `${e.u}-${e.v}`;
    const uName = CITY_NODES[e.u].name;
    const vName = CITY_NODES[e.v].name;
    opt.textContent = `${uName} ↔ ${vName} (${e.w}m)`;
    if ((e.u === brokenEdgeU && e.v === brokenEdgeV) || (e.u === brokenEdgeV && e.v === brokenEdgeU)) {
      opt.selected = true;
    }
    edgeSelect.appendChild(opt);
  });
}

function pickRandomBrokenRoad() {
  const randomEdge = CITY_EDGES[Math.floor(Math.random() * CITY_EDGES.length)];
  brokenEdgeU = randomEdge.u;
  brokenEdgeV = randomEdge.v;
  populateBrokenRoadDropdown();
  drawGraph();
}

function renderAmbulanceList() {
  const container = document.getElementById("ambulanceList");
  container.innerHTML = "";

  ambulances.forEach(amb => {
    const item = document.createElement("div");
    item.className = "ambulance-item";

    const locName = CITY_NODES[amb.currentNode] ? CITY_NODES[amb.currentNode].name : `Node ${amb.currentNode}`;
    let badgeClass = "available";
    if (amb.status === "Dispatched") badgeClass = "dispatched";
    if (amb.status === "Arrived") badgeClass = "arrived";

    item.innerHTML = `
      <div class="amb-info">
        <span class="amb-icon">🚑</span>
        <div>
          <div class="amb-name">${amb.name}</div>
          <div class="amb-loc">Station: <strong>${locName}</strong></div>
        </div>
      </div>
      <span class="badge-amb ${badgeClass}">${amb.status}</span>
    `;
    container.appendChild(item);
  });
}

function setupEventListeners() {
  const accSelect = document.getElementById("accidentNodeSelect");
  accSelect.addEventListener("change", e => {
    activeAccidentNode = parseInt(e.target.value, 10);
    drawGraph();
  });

  const brokenCheckbox = document.getElementById("brokenRoadCheckbox");
  brokenCheckbox.addEventListener("change", e => {
    isBrokenRoadActive = e.target.checked;
    const group = document.getElementById("brokenRoadDropdownGroup");
    const randBtn = document.getElementById("btnRandomEdge");
    group.style.display = isBrokenRoadActive ? "flex" : "none";
    randBtn.style.display = isBrokenRoadActive ? "inline-flex" : "none";

    const alertBanner = document.getElementById("roadAlertBanner");
    if (!isBrokenRoadActive) {
      alertBanner.style.display = "none";
    }
    drawGraph();
  });

  document.getElementById("btnRandomEdge").addEventListener("click", pickRandomBrokenRoad);

  const edgeSelect = document.getElementById("brokenEdgeSelect");
  edgeSelect.addEventListener("change", e => {
    const parts = e.target.value.split("-");
    brokenEdgeU = parseInt(parts[0], 10);
    brokenEdgeV = parseInt(parts[1], 10);
    drawGraph();
  });

  document.getElementById("btnDismissAlert").addEventListener("click", () => {
    document.getElementById("roadAlertBanner").style.display = "none";
  });

  document.getElementById("btnDispatch").addEventListener("click", handleDispatch);
  document.getElementById("btnQuickReset").addEventListener("click", handleReset);

  // Canvas interaction
  canvas.addEventListener("mousemove", handleCanvasMouseMove);
  canvas.addEventListener("click", handleCanvasClick);
  canvas.addEventListener("mouseleave", () => {
    hoveredNodeId = -1;
    drawGraph();
  });
}

// 3. DISPATCH LOGIC (ROUND TRIP)
async function handleDispatch() {
  if (isAnimating) {
    alert("An emergency mission is currently underway. Please wait for patient delivery or click Reset Simulation.");
    return;
  }

  const payload = {
    accidentNode: activeAccidentNode,
    isBrokenRoad: isBrokenRoadActive,
    brokenEdgeU: brokenEdgeU,
    brokenEdgeV: brokenEdgeV
  };

  let result = null;

  try {
    const res = await fetch("/api/dispatch", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    if (res.ok) {
      result = await res.json();
    }
  } catch (e) {}

  if (!result || result.error) {
    result = runClientRoundTripDispatch(payload.accidentNode, isBrokenRoadActive ? [brokenEdgeU, brokenEdgeV] : null);
  }

  if (result.error) {
    alert(result.error);
    return;
  }

  activeDispatchRecord = result;
  isEmergencyActive = true;

  // Show Emergency Banner at top
  const emergBanner = document.getElementById("emergencyBanner");
  const emergText = document.getElementById("emergencyBannerText");
  emergText.textContent = `🚨 EMERGENCY DISPATCHED: Accident at ${result.accidentName}`;
  emergBanner.style.display = "flex";

  // Show Broken Road Alert Banner if active
  const alertBanner = document.getElementById("roadAlertBanner");
  const alertText = document.getElementById("alertBannerText");
  if (result.isBrokenRoadActive) {
    const uName = CITY_NODES[brokenEdgeU].name;
    const vName = CITY_NODES[brokenEdgeV].name;
    alertText.textContent = `⚠️ ROAD BROKEN at [${uName} ↔ ${vName}]. Rerouting...`;
    alertBanner.style.display = "flex";
  } else {
    alertBanner.style.display = "none";
  }

  startRoundTripSimulation(result);
}

function handleReset() {
  stopSimulation();

  try { fetch("/api/reset", { method: "POST" }); } catch (e) {}

  ambulances[0].currentNode = 2;
  ambulances[0].status = "Available";
  ambulances[1].currentNode = 7;
  ambulances[1].status = "Available";

  activeDispatchRecord = null;
  isEmergencyActive = false;
  currentLegPath = [];
  traveledPoints = [];
  timerSeconds = 0;
  updateTimerDisplay(0);

  document.getElementById("hudEta").textContent = "-- mins";
  const badge = document.getElementById("hudStatusBadge");
  badge.className = "hud-badge idle";
  badge.textContent = "STANDBY";

  document.getElementById("emergencyBanner").style.display = "none";
  document.getElementById("roadAlertBanner").style.display = "none";

  resetStepTracker();
  renderAmbulanceList();
  drawGraph();
}

function resetStepTracker() {
  for (let i = 1; i <= 4; i++) {
    const el = document.getElementById(`trackerStep${i}`);
    el.className = "step-item";
  }
  document.getElementById("step1Title").textContent = "Ambulance dispatched to Accident";
  document.getElementById("step1Status").textContent = "Waiting for dispatch";
  document.getElementById("step2Status").textContent = "Securing at scene";
  document.getElementById("step3Title").textContent = "Heading to Hospital";
  document.getElementById("step3Status").textContent = "Transporting to closest facility";
  document.getElementById("step4Status").textContent = "Patient delivered safely";
}

// 4. CLIENT-SIDE DIJKSTRA ROUND-TRIP FALLBACK
function runClientDijkstra(startNode, brokenEdge = null) {
  const n = CITY_NODES.length;
  const dist = Array(n).fill(Infinity);
  const parent = Array(n).fill(null);
  const visited = new Set();

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

    for (const edge of CITY_EDGES) {
      let v = -1, w = edge.w;
      if (edge.u === u) v = edge.v;
      else if (edge.v === u) v = edge.u;
      else continue;

      if (brokenEdge) {
        const bu = brokenEdge[0], bv = brokenEdge[1];
        if ((edge.u === bu && edge.v === bv) || (edge.u === bv && edge.v === bu)) {
          continue;
        }
      }

      if (!visited.has(v)) {
        const newD = dist[u] + w;
        if (newD < dist[v]) {
          dist[v] = newD;
          parent[v] = u;
        }
      }
    }
  }

  return { dist, parent };
}

function reconstructClientPath(parent, target) {
  const pathArr = [];
  let curr = target;
  while (curr !== null) {
    pathArr.push(curr);
    curr = parent[curr];
  }
  pathArr.reverse();
  return pathArr;
}

function runClientRoundTripDispatch(accidentNode, brokenEdge = null) {
  // Leg 1: Ambulance to Accident
  let bestAmb = null;
  let bestLeg1Eta = Infinity;
  let bestLeg1Path = [];

  for (const amb of ambulances) {
    const { dist, parent } = runClientDijkstra(amb.currentNode, brokenEdge);
    if (dist[accidentNode] < bestLeg1Eta) {
      bestLeg1Eta = dist[accidentNode];
      bestAmb = amb;
      bestLeg1Path = reconstructClientPath(parent, accidentNode);
    }
  }

  if (!bestAmb || bestLeg1Eta === Infinity) {
    return { error: "No accessible route found to the accident location." };
  }

  // Leg 2: Accident to Nearest Hospital (0 or 10)
  const { dist: distFromAcc, parent: parentFromAcc } = runClientDijkstra(accidentNode, brokenEdge);
  let bestHospital = null;
  let bestLeg2Eta = Infinity;
  let bestLeg2Path = [];

  for (const hosp of HOSPITAL_NODES) {
    if (distFromAcc[hosp] < bestLeg2Eta) {
      bestLeg2Eta = distFromAcc[hosp];
      bestHospital = hosp;
      bestLeg2Path = reconstructClientPath(parentFromAcc, hosp);
    }
  }

  if (bestHospital === null || bestLeg2Eta === Infinity) {
    return { error: "No accessible route found from accident to any hospital." };
  }

  const fullPath = [...bestLeg1Path, ...bestLeg2Path.slice(1)];
  const totalEta = bestLeg1Eta + bestLeg2Eta;

  return {
    status: "success",
    ambulance: {
      id: bestAmb.id,
      name: bestAmb.name,
      stationNode: bestAmb.stationNode,
      startNode: bestLeg1Path[0]
    },
    accidentNode,
    accidentName: CITY_NODES[accidentNode].name,
    hospitalNode: bestHospital,
    hospitalName: CITY_NODES[bestHospital].name,
    brokenEdge,
    isBrokenRoadActive: brokenEdge !== null,
    totalEtaMinutes: totalEta,
    leg1: { from: bestLeg1Path[0], to: accidentNode, eta: bestLeg1Eta, path: bestLeg1Path },
    leg2: { from: accidentNode, to: bestHospital, eta: bestLeg2Eta, path: bestLeg2Path },
    fullPath
  };
}

// 5. ANIMATION & LIVE STEP TRACKER CONTROLLER
function startRoundTripSimulation(record) {
  stopSimulation();

  animAmbulanceId = record.ambulance.id;
  const amb = ambulances.find(a => a.id === animAmbulanceId);
  if (amb) amb.status = "Dispatched";
  renderAmbulanceList();

  document.getElementById("hudEta").textContent = `${record.totalEtaMinutes} mins`;

  // Start live timer
  timerSeconds = 0;
  updateTimerDisplay(0);
  timerIntervalId = setInterval(() => {
    timerSeconds++;
    updateTimerDisplay(timerSeconds);
  }, 1000);

  // Phase 1: Leg 1 (Station -> Accident)
  animationLeg = 1;
  currentLegPath = [...record.leg1.path];
  animEdgeIndex = 0;
  animSegmentProgress = 0;

  const startNode = CITY_NODES[currentLegPath[0]];
  animAmbulancePos = { x: startNode.x, y: startNode.y };
  traveledPoints = [{ x: startNode.x, y: startNode.y }];

  // Calculate initial angle
  if (currentLegPath.length > 1) {
    const nextNode = CITY_NODES[currentLegPath[1]];
    animAmbulanceAngle = Math.atan2(nextNode.y - startNode.y, nextNode.x - startNode.x);
  }

  // Step Tracker Step 1 Active
  setTrackerStep(1, `Ambulance dispatched to ${record.accidentName}`, `ETA: ${record.leg1.eta} min`);

  const badge = document.getElementById("hudStatusBadge");
  badge.className = "hud-badge moving";
  badge.textContent = "HEADING TO ACCIDENT";

  isAnimating = true;
}

function stopSimulation() {
  isAnimating = false;
  if (timerIntervalId) {
    clearInterval(timerIntervalId);
    timerIntervalId = null;
  }
  if (pauseTimeoutId) {
    clearTimeout(pauseTimeoutId);
    pauseTimeoutId = null;
  }
}

function setTrackerStep(stepNum, titleText, statusText) {
  for (let i = 1; i <= 4; i++) {
    const el = document.getElementById(`trackerStep${i}`);
    if (i < stepNum) el.className = "step-item done";
    else if (i === stepNum) el.className = "step-item active";
    else el.className = "step-item";
  }

  if (stepNum === 1) {
    if (titleText) document.getElementById("step1Title").textContent = titleText;
    if (statusText) document.getElementById("step1Status").textContent = statusText;
  } else if (stepNum === 2) {
    if (statusText) document.getElementById("step2Status").textContent = statusText;
  } else if (stepNum === 3) {
    if (titleText) document.getElementById("step3Title").textContent = titleText;
    if (statusText) document.getElementById("step3Status").textContent = statusText;
  } else if (stepNum === 4) {
    if (statusText) document.getElementById("step4Status").textContent = statusText;
  }
}

function onReachedAccident() {
  isAnimating = false;
  animationLeg = 2; // Pause at scene

  setTrackerStep(2, null, "Securing patient at scene");
  const badge = document.getElementById("hudStatusBadge");
  badge.className = "hud-badge idle";
  badge.textContent = "PATIENT PICKED UP";

  pauseTimeoutId = setTimeout(() => {
    startLeg2();
  }, 1800);
}

function startLeg2() {
  animationLeg = 3; // Leg 2: Transport to Hospital
  currentLegPath = [...activeDispatchRecord.leg2.path];
  animEdgeIndex = 0;
  animSegmentProgress = 0;

  if (currentLegPath.length > 1) {
    const currNode = CITY_NODES[currentLegPath[0]];
    const nextNode = CITY_NODES[currentLegPath[1]];
    animAmbulanceAngle = Math.atan2(nextNode.y - currNode.y, nextNode.x - currNode.x);
  }

  setTrackerStep(3, `Heading to ${activeDispatchRecord.hospitalName}`, `ETA: ${activeDispatchRecord.leg2.eta} min`);
  const badge = document.getElementById("hudStatusBadge");
  badge.className = "hud-badge moving";
  badge.textContent = "HEADING TO HOSPITAL";

  isAnimating = true;
}

function onDeliveredToHospital() {
  stopSimulation();
  animationLeg = 4;

  const hospNodeId = activeDispatchRecord.hospitalNode;
  const amb = ambulances.find(a => a.id === animAmbulanceId);
  if (amb) {
    amb.status = "Arrived";
    amb.currentNode = hospNodeId;
  }
  renderAmbulanceList();

  setTrackerStep(4, null, `Delivered to ${activeDispatchRecord.hospitalName}!`);
  const badge = document.getElementById("hudStatusBadge");
  badge.className = "hud-badge arrived";
  badge.textContent = "ARRIVED AT HOSPITAL";

  drawGraph();
}

function updateTimerDisplay(secs) {
  const m = Math.floor(secs / 60).toString().padStart(2, "0");
  const s = (secs % 60).toString().padStart(2, "0");
  document.getElementById("hudTimer").textContent = `${m}:${s}`;
}

// 6. CANVAS ANIMATION & GRAPH RENDER LOOP
function renderLoop(now) {
  if (isAnimating && currentLegPath.length > 1) {
    updateAnimationStep();
  }

  drawGraph();
  requestAnimationFrame(renderLoop);
}

function updateAnimationStep() {
  if (animEdgeIndex >= currentLegPath.length - 1) {
    if (animationLeg === 1) onReachedAccident();
    else if (animationLeg === 3) onDeliveredToHospital();
    return;
  }

  const uId = currentLegPath[animEdgeIndex];
  const vId = currentLegPath[animEdgeIndex + 1];
  const uNode = CITY_NODES[uId];
  const vNode = CITY_NODES[vId];

  // Calculate travel direction
  const dx = vNode.x - uNode.x;
  const dy = vNode.y - uNode.y;
  animAmbulanceAngle = Math.atan2(dy, dx);

  const edgeWeight = ADJ_MATRIX[uId][vId] || 6;
  const stepSpeed = 1 / (edgeWeight * 32);

  animSegmentProgress += stepSpeed;

  if (animSegmentProgress >= 1.0) {
    animSegmentProgress = 0;
    animEdgeIndex++;
    animAmbulancePos = { x: vNode.x, y: vNode.y };
    traveledPoints.push({ x: vNode.x, y: vNode.y });

    if (animEdgeIndex >= currentLegPath.length - 1) {
      if (animationLeg === 1) onReachedAccident();
      else if (animationLeg === 3) onDeliveredToHospital();
      return;
    }
  } else {
    animAmbulancePos.x = uNode.x + dx * animSegmentProgress;
    animAmbulancePos.y = uNode.y + dy * animSegmentProgress;
    traveledPoints.push({ x: animAmbulancePos.x, y: animAmbulancePos.y });
  }
}

function drawGraph() {
  if (!ctx) return;

  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  drawBackgroundGrid(w, h);

  // 1. Draw Roads
  drawEdges();

  // 2. Draw Full Calculated Path (Faint guide line)
  if (activeDispatchRecord && activeDispatchRecord.fullPath && activeDispatchRecord.fullPath.length > 1) {
    drawPlannedRoute(activeDispatchRecord.fullPath);
  }

  // 3. Draw Traveled Path Lighting Up (Glowing Neon Cyan Trail)
  if (traveledPoints.length > 1) {
    drawTraveledGlowTrail();
  }

  // 4. Draw Nodes
  drawNodes();

  // 5. Draw Bright Pulsing RED Triangle over Accident Node
  if (isEmergencyActive) {
    drawPulsingAccidentTriangle(activeAccidentNode);
  }

  // 6. Draw Moving or Dispatched Ambulance Icon
  if (isAnimating || animationLeg === 2 || animationLeg === 4) {
    drawMovingAmbulance(animAmbulancePos.x, animAmbulancePos.y, animAmbulanceAngle);
  }
}

function drawBackgroundGrid(w, h) {
  ctx.strokeStyle = "rgba(255, 255, 255, 0.025)";
  ctx.lineWidth = 1;
  const step = 40;
  for (let x = 0; x < w; x += step) {
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
  }
  for (let y = 0; y < h; y += step) {
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
  }
}

function drawEdges() {
  CITY_EDGES.forEach(e => {
    const u = CITY_NODES[e.u];
    const v = CITY_NODES[e.v];

    const isBroken = isBrokenRoadActive && (
      (e.u === brokenEdgeU && e.v === brokenEdgeV) ||
      (e.u === brokenEdgeV && e.v === brokenEdgeU)
    );

    ctx.save();

    if (isBroken) {
      // Red dashed broken road
      ctx.strokeStyle = "#ef4444";
      ctx.lineWidth = 6;
      ctx.setLineDash([12, 8]);
      ctx.shadowColor = "rgba(239, 68, 68, 0.85)";
      ctx.shadowBlur = 14;

      ctx.beginPath();
      ctx.moveTo(u.x, u.y);
      ctx.lineTo(v.x, v.y);
      ctx.stroke();

      const midX = (u.x + v.x) / 2;
      const midY = (u.y + v.y) / 2;
      drawBrokenRoadMarker(midX, midY);

    } else {
      // Solid thick road
      ctx.strokeStyle = "#273854";
      ctx.lineWidth = 7;
      ctx.lineCap = "round";
      ctx.beginPath();
      ctx.moveTo(u.x, u.y);
      ctx.lineTo(v.x, v.y);
      ctx.stroke();

      ctx.strokeStyle = "#172338";
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(u.x, u.y);
      ctx.lineTo(v.x, v.y);
      ctx.stroke();

      const midX = (u.x + v.x) / 2;
      const midY = (u.y + v.y) / 2;
      drawWeightBadge(midX, midY, `${e.w}m`);
    }

    ctx.restore();
  });
}

function drawBrokenRoadMarker(x, y) {
  ctx.save();
  ctx.font = "bold 12px 'JetBrains Mono', monospace";
  const text = "✖ ROAD CLOSED";
  const metrics = ctx.measureText(text);
  const rw = metrics.width + 16;
  const rh = 22;

  ctx.fillStyle = "#991b1b";
  ctx.strokeStyle = "#ef4444";
  ctx.lineWidth = 1.5;
  roundRect(ctx, x - rw / 2, y - rh / 2, rw, rh, 5, true, true);

  ctx.fillStyle = "#ffffff";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, x, y);
  ctx.restore();
}

function drawWeightBadge(x, y, text) {
  ctx.save();
  ctx.font = "800 12px 'JetBrains Mono', monospace";
  const metrics = ctx.measureText(text);
  const rw = metrics.width + 16;
  const rh = 20;

  ctx.fillStyle = "#0c1322";
  ctx.strokeStyle = "#334769";
  ctx.lineWidth = 1.5;
  roundRect(ctx, x - rw / 2, y - rh / 2, rw, rh, 10, true, true);

  ctx.fillStyle = "#cbd5e1";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, x, y);
  ctx.restore();
}

// Faint guide line for planned route
function drawPlannedRoute(path) {
  ctx.save();
  ctx.strokeStyle = "rgba(6, 182, 212, 0.25)";
  ctx.lineWidth = 6;
  ctx.setLineDash([8, 6]);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  ctx.beginPath();
  for (let i = 0; i < path.length; i++) {
    const node = CITY_NODES[path[i]];
    if (i === 0) ctx.moveTo(node.x, node.y);
    else ctx.lineTo(node.x, node.y);
  }
  ctx.stroke();
  ctx.restore();
}

// Progressive Glowing Trail lighting up behind ambulance
function drawTraveledGlowTrail() {
  if (traveledPoints.length < 2) return;

  ctx.save();
  ctx.strokeStyle = "#06b6d4";
  ctx.lineWidth = 8;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.shadowColor = "rgba(6, 182, 212, 0.85)";
  ctx.shadowBlur = 18;

  ctx.beginPath();
  ctx.moveTo(traveledPoints[0].x, traveledPoints[0].y);
  for (let i = 1; i < traveledPoints.length; i++) {
    ctx.lineTo(traveledPoints[i].x, traveledPoints[i].y);
  }
  ctx.stroke();
  ctx.restore();
}

// Pulsing RED Triangle with Exclamation Mark over Accident Site
function drawPulsingAccidentTriangle(nodeId) {
  const node = CITY_NODES[nodeId];
  if (!node) return;

  const t = performance.now() * 0.007;
  const pulse = 1 + 0.18 * Math.sin(t);
  const size = 32 * pulse;
  const topY = node.y - 48 * pulse;

  ctx.save();
  ctx.translate(node.x, topY);

  ctx.shadowColor = "rgba(239, 68, 68, 0.95)";
  ctx.shadowBlur = 25;

  // Warning Triangle
  ctx.beginPath();
  ctx.moveTo(0, -size / 2);
  ctx.lineTo(size * 0.65, size / 2);
  ctx.lineTo(-size * 0.65, size / 2);
  ctx.closePath();

  ctx.fillStyle = "#ef4444";
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 2.5;
  ctx.fill();
  ctx.stroke();

  // Exclamation mark (!)
  ctx.fillStyle = "#ffffff";
  ctx.font = `bold ${Math.round(18 * pulse)}px 'JetBrains Mono', monospace`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("!", 0, size * 0.12);

  ctx.restore();
}

function drawNodes() {
  CITY_NODES.forEach(node => {
    const isHospital = (node.type === "hospital");
    const isAmbulanceStation = (node.id === 2 || node.id === 7);
    const isAccident = (node.id === activeAccidentNode);
    const isHovered = (node.id === hoveredNodeId);

    const r = isAccident ? 28 : (isHospital || isAmbulanceStation ? 24 : 20);

    ctx.save();

    if (isAccident) {
      ctx.shadowColor = "rgba(245, 158, 11, 0.95)";
      ctx.shadowBlur = 24;
    } else if (isHospital) {
      ctx.shadowColor = "rgba(239, 68, 68, 0.9)";
      ctx.shadowBlur = 20;
    } else if (isAmbulanceStation) {
      ctx.shadowColor = "rgba(16, 185, 129, 0.7)";
      ctx.shadowBlur = 18;
    } else if (isHovered) {
      ctx.shadowColor = "rgba(59, 130, 246, 0.8)";
      ctx.shadowBlur = 16;
    }

    ctx.beginPath();
    ctx.arc(node.x, node.y, r, 0, Math.PI * 2);

    if (isAccident) {
      ctx.fillStyle = "#f59e0b";
      ctx.strokeStyle = "#ffffff";
      ctx.lineWidth = 3.5;
    } else if (isHospital) {
      ctx.fillStyle = "#ef4444"; // RED HOSPITAL
      ctx.strokeStyle = "#ffffff";
      ctx.lineWidth = 3;
    } else if (isAmbulanceStation) {
      ctx.fillStyle = "#10b981"; // GREEN AMBULANCE STATION
      ctx.strokeStyle = "#ffffff";
      ctx.lineWidth = 3;
    } else {
      ctx.fillStyle = "#1b2538";
      ctx.strokeStyle = isHovered ? "#38bdf8" : "#4b6288";
      ctx.lineWidth = 2.5;
    }

    ctx.fill();
    ctx.stroke();
    ctx.restore();

    // Node ID Label
    ctx.save();
    ctx.font = `800 ${isAccident ? 15 : 13}px 'JetBrains Mono', monospace`;
    ctx.fillStyle = (isAccident || isHospital || isAmbulanceStation) ? "#ffffff" : "#f8fafc";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(node.id, node.x, node.y);
    ctx.restore();

    // Node Name Tag
    drawNodeNameTag(node, r, isAccident, isHospital);

    // Hospital Badge
    if (isHospital) {
      ctx.font = "bold 15px sans-serif";
      ctx.textAlign = "center";
      ctx.fillText("🏥", node.x + r - 2, node.y - r + 4);
    }

    // SCALED UP (LARGE) AMBULANCE ICON AT IDLE STATIONS
    if (isAmbulanceStation && !isAnimating && animationLeg !== 2) {
      const amb = ambulances.find(a => a.currentNode === node.id);
      if (amb && amb.status === "Available") {
        drawLargeIdleAmbulanceIcon(node.x + r + 6, node.y - r + 4);
      }
    }
  });
}

function drawLargeIdleAmbulanceIcon(x, y) {
  ctx.save();
  ctx.translate(x, y);

  // Large Glowing Station Badge
  ctx.shadowColor = "rgba(16, 185, 129, 0.9)";
  ctx.shadowBlur = 18;

  ctx.fillStyle = "#0c1f17";
  ctx.strokeStyle = "#10b981";
  ctx.lineWidth = 2.5;
  ctx.beginPath();
  ctx.arc(0, 0, 18, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  // Large Crisp Emoji
  ctx.font = "20px sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("🚑", 0, 1);

  ctx.restore();
}

function drawNodeNameTag(node, r, isAccident, isHospital) {
  ctx.save();
  ctx.font = `700 ${isAccident ? 13 : 11}px 'Plus Jakarta Sans', sans-serif`;
  ctx.textAlign = "center";
  ctx.textBaseline = "top";

  const tagY = node.y + r + 8;
  const text = node.name;
  const metrics = ctx.measureText(text);
  const pw = metrics.width + 14;
  const ph = 20;

  ctx.fillStyle = "rgba(11, 17, 30, 0.92)";
  roundRect(ctx, node.x - pw / 2, tagY - 2, pw, ph, 5, true, false);

  if (isAccident) ctx.fillStyle = "#fde68a";
  else if (isHospital) ctx.fillStyle = "#fca5a5";
  else if (node.id === 2 || node.id === 7) ctx.fillStyle = "#86efac";
  else ctx.fillStyle = "#cbd5e1";

  ctx.fillText(text, node.x, tagY);
  ctx.restore();
}

// MOVING AMBULANCE (ROTATED DIRECTIONALLY ALONG ROAD)
function drawMovingAmbulance(x, y, angle) {
  ctx.save();
  ctx.translate(x, y);
  ctx.rotate(angle);

  // Rotating emergency light
  const t = performance.now() * 0.009;
  const sirenColor = Math.sin(t) > 0 ? "rgba(239, 68, 68, 0.95)" : "rgba(59, 130, 246, 0.95)";
  ctx.shadowColor = sirenColor;
  ctx.shadowBlur = 26;

  // Vehicle Body
  ctx.fillStyle = "#ffffff";
  ctx.strokeStyle = "#070a12";
  ctx.lineWidth = 2;
  roundRect(ctx, -22, -13, 44, 26, 6, true, true);

  // Red cross
  ctx.fillStyle = "#ef4444";
  ctx.fillRect(-12, -4, 8, 8);
  ctx.fillRect(-10, -7, 4, 14);

  // Front Windshield (Facing right)
  ctx.fillStyle = "#38bdf8";
  ctx.fillRect(8, -9, 8, 18);

  // Siren Light Bar
  ctx.fillStyle = Math.sin(t) > 0 ? "#ef4444" : "#3b82f6";
  roundRect(ctx, -4, -17, 8, 5, 2, true, false);

  // Wheels
  ctx.fillStyle = "#0f172a";
  roundRect(ctx, -16, -16, 7, 3, 1, true, false);
  roundRect(ctx, 9, -16, 7, 3, 1, true, false);
  roundRect(ctx, -16, 13, 7, 3, 1, true, false);
  roundRect(ctx, 9, 13, 7, 3, 1, true, false);

  ctx.restore();
}

// Canvas Mouse Interactions
function handleCanvasMouseMove(e) {
  const rect = canvas.getBoundingClientRect();
  const scaleX = canvas.width / rect.width;
  const scaleY = canvas.height / rect.height;

  const mouseX = (e.clientX - rect.left) * scaleX;
  const mouseY = (e.clientY - rect.top) * scaleY;

  let found = -1;
  CITY_NODES.forEach(node => {
    const dx = mouseX - node.x;
    const dy = mouseY - node.y;
    if (Math.hypot(dx, dy) <= 30) {
      found = node.id;
    }
  });

  if (hoveredNodeId !== found) {
    hoveredNodeId = found;
    canvas.style.cursor = found !== -1 ? "pointer" : "crosshair";
    drawGraph();
  }
}

function handleCanvasClick(e) {
  if (hoveredNodeId !== -1) {
    if (CITY_NODES[hoveredNodeId].type !== "hospital") {
      activeAccidentNode = hoveredNodeId;
      document.getElementById("accidentNodeSelect").value = activeAccidentNode;
      drawGraph();
    }
  }
}

function roundRect(ctx, x, y, width, height, radius, fill, stroke) {
  if (typeof radius === 'undefined') radius = 5;
  if (typeof radius === 'number') {
    radius = { tl: radius, tr: radius, br: radius, bl: radius };
  }
  ctx.beginPath();
  ctx.moveTo(x + radius.tl, y);
  ctx.lineTo(x + width - radius.tr, y);
  ctx.quadraticCurveTo(x + width, y, x + width, y + radius.tr);
  ctx.lineTo(x + width, y + height - radius.br);
  ctx.quadraticCurveTo(x + width, y + height, x + width - radius.br, y + height);
  ctx.lineTo(x + radius.bl, y + height);
  ctx.quadraticCurveTo(x, y + height, x, y + height - radius.bl);
  ctx.lineTo(x, y + radius.tl);
  ctx.quadraticCurveTo(x, y, x + radius.tl, y);
  ctx.closePath();
  if (fill) ctx.fill();
  if (stroke) ctx.stroke();
}
