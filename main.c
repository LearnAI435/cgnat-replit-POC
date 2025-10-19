#include "cgnat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

void print_packet_info(const char *direction, packet_info_t *pkt) {
    struct in_addr src_addr, dst_addr;
    src_addr.s_addr = htonl(pkt->src_ip);
    dst_addr.s_addr = htonl(pkt->dst_ip);
    
    char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr, src_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &dst_addr, dst_str, INET_ADDRSTRLEN);
    
    printf("[%s] %s:%d -> %s:%d (%s)\n",
           direction, src_str, pkt->src_port, dst_str, pkt->dst_port,
           pkt->protocol == PROTO_TCP ? "TCP" : "UDP");
}

uint32_t parse_ip(const char *ip_str) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1) {
        return 0;
    }
    return ntohl(addr.s_addr);
}

void simulate_customer_traffic(cgnat_t *cgnat) {
    printf("\n========== Simulating Customer Traffic ==========\n\n");
    
    packet_info_t packets[20];
    int num_packets = 0;
    
    for (int i = 0; i < 10; i++) {
        char customer_ip[32];
        snprintf(customer_ip, sizeof(customer_ip), "10.0.%d.%d", i / 256, i % 256);
        
        packets[num_packets].src_ip = parse_ip(customer_ip);
        packets[num_packets].src_port = 40000 + i;
        packets[num_packets].dst_ip = parse_ip("8.8.8.8");
        packets[num_packets].dst_port = 80;
        packets[num_packets].protocol = PROTO_TCP;
        packets[num_packets].payload_len = 100;
        num_packets++;
        
        if (i < 5) {
            packets[num_packets].src_ip = parse_ip(customer_ip);
            packets[num_packets].src_port = 50000 + i;
            packets[num_packets].dst_ip = parse_ip("1.1.1.1");
            packets[num_packets].dst_port = 53;
            packets[num_packets].protocol = PROTO_UDP;
            packets[num_packets].payload_len = 64;
            num_packets++;
        }
    }
    
    printf("--- Outbound Traffic (Customer -> Internet) ---\n");
    for (int i = 0; i < num_packets; i++) {
        printf("\nPacket %d (before NAT): ", i + 1);
        print_packet_info("OUT", &packets[i]);
        
        if (cgnat_translate_outbound(cgnat, &packets[i]) == 0) {
            printf("Packet %d (after NAT):  ", i + 1);
            print_packet_info("OUT", &packets[i]);
        } else {
            printf("  ERROR: Translation failed!\n");
        }
    }
    
    printf("\n\n--- Inbound Traffic (Internet -> Customer) ---\n");
    for (int i = 0; i < 5; i++) {
        packet_info_t response;
        response.src_ip = packets[i].dst_ip;
        response.src_port = packets[i].dst_port;
        response.dst_ip = packets[i].src_ip;
        response.dst_port = packets[i].src_port;
        response.protocol = packets[i].protocol;
        response.payload_len = 200;
        
        printf("\nResponse %d (before NAT): ", i + 1);
        print_packet_info("IN", &response);
        
        if (cgnat_translate_inbound(cgnat, &response) == 0) {
            printf("Response %d (after NAT):  ", i + 1);
            print_packet_info("IN", &response);
        } else {
            printf("  ERROR: Translation failed (no mapping found)!\n");
        }
    }
    
    printf("\n========== Simulation Complete ==========\n");
}

void demonstrate_port_pooling(cgnat_t *cgnat) {
    printf("\n========== Port Pooling Demonstration ==========\n\n");
    
    printf("Creating 100 concurrent connections from different customers...\n");
    
    for (int i = 0; i < 100; i++) {
        char customer_ip[32];
        snprintf(customer_ip, sizeof(customer_ip), "10.1.%d.%d", i / 256, i % 256 + 1);
        
        packet_info_t pkt;
        pkt.src_ip = parse_ip(customer_ip);
        pkt.src_port = 35000 + (i % 1000);
        pkt.dst_ip = parse_ip("93.184.216.34");
        pkt.dst_port = 443;
        pkt.protocol = PROTO_TCP;
        pkt.payload_len = 128;
        
        if (cgnat_translate_outbound(cgnat, &pkt) == 0) {
            if (i < 5 || i >= 95) {
                struct in_addr orig, translated;
                orig.s_addr = htonl(parse_ip(customer_ip));
                translated.s_addr = htonl(pkt.src_ip);
                
                char orig_str[INET_ADDRSTRLEN], trans_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &orig, orig_str, INET_ADDRSTRLEN);
                inet_ntop(AF_INET, &translated, trans_str, INET_ADDRSTRLEN);
                
                printf("  Customer %s:%d -> NAT %s:%d\n",
                       orig_str, 35000 + (i % 1000),
                       trans_str, pkt.src_port);
            } else if (i == 5) {
                printf("  ... (90 more connections) ...\n");
            }
        }
    }
    
    printf("\n========== Port Pooling Complete ==========\n");
}

void run_interactive_mode(cgnat_t *cgnat) {
    printf("\n========== CGNAT Interactive Mode ==========\n");
    printf("Commands:\n");
    printf("  stats     - Show statistics\n");
    printf("  sim       - Simulate traffic\n");
    printf("  pool      - Demonstrate port pooling\n");
    printf("  cleanup   - Clean expired connections\n");
    printf("  quit      - Exit\n");
    printf("===========================================\n\n");
    
    char command[64];
    while (1) {
        printf("cgnat> ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        command[strcspn(command, "\n")] = 0;
        
        if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        } else if (strcmp(command, "stats") == 0) {
            cgnat_print_stats(cgnat);
        } else if (strcmp(command, "sim") == 0) {
            simulate_customer_traffic(cgnat);
        } else if (strcmp(command, "pool") == 0) {
            demonstrate_port_pooling(cgnat);
        } else if (strcmp(command, "cleanup") == 0) {
            cgnat_cleanup_expired(cgnat);
        } else if (strlen(command) > 0) {
            printf("Unknown command: %s\n", command);
        }
    }
}

int main(void) {
    printf("===========================================\n");
    printf("  CGNAT - Carrier Grade NAT System\n");
    printf("  Managing 20K customers with 10 Public IPs\n");
    printf("===========================================\n\n");
    
    cgnat_t *cgnat = cgnat_init();
    if (!cgnat) {
        fprintf(stderr, "Failed to initialize CGNAT\n");
        return 1;
    }
    
    printf("Configuring public IP pool...\n");
    cgnat_add_public_ip(cgnat, "203.0.113.1");
    cgnat_add_public_ip(cgnat, "203.0.113.2");
    cgnat_add_public_ip(cgnat, "203.0.113.3");
    cgnat_add_public_ip(cgnat, "203.0.113.4");
    cgnat_add_public_ip(cgnat, "203.0.113.5");
    cgnat_add_public_ip(cgnat, "203.0.113.6");
    cgnat_add_public_ip(cgnat, "203.0.113.7");
    cgnat_add_public_ip(cgnat, "203.0.113.8");
    cgnat_add_public_ip(cgnat, "203.0.113.9");
    cgnat_add_public_ip(cgnat, "203.0.113.10");
    
    printf("\n[CGNAT] System ready!\n");
    printf("[CGNAT] Total port capacity: %d ports\n", 10 * TOTAL_PORTS_PER_IP);
    printf("[CGNAT] Can support simultaneous connections from %d customers\n\n", MAX_CUSTOMERS);
    
    simulate_customer_traffic(cgnat);
    cgnat_print_stats(cgnat);
    
    demonstrate_port_pooling(cgnat);
    cgnat_print_stats(cgnat);
    
    run_interactive_mode(cgnat);
    
    cgnat_print_stats(cgnat);
    cgnat_destroy(cgnat);
    
    printf("\n[CGNAT] System shutdown complete\n");
    return 0;
}
