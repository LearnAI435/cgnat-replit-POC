#include "cgnat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#define PORT 5000
#define BUFFER_SIZE 65536

static cgnat_t *global_cgnat = NULL;
static volatile int server_running = 1;

void signal_handler(int sig) {
    (void)sig;
    server_running = 0;
}

void send_http_response(int client_socket, const char *status, const char *content_type, const char *body) {
    char header[1024];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, strlen(body));
    
    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, strlen(body), 0);
}

void serve_dashboard(int client_socket) {
    FILE *fp = fopen("dashboard.html", "r");
    if (!fp) {
        const char *error = "<html><body><h1>Dashboard not found</h1></body></html>";
        send_http_response(client_socket, "404 Not Found", "text/html", error);
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(fsize + 1);
    fread(content, 1, fsize, fp);
    content[fsize] = 0;
    fclose(fp);
    
    send_http_response(client_socket, "200 OK", "text/html; charset=utf-8", content);
    free(content);
}

void get_ip_pool_stats(cgnat_t *cgnat, int *ports_per_ip) {
    for (int i = 0; i < cgnat->num_public_ips; i++) {
        ports_per_ip[i] = 0;
        for (int j = 0; j < TOTAL_PORTS_PER_IP; j++) {
            if (cgnat->port_pool[i][j].in_use) {
                ports_per_ip[i]++;
            }
        }
    }
}

void get_connection_states(cgnat_t *cgnat, int *state_counts) {
    memset(state_counts, 0, 8 * sizeof(int));
    for (int i = 0; i < MAX_NAT_ENTRIES; i++) {
        if (cgnat->nat_table[i].in_use) {
            state_counts[cgnat->nat_table[i].state]++;
        }
    }
}

void serve_api_stats(int client_socket) {
    char json[BUFFER_SIZE];
    char *ptr = json;
    int remaining = BUFFER_SIZE;
    
    pthread_mutex_lock(&global_cgnat->lock);
    
    int ports_per_ip[MAX_PUBLIC_IPS] = {0};
    get_ip_pool_stats(global_cgnat, ports_per_ip);
    
    int state_counts[8] = {0};
    get_connection_states(global_cgnat, state_counts);
    
    int total_ports = global_cgnat->num_public_ips * TOTAL_PORTS_PER_IP;
    int ports_in_use = 0;
    for (int i = 0; i < global_cgnat->num_public_ips; i++) {
        ports_in_use += ports_per_ip[i];
    }
    
    int written = snprintf(ptr, remaining, "{\n");
    ptr += written; remaining -= written;
    
    written = snprintf(ptr, remaining,
        "  \"timestamp\": %ld,\n"
        "  \"num_public_ips\": %d,\n"
        "  \"total_ports\": %d,\n"
        "  \"ports_in_use\": %d,\n"
        "  \"ports_available\": %d,\n"
        "  \"port_utilization\": %.2f,\n"
        "  \"total_connections\": %lu,\n"
        "  \"active_connections\": %lu,\n"
        "  \"packets_translated\": %lu,\n"
        "  \"port_exhaustion_events\": %lu,\n"
        "  \"nat_table_entries\": %d,\n"
        "  \"nat_table_capacity\": %d,\n"
        "  \"nat_table_utilization\": %.2f,\n",
        time(NULL),
        global_cgnat->num_public_ips,
        total_ports,
        ports_in_use,
        total_ports - ports_in_use,
        total_ports > 0 ? (double)ports_in_use / total_ports * 100.0 : 0.0,
        global_cgnat->stats_total_connections,
        global_cgnat->stats_active_connections,
        global_cgnat->stats_packets_translated,
        global_cgnat->stats_port_exhaustion_events,
        global_cgnat->nat_entries_count,
        MAX_NAT_ENTRIES,
        (double)global_cgnat->nat_entries_count / MAX_NAT_ENTRIES * 100.0
    );
    ptr += written; remaining -= written;
    
    written = snprintf(ptr, remaining, "  \"public_ips\": [\n");
    ptr += written; remaining -= written;
    
    for (int i = 0; i < global_cgnat->num_public_ips; i++) {
        struct in_addr addr;
        addr.s_addr = htonl(global_cgnat->public_ips[i]);
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
        
        written = snprintf(ptr, remaining,
            "    {\"ip\": \"%s\", \"ports_used\": %d, \"ports_available\": %d}%s\n",
            ip_str, ports_per_ip[i], TOTAL_PORTS_PER_IP - ports_per_ip[i],
            i < global_cgnat->num_public_ips - 1 ? "," : ""
        );
        ptr += written; remaining -= written;
    }
    
    written = snprintf(ptr, remaining, "  ],\n  \"connection_states\": {\n");
    ptr += written; remaining -= written;
    
    written = snprintf(ptr, remaining,
        "    \"closed\": %d,\n"
        "    \"syn_sent\": %d,\n"
        "    \"syn_received\": %d,\n"
        "    \"established\": %d,\n"
        "    \"fin_wait\": %d,\n"
        "    \"closing\": %d,\n"
        "    \"time_wait\": %d,\n"
        "    \"udp_active\": %d\n"
        "  }\n",
        state_counts[0], state_counts[1], state_counts[2], state_counts[3],
        state_counts[4], state_counts[5], state_counts[6], state_counts[7]
    );
    ptr += written; remaining -= written;
    
    written = snprintf(ptr, remaining, "}\n");
    
    pthread_mutex_unlock(&global_cgnat->lock);
    
    send_http_response(client_socket, "200 OK", "application/json", json);
}

void serve_api_connections(int client_socket) {
    char json[BUFFER_SIZE];
    char *ptr = json;
    int remaining = BUFFER_SIZE;
    
    pthread_mutex_lock(&global_cgnat->lock);
    
    int written = snprintf(ptr, remaining, "{\n  \"connections\": [\n");
    ptr += written; remaining -= written;
    
    int count = 0;
    int first = 1;
    for (int i = 0; i < MAX_NAT_ENTRIES && count < 100; i++) {
        if (global_cgnat->nat_table[i].in_use) {
            struct in_addr priv_addr, pub_addr;
            priv_addr.s_addr = htonl(global_cgnat->nat_table[i].priv_ip);
            pub_addr.s_addr = htonl(global_cgnat->nat_table[i].pub_ip);
            
            char priv_ip[INET_ADDRSTRLEN], pub_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &priv_addr, priv_ip, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &pub_addr, pub_ip, INET_ADDRSTRLEN);
            
            const char *proto = (global_cgnat->nat_table[i].protocol == PROTO_TCP) ? "TCP" : "UDP";
            const char *states[] = {"CLOSED", "SYN_SENT", "SYN_RECV", "ESTABLISHED", 
                                   "FIN_WAIT", "CLOSING", "TIME_WAIT", "UDP_ACTIVE"};
            
            written = snprintf(ptr, remaining,
                "    %s{\"priv_ip\": \"%s\", \"priv_port\": %u, "
                "\"pub_ip\": \"%s\", \"pub_port\": %u, "
                "\"protocol\": \"%s\", \"state\": \"%s\", "
                "\"age\": %ld}\n",
                first ? "" : ",",
                priv_ip, global_cgnat->nat_table[i].priv_port,
                pub_ip, global_cgnat->nat_table[i].pub_port,
                proto, states[global_cgnat->nat_table[i].state],
                time(NULL) - global_cgnat->nat_table[i].last_activity
            );
            ptr += written; remaining -= written;
            
            count++;
            first = 0;
        }
    }
    
    written = snprintf(ptr, remaining, "  ],\n  \"total\": %d,\n  \"showing\": %d\n}\n",
        global_cgnat->nat_entries_count, count);
    
    pthread_mutex_unlock(&global_cgnat->lock);
    
    send_http_response(client_socket, "200 OK", "application/json", json);
}

void handle_client(int client_socket) {
    char buffer[4096];
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    if (strncmp(buffer, "GET / ", 6) == 0 || strncmp(buffer, "GET /index.html", 15) == 0) {
        serve_dashboard(client_socket);
    } else if (strncmp(buffer, "GET /api/stats", 14) == 0) {
        serve_api_stats(client_socket);
    } else if (strncmp(buffer, "GET /api/connections", 20) == 0) {
        serve_api_connections(client_socket);
    } else {
        const char *msg = "{\"error\": \"Not found\"}";
        send_http_response(client_socket, "404 Not Found", "application/json", msg);
    }
    
    close(client_socket);
}

void* traffic_simulator(void *arg) {
    (void)arg;
    int customer_id = 1;
    
    while (server_running) {
        sleep(5);
        
        for (int i = 0; i < 10; i++) {
            packet_info_t pkt = {
                .src_ip = 0x0A000000 | (customer_id % 65536),
                .src_port = 10000 + (customer_id % 50000),
                .dst_ip = 0x08080808,
                .dst_port = 80,
                .protocol = (customer_id % 2 == 0) ? PROTO_TCP : PROTO_UDP,
                .payload_len = 1024
            };
            
            cgnat_translate_outbound(global_cgnat, &pkt);
            customer_id++;
            
            if (customer_id % 100 == 0) {
                cgnat_cleanup_expired(global_cgnat);
            }
        }
    }
    return NULL;
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("[WEB] Initializing CGNAT system...\n");
    global_cgnat = cgnat_init();
    if (!global_cgnat) {
        fprintf(stderr, "[WEB] Failed to initialize CGNAT\n");
        return 1;
    }
    
    for (int i = 1; i <= 10; i++) {
        char ip[20];
        snprintf(ip, sizeof(ip), "203.0.113.%d", i);
        cgnat_add_public_ip(global_cgnat, ip);
    }
    
    pthread_t sim_thread;
    pthread_create(&sim_thread, NULL, traffic_simulator, NULL);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[WEB] Socket creation failed");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[WEB] Bind failed");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("[WEB] Listen failed");
        close(server_fd);
        return 1;
    }
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║        CGNAT Web Dashboard Started                    ║\n");
    printf("║                                                        ║\n");
    printf("║  Dashboard: http://0.0.0.0:5000                       ║\n");
    printf("║  API Stats: http://0.0.0.0:5000/api/stats             ║\n");
    printf("║  Traffic simulation running in background...          ║\n");
    printf("║                                                        ║\n");
    printf("║  Press Ctrl+C to stop                                 ║\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("\n");
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            if (server_running) {
                perror("[WEB] Accept failed");
            }
            continue;
        }
        
        handle_client(client_socket);
    }
    
    printf("\n[WEB] Shutting down...\n");
    pthread_join(sim_thread, NULL);
    close(server_fd);
    cgnat_destroy(global_cgnat);
    
    return 0;
}
