/* ==============================================================================
 * SMART AMBULANCE FINDER - DAA MINI PROJECT
 * ------------------------------------------------------------------------------
 * Architecture:
 *   [1] Data Layer: Adjacency matrix, accessibility status, ambulance fleet
 *   [2] Core Algorithm Layer: Dijkstra O(V^2), Path Reconstruction, Rendezvous Engine
 *   [3] Application Layer: Dispatcher & Request State Machine
 *   [4] JSON Writer: Pure C fprintf-based state serializer (no external libraries)
 *   [5] File Bridge & Command Parser: Reads command.json, updates state.json
 *   [6] Built-in Mini HTTP Server (Winsock2): Zero-dependency local dashboard host
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define MAX_V 10
#define NUM_AMBULANCES 3
#define INF 999999
#define BUFFER_SIZE 65536

/* ==============================================================================
 * 1. DATA LAYER
 * ============================================================================== */

typedef struct {
    int id;
    char name[32];
    int x; // For 2D canvas visualization
    int y;
} Junction;

typedef struct {
    int id;
    char code[16];
    int junction;
    bool available;
    int activeRequestId;
    int targetJunction;
} Ambulance;

typedef enum {
    REQ_IDLE = 0,
    REQ_DISPATCHED = 1,
    REQ_RENDEZVOUS = 2,
    REQ_FAILED_NO_AMBULANCE = 3,
    REQ_FAILED_UNREACHABLE = 4,
    REQ_COMPLETED = 5
} RequestStatus;

typedef struct {
    int requestId;
    int patientJunction;
    bool patientAccessible;
    int assignedAmbulanceId;
    int destinationJunction; // Same as patientJunction unless rendezvous triggered
    int rendezvousJunction;  // -1 if direct
    int etaMinutes;
    int path[MAX_V];
    int pathLength;
    RequestStatus status;
    char message[128];
    char timestamp[32];
} DispatchRecord;

/* City Network Junctions */
static Junction junctions[MAX_V] = {
    {0, "Central Hospital",    120, 180},
    {1, "City Square",         270, 120},
    {2, "North Junction",      450,  90},
    {3, "East Market",         630, 170},
    {4, "West Highway",        180, 340},
    {5, "Metro Hub",           380, 260},
    {6, "Tech Park",           580, 310},
    {7, "South Industrial",    260, 480},
    {8, "Riverside Colony",    470, 470},
    {9, "Airport Bypass",      660, 460}
};

/* Road Network Adjacency Matrix: Travel time in minutes */
static int graph[MAX_V][MAX_V];

/* Junction Accessibility (1 = Open, 0 = Blocked / Flooded / Roadwork) */
static int isAccessible[MAX_V];

/* Ambulance Fleet */
static Ambulance ambulances[NUM_AMBULANCES] = {
    {1, "AMB-ALPHA", 0, true, 0, -1}, // Stationed at Central Hospital (0)
    {2, "AMB-BRAVO", 2, true, 0, -1}, // Stationed at North Junction (2)
    {3, "AMB-CHARLIE", 7, true, 0, -1} // Stationed at South Industrial (7)
};

/* Latest Dispatch Details & History */
static DispatchRecord lastDispatch;
static int totalRequestsServed = 0;
static int activeDispatches = 0;

/* ==============================================================================
 * NETWORK INITIALIZATION
 * ============================================================================== */

void initNetwork(void) {
    for (int i = 0; i < MAX_V; i++) {
        isAccessible[i] = 1;
        for (int j = 0; j < MAX_V; j++) {
            graph[i][j] = (i == j) ? 0 : INF;
        }
    }

    // Helper lambda-like macro to add bidirectional edges with weights (in minutes)
    #define ADD_ROAD(u, v, w) do { graph[u][v] = (w); graph[v][u] = (w); } while(0)

    ADD_ROAD(0, 1, 6);   // Central Hospital <-> City Square (6 mins)
    ADD_ROAD(0, 4, 8);   // Central Hospital <-> West Highway (8 mins)
    ADD_ROAD(1, 2, 5);   // City Square <-> North Junction (5 mins)
    ADD_ROAD(1, 5, 7);   // City Square <-> Metro Hub (7 mins)
    ADD_ROAD(2, 3, 9);   // North Junction <-> East Market (9 mins)
    ADD_ROAD(2, 5, 6);   // North Junction <-> Metro Hub (6 mins)
    ADD_ROAD(3, 6, 8);   // East Market <-> Tech Park (8 mins)
    ADD_ROAD(4, 5, 9);   // West Highway <-> Metro Hub (9 mins)
    ADD_ROAD(4, 7, 7);   // West Highway <-> South Industrial (7 mins)
    ADD_ROAD(5, 6, 5);   // Metro Hub <-> Tech Park (5 mins)
    ADD_ROAD(5, 7, 10);  // Metro Hub <-> South Industrial (10 mins)
    ADD_ROAD(5, 8, 8);   // Metro Hub <-> Riverside Colony (8 mins)
    ADD_ROAD(6, 9, 6);   // Tech Park <-> Airport Bypass (6 mins)
    ADD_ROAD(7, 8, 6);   // South Industrial <-> Riverside Colony (6 mins)
    ADD_ROAD(8, 9, 7);   // Riverside Colony <-> Airport Bypass (7 mins)

    #undef ADD_ROAD

    memset(&lastDispatch, 0, sizeof(DispatchRecord));
    lastDispatch.status = REQ_IDLE;
    lastDispatch.rendezvousJunction = -1;
    strcpy(lastDispatch.message, "System initialized. Fleet ready for dispatch.");
}

/* ==============================================================================
 * 2. CORE ALGORITHM LAYER (DAA EXCELLENCE)
 * ==============================================================================
 * 
 * Algorithm 1: Dijkstra's Single-Source Shortest Path
 * ----------------------------------------------------
 * Time Complexity  : O(V^2) using Adjacency Matrix
 * Space Complexity : O(V) auxiliary space (dist, visited, parent arrays)
 * 
 * In standard urban city networks with dense arterial junctions, the O(V^2) 
 * matrix approach avoids heap overhead and provides deterministic cache-friendly
 * traversal. Road accessibility is integrated directly: if isAccessible[v] == 0,
 * node v cannot be used as an intermediate transit node.
 * ============================================================================== */

int findMinDistanceVertex(const int dist[], const bool visited[]) {
    int minVal = INF;
    int minIndex = -1;

    for (int v = 0; v < MAX_V; v++) {
        if (!visited[v] && dist[v] <= minVal) {
            minVal = dist[v];
            minIndex = v;
        }
    }
    return minIndex;
}

void dijkstra(int src, int dist[MAX_V], int parent[MAX_V], bool allowDestBlocked, int targetDest) {
    bool visited[MAX_V];

    for (int i = 0; i < MAX_V; i++) {
        dist[i] = INF;
        visited[i] = false;
        parent[i] = -1;
    }

    dist[src] = 0;

    for (int count = 0; count < MAX_V - 1; count++) {
        int u = findMinDistanceVertex(dist, visited);
        if (u == -1 || dist[u] == INF) break;

        visited[u] = true;

        for (int v = 0; v < MAX_V; v++) {
            // Relaxation condition:
            // 1. Edge exists
            // 2. Vertex v is unvisited
            // 3. Junction v is accessible (OR v is the designated destination if allowed)
            // 4. Distance through u is shorter
            bool canPass = isAccessible[v] || (allowDestBlocked && v == targetDest);

            if (!visited[v] && graph[u][v] != INF && canPass) {
                if (dist[u] + graph[u][v] < dist[v]) {
                    dist[v] = dist[u] + graph[u][v];
                    parent[v] = u;
                }
            }
        }
    }
}

/* Backtracks through parent[] to reconstruct shortest path sequence */
int reconstructPath(int src, int dest, const int parent[], int outPath[]) {
    if (src == dest) {
        outPath[0] = src;
        return 1;
    }
    if (parent[dest] == -1) {
        return 0; // Unreachable
    }

    int temp[MAX_V];
    int count = 0;
    int curr = dest;

    while (curr != -1) {
        temp[count++] = curr;
        if (curr == src) break;
        curr = parent[curr];
    }

    // Reverse to get path from source to destination
    for (int i = 0; i < count; i++) {
        outPath[i] = temp[count - 1 - i];
    }
    return count;
}

/* ==============================================================================
 * Algorithm 2: Rendezvous Point Identification (O(V) scan)
 * ----------------------------------------------------
 * When a patient's junction is blocked/flooded/inaccessible (isAccessible[P] == 0),
 * an ambulance cannot reach them directly.
 * 
 * The Rendezvous Engine performs an O(V) neighborhood scan from P to find the 
 * closest accessible junction R such that:
 *     R = argmin { graph[P][j] } for all j where isAccessible[j] == 1
 * 
 * If no direct neighbor is accessible, it executes Dijkstra from P to compute 
 * the shortest evacuation distance to ANY accessible junction in the city.
 * ============================================================================== */

int findRendezvousPoint(int patientJunction, int *outWalkMinutes) {
    int bestJunction = -1;
    int minWalk = INF;

    // Direct neighbor check first (O(V))
    for (int j = 0; j < MAX_V; j++) {
        if (graph[patientJunction][j] != INF && isAccessible[j] == 1) {
            if (graph[patientJunction][j] < minWalk) {
                minWalk = graph[patientJunction][j];
                bestJunction = j;
            }
        }
    }

    // If direct neighbors are also blocked, scan network via Dijkstra (O(V^2))
    if (bestJunction == -1) {
        int tempDist[MAX_V];
        // Allow walking through graph to nearest safe node
        bool visited[MAX_V] = {false};
        for (int i = 0; i < MAX_V; i++) tempDist[i] = INF;
        tempDist[patientJunction] = 0;

        for (int c = 0; c < MAX_V - 1; c++) {
            int u = findMinDistanceVertex(tempDist, visited);
            if (u == -1 || tempDist[u] == INF) break;
            visited[u] = true;
            for (int v = 0; v < MAX_V; v++) {
                if (!visited[v] && graph[u][v] != INF) {
                    if (tempDist[u] + graph[u][v] < tempDist[v]) {
                        tempDist[v] = tempDist[u] + graph[u][v];
                    }
                }
            }
        }

        for (int j = 0; j < MAX_V; j++) {
            if (isAccessible[j] == 1 && tempDist[j] < minWalk) {
                minWalk = tempDist[j];
                bestJunction = j;
            }
        }
    }

    if (outWalkMinutes) *outWalkMinutes = (minWalk == INF) ? 0 : minWalk;
    return bestJunction;
}

/* ==============================================================================
 * 3. APPLICATION LAYER (Dispatcher & Request State Machine)
 * ============================================================================== */

bool dispatchAmbulance(int patientJunction, bool patientAccessible) {
    if (patientJunction < 0 || patientJunction >= MAX_V) {
        strcpy(lastDispatch.message, "Invalid junction requested.");
        lastDispatch.status = REQ_FAILED_UNREACHABLE;
        return false;
    }

    // Update accessibility status of the target junction
    isAccessible[patientJunction] = patientAccessible ? 1 : 0;

    int destinationJunction = patientJunction;
    int rendezvousJunction = -1;
    int walkTime = 0;
    bool isRendezvousNeeded = !patientAccessible;

    if (isRendezvousNeeded) {
        rendezvousJunction = findRendezvousPoint(patientJunction, &walkTime);
        if (rendezvousJunction == -1) {
            lastDispatch.requestId = ++totalRequestsServed;
            lastDispatch.patientJunction = patientJunction;
            lastDispatch.patientAccessible = false;
            lastDispatch.status = REQ_FAILED_UNREACHABLE;
            strcpy(lastDispatch.message, "CRITICAL: Patient location and surrounding area completely isolated!");
            return false;
        }
        destinationJunction = rendezvousJunction;
    }

    // Fleet Coordinator: Find nearest available ambulance to destination
    int bestAmbulanceIdx = -1;
    int minTravelTime = INF;
    int bestParent[MAX_V];
    int bestPath[MAX_V];
    int bestPathLen = 0;

    for (int i = 0; i < NUM_AMBULANCES; i++) {
        if (!ambulances[i].available) continue;

        int dist[MAX_V];
        int parent[MAX_V];
        dijkstra(ambulances[i].junction, dist, parent, false, destinationJunction);

        if (dist[destinationJunction] < minTravelTime) {
            minTravelTime = dist[destinationJunction];
            bestAmbulanceIdx = i;
            memcpy(bestParent, parent, sizeof(bestParent));
        }
    }

    lastDispatch.requestId = ++totalRequestsServed;
    lastDispatch.patientJunction = patientJunction;
    lastDispatch.patientAccessible = patientAccessible;
    lastDispatch.rendezvousJunction = rendezvousJunction;
    lastDispatch.destinationJunction = destinationJunction;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(lastDispatch.timestamp, sizeof(lastDispatch.timestamp), "%H:%M:%S", t);

    if (bestAmbulanceIdx == -1) {
        lastDispatch.assignedAmbulanceId = -1;
        lastDispatch.etaMinutes = 0;
        lastDispatch.pathLength = 0;
        lastDispatch.status = REQ_FAILED_NO_AMBULANCE;
        strcpy(lastDispatch.message, "All ambulances are currently BUSY. Request queued.");
        return false;
    }

    if (minTravelTime >= INF) {
        lastDispatch.assignedAmbulanceId = -1;
        lastDispatch.etaMinutes = 0;
        lastDispatch.pathLength = 0;
        lastDispatch.status = REQ_FAILED_UNREACHABLE;
        strcpy(lastDispatch.message, "No accessible road route found from any ambulance base.");
        return false;
    }

    // Reconstruct route
    bestPathLen = reconstructPath(ambulances[bestAmbulanceIdx].junction, destinationJunction, bestParent, bestPath);

    // Update fleet state
    ambulances[bestAmbulanceIdx].available = false;
    ambulances[bestAmbulanceIdx].activeRequestId = lastDispatch.requestId;
    ambulances[bestAmbulanceIdx].targetJunction = destinationJunction;
    activeDispatches++;

    // Populate dispatch record
    lastDispatch.assignedAmbulanceId = ambulances[bestAmbulanceIdx].id;
    lastDispatch.etaMinutes = minTravelTime;
    lastDispatch.pathLength = bestPathLen;
    for (int k = 0; k < bestPathLen; k++) {
        lastDispatch.path[k] = bestPath[k];
    }

    if (isRendezvousNeeded) {
        lastDispatch.status = REQ_RENDEZVOUS;
        snprintf(lastDispatch.message, sizeof(lastDispatch.message),
            "RENDEZVOUS: %s dispatched to %s. Patient walks %d min from %s.",
            ambulances[bestAmbulanceIdx].code, junctions[destinationJunction].name, walkTime, junctions[patientJunction].name);
    } else {
        lastDispatch.status = REQ_DISPATCHED;
        snprintf(lastDispatch.message, sizeof(lastDispatch.message),
            "DISPATCHED: %s assigned. ETA: %d min to %s.",
            ambulances[bestAmbulanceIdx].code, minTravelTime, junctions[patientJunction].name);
    }

    return true;
}

void completeAmbulanceTrip(int ambulanceId) {
    for (int i = 0; i < NUM_AMBULANCES; i++) {
        if (ambulances[i].id == ambulanceId && !ambulances[i].available) {
            ambulances[i].available = true;
            ambulances[i].junction = ambulances[i].targetJunction; // Ambulance is now at destination
            ambulances[i].targetJunction = -1;
            ambulances[i].activeRequestId = 0;
            if (activeDispatches > 0) activeDispatches--;
            snprintf(lastDispatch.message, sizeof(lastDispatch.message),
                "%s completed mission and is now stationed at %s.",
                ambulances[i].code, junctions[ambulances[i].junction].name);
            lastDispatch.status = REQ_COMPLETED;
            return;
        }
    }
}

void resetAllAmbulances(void) {
    ambulances[0].junction = 0; ambulances[0].available = true; ambulances[0].targetJunction = -1;
    ambulances[1].junction = 2; ambulances[1].available = true; ambulances[1].targetJunction = -1;
    ambulances[2].junction = 7; ambulances[2].available = true; ambulances[2].targetJunction = -1;
    activeDispatches = 0;
    lastDispatch.status = REQ_IDLE;
    strcpy(lastDispatch.message, "Fleet reset: all 3 ambulances returned to base stations.");
}

/* ==============================================================================
 * 4. JSON WRITER (fprintf only - pure standard C)
 * ============================================================================== */

void writeStateJson(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "[Error] Could not open %s for writing\n", filename);
        return;
    }

    int availableCount = 0;
    for (int i = 0; i < NUM_AMBULANCES; i++) {
        if (ambulances[i].available) availableCount++;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"project\": \"Smart Ambulance Finder (DAA)\",\n");
    fprintf(fp, "    \"numJunctions\": %d,\n", MAX_V);
    fprintf(fp, "    \"numAmbulances\": %d\n", NUM_AMBULANCES);
    fprintf(fp, "  },\n");

    fprintf(fp, "  \"statistics\": {\n");
    fprintf(fp, "    \"totalRequests\": %d,\n", totalRequestsServed);
    fprintf(fp, "    \"activeDispatches\": %d,\n", activeDispatches);
    fprintf(fp, "    \"availableAmbulances\": %d,\n", availableCount);
    fprintf(fp, "    \"busyAmbulances\": %d\n", NUM_AMBULANCES - availableCount);
    fprintf(fp, "  },\n");

    /* Junctions */
    fprintf(fp, "  \"junctions\": [\n");
    for (int i = 0; i < MAX_V; i++) {
        fprintf(fp, "    {\"id\": %d, \"name\": \"%s\", \"x\": %d, \"y\": %d, \"isAccessible\": %d}%s\n",
            junctions[i].id, junctions[i].name, junctions[i].x, junctions[i].y, isAccessible[i],
            (i == MAX_V - 1) ? "" : ",");
    }
    fprintf(fp, "  ],\n");

    /* Edges */
    fprintf(fp, "  \"edges\": [\n");
    bool firstEdge = true;
    for (int i = 0; i < MAX_V; i++) {
        for (int j = i + 1; j < MAX_V; j++) {
            if (graph[i][j] != INF) {
                if (!firstEdge) fprintf(fp, ",\n");
                fprintf(fp, "    {\"u\": %d, \"v\": %d, \"weight\": %d}", i, j, graph[i][j]);
                firstEdge = false;
            }
        }
    }
    fprintf(fp, "\n  ],\n");

    /* Ambulances */
    fprintf(fp, "  \"ambulances\": [\n");
    for (int i = 0; i < NUM_AMBULANCES; i++) {
        fprintf(fp, "    {\"id\": %d, \"code\": \"%s\", \"junction\": %d, \"available\": %s, \"target\": %d}%s\n",
            ambulances[i].id, ambulances[i].code, ambulances[i].junction,
            ambulances[i].available ? "true" : "false", ambulances[i].targetJunction,
            (i == NUM_AMBULANCES - 1) ? "" : ",");
    }
    fprintf(fp, "  ],\n");

    /* Last Dispatch Record */
    fprintf(fp, "  \"lastDispatch\": {\n");
    fprintf(fp, "    \"requestId\": %d,\n", lastDispatch.requestId);
    fprintf(fp, "    \"patientJunction\": %d,\n", lastDispatch.patientJunction);
    fprintf(fp, "    \"patientAccessible\": %s,\n", lastDispatch.patientAccessible ? "true" : "false");
    fprintf(fp, "    \"assignedAmbulanceId\": %d,\n", lastDispatch.assignedAmbulanceId);
    fprintf(fp, "    \"destinationJunction\": %d,\n", lastDispatch.destinationJunction);
    fprintf(fp, "    \"rendezvousJunction\": %d,\n", lastDispatch.rendezvousJunction);
    fprintf(fp, "    \"etaMinutes\": %d,\n", lastDispatch.etaMinutes);
    fprintf(fp, "    \"status\": %d,\n", (int)lastDispatch.status);
    fprintf(fp, "    \"message\": \"%s\",\n", lastDispatch.message);
    fprintf(fp, "    \"timestamp\": \"%s\",\n", lastDispatch.timestamp);
    fprintf(fp, "    \"path\": [");
    for (int k = 0; k < lastDispatch.pathLength; k++) {
        fprintf(fp, "%d%s", lastDispatch.path[k], (k == lastDispatch.pathLength - 1) ? "" : ", ");
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  }\n");

    fprintf(fp, "}\n");
    fclose(fp);
}

/* ==============================================================================
 * 5. FILE BRIDGE & COMMAND PARSER
 * ============================================================================== */

static bool extractJsonString(const char *json, const char *key, char *outVal, size_t maxLen) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *ptr = strstr(json, searchKey);
    if (!ptr) return false;
    ptr += strlen(searchKey);
    while (*ptr && (*ptr == ' ' || *ptr == ':' || *ptr == '\t' || *ptr == '\"')) {
        if (*ptr == '\"') { ptr++; break; }
        ptr++;
    }
    size_t idx = 0;
    while (*ptr && *ptr != '\"' && idx < maxLen - 1) {
        outVal[idx++] = *ptr++;
    }
    outVal[idx] = '\0';
    return idx > 0;
}

static bool extractJsonInt(const char *json, const char *key, int *outVal) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *ptr = strstr(json, searchKey);
    if (!ptr) return false;
    ptr += strlen(searchKey);
    while (*ptr && (*ptr == ' ' || *ptr == ':' || *ptr == '\t')) ptr++;
    if (*ptr) {
        *outVal = atoi(ptr);
        return true;
    }
    return false;
}

bool processCommandJson(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return false;

    char buffer[2048] = {0};
    size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);
    if (bytes == 0) return false;

    // Delete or clear command.json so it isn't processed repeatedly
    remove(filename);

    char action[32] = {0};
    int junction = -1;
    int accessible = 1;
    int ambulanceId = -1;

    extractJsonString(buffer, "action", action, sizeof(action));
    extractJsonInt(buffer, "junction", &junction);
    extractJsonInt(buffer, "accessible", &accessible);
    extractJsonInt(buffer, "ambulanceId", &ambulanceId);

    printf("[Bridge] Executing action: '%s' (junction: %d, accessible: %d, ambId: %d)\n",
        action, junction, accessible, ambulanceId);
    fflush(stdout);

    if (strcmp(action, "dispatch") == 0) {
        dispatchAmbulance(junction, accessible != 0);
    } else if (strcmp(action, "complete") == 0) {
        completeAmbulanceTrip(ambulanceId);
    } else if (strcmp(action, "toggle_access") == 0) {
        if (junction >= 0 && junction < MAX_V) {
            isAccessible[junction] = accessible ? 1 : 0;
            snprintf(lastDispatch.message, sizeof(lastDispatch.message),
                "Junction %s accessibility updated to: %s",
                junctions[junction].name, isAccessible[junction] ? "OPEN" : "BLOCKED");
        }
    } else if (strcmp(action, "reset") == 0) {
        resetAllAmbulances();
    } else {
        printf("[Bridge] Unknown action '%s'\n", action);
        fflush(stdout);
        return false;
    }

    writeStateJson("state.json");
    return true;
}

/* ==============================================================================
 * 6. BUILT-IN ZERO-DEPENDENCY HTTP SERVER
 * ==============================================================================
 * Enables launching the project directly with:
 *   smart_ambulance.exe --serve 8080
 * Serves index.html, style.css, app.js, state.json, and handles POST /api/command!
 * ============================================================================== */

#ifdef _WIN32
void startHttpServer(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        WSACleanup();
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed on port %d\n", port);
        closesocket(server_fd);
        WSACleanup();
        return;
    }

    if (listen(server_fd, 10) == SOCKET_ERROR) {
        fprintf(stderr, "Listen failed\n");
        closesocket(server_fd);
        WSACleanup();
        return;
    }

    printf("\n===============================================================\n");
    printf(" SMART AMBULANCE FINDER - DAA PROJECT DASHBOARD ACTIVE\n");
    printf(" Open in your browser: http://localhost:%d\n", port);
    printf(" Press Ctrl+C in this terminal to stop.\n");
    printf("===============================================================\n\n");
    fflush(stdout);

    // Write initial state
    writeStateJson("state.json");

    while (1) {
        SOCKET client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == INVALID_SOCKET) continue;

        char reqBuffer[BUFFER_SIZE] = {0};
        int totalReceived = 0;
        int bytesRead = recv(client_fd, reqBuffer, sizeof(reqBuffer) - 1, 0);
        if (bytesRead <= 0) {
            closesocket(client_fd);
            continue;
        }
        totalReceived = bytesRead;

        char method[16] = {0};
        char path[128] = {0};
        sscanf(reqBuffer, "%15s %127s", method, path);

        // Handle POST /api/command or /command
        if (strcmp(method, "POST") == 0 && (strcmp(path, "/api/command") == 0 || strcmp(path, "/command") == 0)) {
            // Check if full body was received; if not, read remaining
            int contentLength = 0;
            char *clHeader = strstr(reqBuffer, "Content-Length:");
            if (!clHeader) clHeader = strstr(reqBuffer, "content-length:");
            if (clHeader) {
                sscanf(clHeader + 15, "%d", &contentLength);
            }

            char *body = strstr(reqBuffer, "\r\n\r\n");
            int bodyReceived = 0;
            if (body) {
                body += 4;
                bodyReceived = totalReceived - (int)(body - reqBuffer);
            }

            while (contentLength > 0 && bodyReceived < contentLength && totalReceived < (int)sizeof(reqBuffer) - 1) {
                int extra = recv(client_fd, reqBuffer + totalReceived, sizeof(reqBuffer) - 1 - totalReceived, 0);
                if (extra <= 0) break;
                totalReceived += extra;
                bodyReceived += extra;
            }

            if (body) {
                printf("[Server] Received POST command: %s\n", body);
                fflush(stdout);
                FILE *cmdFp = fopen("command.json", "w");
                if (cmdFp) {
                    fputs(body, cmdFp);
                    fclose(cmdFp);
                    processCommandJson("command.json");
                }
            }

            const char *res = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 15\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n"
                "Connection: close\r\n\r\n"
                "{\"status\":\"ok\"}";
            send(client_fd, res, (int)strlen(res), 0);
            shutdown(client_fd, SD_SEND);
            closesocket(client_fd);
            continue;
        }

        // Handle OPTIONS for CORS
        if (strcmp(method, "OPTIONS") == 0) {
            const char *res = 
                "HTTP/1.1 204 No Content\r\n"
                "Content-Length: 0\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n"
                "Connection: close\r\n\r\n";
            send(client_fd, res, (int)strlen(res), 0);
            shutdown(client_fd, SD_SEND);
            closesocket(client_fd);
            continue;
        }

        // Route static files
        char filename[128] = "index.html";
        const char *contentType = "text/html";

        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            strcpy(filename, "index.html");
            contentType = "text/html";
        } else if (strcmp(path, "/style.css") == 0) {
            strcpy(filename, "style.css");
            contentType = "text/css";
        } else if (strcmp(path, "/app.js") == 0) {
            strcpy(filename, "app.js");
            contentType = "application/javascript";
        } else if (strcmp(path, "/state.json") == 0) {
            writeStateJson("state.json");
            strcpy(filename, "state.json");
            contentType = "application/json";
        }

        FILE *f = fopen(filename, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);

            char *fileData = (char *)malloc(fsize);
            if (fileData) {
                fread(fileData, 1, fsize, f);
                char header[512];
                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %ld\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Connection: close\r\n\r\n", contentType, fsize);
                send(client_fd, header, (int)strlen(header), 0);
                send(client_fd, fileData, (int)fsize, 0);
                free(fileData);
            }
            fclose(f);
        } else {
            const char *notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\nConnection: close\r\n\r\n404 Not Found";
            send(client_fd, notFound, (int)strlen(notFound), 0);
        }

        shutdown(client_fd, SD_SEND);
        closesocket(client_fd);
    }

    closesocket(server_fd);
    WSACleanup();
}
#endif

/* ==============================================================================
 * 7. INTERACTIVE CLI / FILE BRIDGE POLLER
 * ============================================================================== */

void printCliBanner(void) {
    printf("=================================================================\n");
    printf("       SMART AMBULANCE FINDER - DAA ALGORITHM ENGINE (C)         \n");
    printf("=================================================================\n");
    printf(" Junctions: 10 | Ambulances: 3 | Dijkstra: O(V^2) Matrix        \n");
    printf(" Rendezvous Scan: O(V) on Inaccessible Emergency Nodes           \n");
    printf(" Output: state.json (JSON Bridge)                                \n");
    printf("=================================================================\n");
}

void printCliMenu(void) {
    printf("\n--- COMMAND OPTIONS ---\n");
    printf(" [1] New Emergency Dispatch (Patient Junction + Accessibility)\n");
    printf(" [2] Toggle Junction Accessibility (Block / Unblock Road)\n");
    printf(" [3] Complete Mission / Free Ambulance\n");
    printf(" [4] Display Road Network Matrix & Fleets\n");
    printf(" [5] Reset Fleet to Station Bases\n");
    printf(" [6] Watch Mode (Poll command.json every 1 sec)\n");
    printf(" [7] Exit\n");
    printf("Select option (1-7): ");
}

void displayNetworkCli(void) {
    printf("\n=== CITY JUNCTIONS ===\n");
    for (int i = 0; i < MAX_V; i++) {
        printf(" [%d] %-20s (Status: %s)\n",
            junctions[i].id, junctions[i].name, isAccessible[i] ? "ACCESSIBLE" : "BLOCKED");
    }

    printf("\n=== AMBULANCE FLEET ===\n");
    for (int i = 0; i < NUM_AMBULANCES; i++) {
        printf(" %-12s | Station: [%d] %-18s | Status: %s\n",
            ambulances[i].code, ambulances[i].junction, junctions[ambulances[i].junction].name,
            ambulances[i].available ? "AVAILABLE" : "ON ACTIVE ROUTE");
    }
}

int main(int argc, char *argv[]) {
    initNetwork();
    writeStateJson("state.json");

    #ifdef _WIN32
    // Check if launched with --serve
    if (argc >= 2 && strcmp(argv[1], "--serve") == 0) {
        int port = 8080;
        if (argc >= 3) port = atoi(argv[2]);
        startHttpServer(port);
        return 0;
    }
    #endif

    // Check if launched in watch mode
    if (argc >= 2 && strcmp(argv[1], "--watch") == 0) {
        printf("[Watch Mode] Listening for command.json ... (Press Ctrl+C to stop)\n");
        while (1) {
            processCommandJson("command.json");
            #ifdef _WIN32
            Sleep(1000);
            #else
            sleep(1);
            #endif
        }
        return 0;
    }

    printCliBanner();

    int choice;
    while (1) {
        printCliMenu();
        if (scanf("%d", &choice) != 1) {
            break;
        }

        if (choice == 1) {
            int pj, acc;
            printf("\nEnter Patient Junction (0 to %d): ", MAX_V - 1);
            scanf("%d", &pj);
            printf("Is Road to Junction Accessible? (1 = Yes, 0 = Blocked/Flooded): ");
            scanf("%d", &acc);

            printf("\n--> Executing Dijkstra & Dispatch Engine...\n");
            dispatchAmbulance(pj, acc != 0);
            writeStateJson("state.json");

            printf("\nRESULT:\n");
            printf(" Status : %s\n", lastDispatch.status == REQ_RENDEZVOUS ? "RENDEZVOUS REQUIRED" : "DISPATCHED");
            printf(" Message: %s\n", lastDispatch.message);
            printf(" ETA    : %d minutes\n", lastDispatch.etaMinutes);
            if (lastDispatch.pathLength > 0) {
                printf(" Route  : ");
                for (int k = 0; k < lastDispatch.pathLength; k++) {
                    printf("[%d] %s%s", lastDispatch.path[k], junctions[lastDispatch.path[k]].name,
                        (k == lastDispatch.pathLength - 1) ? "" : " -> ");
                }
                printf("\n");
            }
            printf("--> state.json updated.\n");

        } else if (choice == 2) {
            int junc, acc;
            printf("Enter Junction ID (0 to %d): ", MAX_V - 1);
            scanf("%d", &junc);
            printf("Set Accessibility (1 = Open, 0 = Blocked): ");
            scanf("%d", &acc);

            if (junc >= 0 && junc < MAX_V) {
                isAccessible[junc] = acc ? 1 : 0;
                writeStateJson("state.json");
                printf("Junction [%d] %s updated to: %s\n", junc, junctions[junc].name, isAccessible[junc] ? "OPEN" : "BLOCKED");
            } else {
                printf("Invalid junction ID.\n");
            }

        } else if (choice == 3) {
            int ambId;
            printf("Enter Ambulance ID to Complete Trip (1 to %d): ", NUM_AMBULANCES);
            scanf("%d", &ambId);
            completeAmbulanceTrip(ambId);
            writeStateJson("state.json");
            printf("Trip completed. Ambulance freed.\n");

        } else if (choice == 4) {
            displayNetworkCli();

        } else if (choice == 5) {
            resetAllAmbulances();
            writeStateJson("state.json");
            printf("Fleet reset.\n");

        } else if (choice == 6) {
            printf("\n[Watch Mode] Watching for command.json. Press Ctrl+C to return to menu.\n");
            while (1) {
                if (processCommandJson("command.json")) {
                    printf("Processed command.json successfully.\n");
                }
                #ifdef _WIN32
                Sleep(1000);
                #else
                sleep(1);
                #endif
            }

        } else if (choice == 7) {
            printf("Exiting Smart Ambulance Finder.\n");
            break;
        }
    }

    return 0;
}
