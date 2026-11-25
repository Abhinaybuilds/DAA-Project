/* ==============================================================================
 * SMART AMBULANCE ROUTE FINDER - C HTTP BACKEND
 * ------------------------------------------------------------------------------
 * Emergency Round-Trip Simulation:
 *   Leg 1: Nearest Ambulance (Node 2 or 7) -> Accident Location
 *   Leg 2: Accident Location -> Nearest Hospital (Node 0 or Node 10)
 *   Broken Road Rerouting via Dijkstra O(V^2)
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define SD_SEND SHUT_WR
#endif

#define MAX_V 11
#define NUM_AMBULANCES 2
#define NUM_HOSPITALS 2
#define INF 999999
#define BUFFER_SIZE 65536
#define PORT 8080

typedef struct {
    int id;
    char name[32];
    int x;
    int y;
    bool isHospital;
} Junction;

typedef struct {
    int id;
    char name[32];
    int stationNode;
    int currentNode;
    char status[16];
} Ambulance;

static Junction nodes[MAX_V] = {
    {0, "Central Hospital",    250, 300, true},
    {1, "City Square",         450, 220, false},
    {2, "North Junction",      700, 300, false},
    {3, "East Market",         920, 300, false},
    {4, "West Highway",        320, 520, false},
    {5, "Metro Hub",           580, 420, false},
    {6, "Tech Park",           840, 480, false},
    {7, "South Industrial",    400, 700, false},
    {8, "Riverside Colony",    680, 700, false},
    {9, "Airport Bypass",      940, 660, false},
    {10, "Northside Clinic",   700, 150, true}
};

static int hospitalNodes[NUM_HOSPITALS] = {0, 10};

static int graph[MAX_V][MAX_V];

static Ambulance ambulances[NUM_AMBULANCES] = {
    {1, "Ambulance 1", 2, 2, "Available"},
    {2, "Ambulance 2", 7, 7, "Available"}
};

void initGraph(void) {
    for (int i = 0; i < MAX_V; i++) {
        for (int j = 0; j < MAX_V; j++) {
            graph[i][j] = (i == j) ? 0 : INF;
        }
    }
    #define ADD_E(u, v, w) do { graph[u][v] = (w); graph[v][u] = (w); } while(0)
    ADD_E(0, 1, 6);
    ADD_E(1, 2, 5);
    ADD_E(0, 4, 8);
    ADD_E(2, 5, 6);
    ADD_E(2, 3, 9);
    ADD_E(1, 5, 7);
    ADD_E(3, 6, 8);
    ADD_E(4, 7, 7);
    ADD_E(4, 5, 9);
    ADD_E(5, 7, 10);
    ADD_E(5, 6, 5);
    ADD_E(5, 8, 8);
    ADD_E(6, 9, 6);
    ADD_E(8, 9, 7);
    ADD_E(7, 8, 6);
    ADD_E(2, 10, 4);
    ADD_E(1, 10, 6);
    ADD_E(3, 10, 7);
    #undef ADD_E
}

void dijkstra(int src, int dist[MAX_V], int parent[MAX_V], int brokenU, int brokenV) {
    bool visited[MAX_V];
    for (int i = 0; i < MAX_V; i++) {
        dist[i] = INF;
        visited[i] = false;
        parent[i] = -1;
    }
    dist[src] = 0;

    for (int count = 0; count < MAX_V - 1; count++) {
        int u = -1, minVal = INF;
        for (int v = 0; v < MAX_V; v++) {
            if (!visited[v] && dist[v] < minVal) {
                minVal = dist[v];
                u = v;
            }
        }
        if (u == -1 || dist[u] == INF) break;
        visited[u] = true;

        for (int v = 0; v < MAX_V; v++) {
            if (graph[u][v] == INF) continue;

            if ((u == brokenU && v == brokenV) || (u == brokenV && v == brokenU)) {
                continue;
            }

            if (!visited[v]) {
                if (dist[u] + graph[u][v] < dist[v]) {
                    dist[v] = dist[u] + graph[u][v];
                    parent[v] = u;
                }
            }
        }
    }
}

int reconstructPath(int src, int dest, const int parent[], int outPath[]) {
    if (src == dest) {
        outPath[0] = src;
        return 1;
    }
    if (parent[dest] == -1) return 0;
    int temp[MAX_V];
    int count = 0;
    int curr = dest;
    while (curr != -1) {
        temp[count++] = curr;
        if (curr == src) break;
        curr = parent[curr];
    }
    for (int i = 0; i < count; i++) {
        outPath[i] = temp[count - 1 - i];
    }
    return count;
}

void sendJsonResponse(SOCKET client_fd, int statusCode, const char *statusText, const char *jsonBody) {
    char header[512];
    int bodyLen = (int)strlen(jsonBody);
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n", statusCode, statusText, bodyLen);

    send(client_fd, header, (int)strlen(header), 0);
    send(client_fd, jsonBody, bodyLen, 0);
    shutdown(client_fd, SD_SEND);
    closesocket(client_fd);
}

int main(void) {
    initGraph();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed\n");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed on port %d\n", PORT);
        closesocket(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) == SOCKET_ERROR) {
        fprintf(stderr, "Listen failed\n");
        closesocket(server_fd);
        return 1;
    }

    printf("===============================================================\n");
    printf(" SMART AMBULANCE ROUTE FINDER - BACKEND ACTIVE (C Server)\n");
    printf(" Open in your browser: http://localhost:%d\n", PORT);
    printf("===============================================================\n");
    fflush(stdout);

    while (1) {
        SOCKET client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == INVALID_SOCKET) continue;

        char buffer[BUFFER_SIZE] = {0};
        int totalReceived = 0;
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            closesocket(client_fd);
            continue;
        }
        totalReceived = bytes;

        char method[16] = {0};
        char path[128] = {0};
        sscanf(buffer, "%15s %127s", method, path);

        if (strcmp(method, "POST") == 0) {
            int cl = 0;
            char *clHdr = strstr(buffer, "Content-Length:");
            if (!clHdr) clHdr = strstr(buffer, "content-length:");
            if (clHdr) sscanf(clHdr + 15, "%d", &cl);

            char *bodyPtr = strstr(buffer, "\r\n\r\n");
            int bodyRecv = 0;
            if (bodyPtr) {
                bodyPtr += 4;
                bodyRecv = totalReceived - (int)(bodyPtr - buffer);
            }
            while (cl > 0 && bodyRecv < cl && totalReceived < (int)sizeof(buffer) - 1) {
                int extra = recv(client_fd, buffer + totalReceived, sizeof(buffer) - 1 - totalReceived, 0);
                if (extra <= 0) break;
                totalReceived += extra;
                bodyRecv += extra;
            }
        }

        if (strcmp(method, "OPTIONS") == 0) {
            const char *res = 
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n"
                "Connection: close\r\n\r\n";
            send(client_fd, res, (int)strlen(res), 0);
            shutdown(client_fd, SD_SEND);
            closesocket(client_fd);
            continue;
        }

        if (strcmp(method, "POST") == 0 && strcmp(path, "/api/reset") == 0) {
            ambulances[0].currentNode = 2;
            strcpy(ambulances[0].status, "Available");
            ambulances[1].currentNode = 7;
            strcpy(ambulances[1].status, "Available");

            sendJsonResponse(client_fd, 200, "OK", "{\"status\":\"success\",\"message\":\"All ambulances reset to starting stations.\"}");
            continue;
        }

        if (strcmp(method, "POST") == 0 && strcmp(path, "/api/dispatch") == 0) {
            char *body = strstr(buffer, "\r\n\r\n");
            int accidentNode = 6;
            bool isBroken = false;
            int brokenU = -1, brokenV = -1;

            if (body) {
                body += 4;
                char *accPtr = strstr(body, "\"accidentNode\"");
                if (accPtr) {
                    while (*accPtr && (*accPtr < '0' || *accPtr > '9')) accPtr++;
                    if (*accPtr >= '0' && *accPtr <= '9') accidentNode = atoi(accPtr);
                }
                char *brkPtr = strstr(body, "\"isBrokenRoad\"");
                if (brkPtr && strstr(brkPtr, "true")) {
                    isBroken = true;
                    char *uPtr = strstr(body, "\"brokenEdgeU\"");
                    char *vPtr = strstr(body, "\"brokenEdgeV\"");
                    if (uPtr) {
                        while (*uPtr && (*uPtr < '0' || *uPtr > '9')) uPtr++;
                        if (*uPtr >= '0' && *uPtr <= '9') brokenU = atoi(uPtr);
                    }
                    if (vPtr) {
                        while (*vPtr && (*vPtr < '0' || *vPtr > '9')) vPtr++;
                        if (*vPtr >= '0' && *vPtr <= '9') brokenV = atoi(vPtr);
                    }
                }
            }

            // Step A: Nearest Ambulance -> Accident
            int bestAmb = -1;
            int bestLeg1Eta = INF;
            int bestLeg1Parent[MAX_V];
            int bestLeg1Path[MAX_V];
            int leg1PathLen = 0;

            for (int i = 0; i < NUM_AMBULANCES; i++) {
                int dist[MAX_V];
                int parent[MAX_V];
                dijkstra(ambulances[i].currentNode, dist, parent, brokenU, brokenV);
                if (dist[accidentNode] < bestLeg1Eta) {
                    bestLeg1Eta = dist[accidentNode];
                    bestAmb = i;
                    memcpy(bestLeg1Parent, parent, sizeof(bestLeg1Parent));
                }
            }

            // Step B: Accident -> Nearest Hospital (0 or 10)
            int distFromAcc[MAX_V];
            int parentFromAcc[MAX_V];
            dijkstra(accidentNode, distFromAcc, parentFromAcc, brokenU, brokenV);

            int bestHospital = -1;
            int bestLeg2Eta = INF;

            for (int h = 0; h < NUM_HOSPITALS; h++) {
                int hospId = hospitalNodes[h];
                if (distFromAcc[hospId] < bestLeg2Eta) {
                    bestLeg2Eta = distFromAcc[hospId];
                    bestHospital = hospId;
                }
            }

            if (bestAmb != -1 && bestLeg1Eta < INF && bestHospital != -1 && bestLeg2Eta < INF) {
                leg1PathLen = reconstructPath(ambulances[bestAmb].currentNode, accidentNode, bestLeg1Parent, bestLeg1Path);
                int leg2Path[MAX_V];
                int leg2PathLen = reconstructPath(accidentNode, bestHospital, parentFromAcc, leg2Path);

                int fullPath[MAX_V * 2];
                int fullLen = 0;
                for (int k = 0; k < leg1PathLen; k++) fullPath[fullLen++] = bestLeg1Path[k];
                for (int k = 1; k < leg2PathLen; k++) fullPath[fullLen++] = leg2Path[k];

                int totalEta = bestLeg1Eta + bestLeg2Eta;

                char jsonBody[4096];
                int offset = snprintf(jsonBody, sizeof(jsonBody),
                    "{"
                    "\"status\":\"success\","
                    "\"ambulance\":{\"id\":%d,\"name\":\"%s\",\"stationNode\":%d,\"startNode\":%d},"
                    "\"accidentNode\":%d,"
                    "\"accidentName\":\"%s\","
                    "\"hospitalNode\":%d,"
                    "\"hospitalName\":\"%s\","
                    "\"isBrokenRoadActive\":%s,"
                    "\"alertMessage\":\"%s\","
                    "\"emergencyBanner\":\"🚨 EMERGENCY DISPATCHED: Accident at %s\","
                    "\"totalEtaMinutes\":%d,"
                    "\"leg1\":{\"from\":%d,\"to\":%d,\"eta\":%d,\"path\":[",
                    ambulances[bestAmb].id, ambulances[bestAmb].name, ambulances[bestAmb].stationNode, bestLeg1Path[0],
                    accidentNode, nodes[accidentNode].name,
                    bestHospital, nodes[bestHospital].name,
                    isBroken ? "true" : "false",
                    isBroken ? "Road alert: Broken segment detected! Alternate routing active." : "",
                    nodes[accidentNode].name,
                    totalEta,
                    bestLeg1Path[0], accidentNode, bestLeg1Eta
                );

                for (int k = 0; k < leg1PathLen; k++) {
                    offset += snprintf(jsonBody + offset, sizeof(jsonBody) - offset,
                        "%d%s", bestLeg1Path[k], (k == leg1PathLen - 1) ? "" : ",");
                }
                offset += snprintf(jsonBody + offset, sizeof(jsonBody) - offset, "]},\"leg2\":{\"from\":%d,\"to\":%d,\"eta\":%d,\"path\":[",
                    accidentNode, bestHospital, bestLeg2Eta);
                for (int k = 0; k < leg2PathLen; k++) {
                    offset += snprintf(jsonBody + offset, sizeof(jsonBody) - offset,
                        "%d%s", leg2Path[k], (k == leg2PathLen - 1) ? "" : ",");
                }
                offset += snprintf(jsonBody + offset, sizeof(jsonBody) - offset, "]},\"fullPath\":[");
                for (int k = 0; k < fullLen; k++) {
                    offset += snprintf(jsonBody + offset, sizeof(jsonBody) - offset,
                        "%d%s", fullPath[k], (k == fullLen - 1) ? "" : ",");
                }
                offset += snprintf(jsonBody + offset, sizeof(jsonBody) - offset, "]}");

                sendJsonResponse(client_fd, 200, "OK", jsonBody);
            } else {
                sendJsonResponse(client_fd, 400, "Bad Request", "{\"error\":\"No accessible route found due to road closures.\"}");
            }
            continue;
        }

        // Serve static files
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
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
