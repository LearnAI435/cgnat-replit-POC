#include "cgnat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

cgnat_t* cgnat_init(void) {
    cgnat_t *cgnat = (cgnat_t*)calloc(1, sizeof(cgnat_t));
    if (!cgnat) {
        fprintf(stderr, "Failed to allocate CGNAT structure\n");
        return NULL;
    }
    
    cgnat->outbound_table = NULL;
    cgnat->inbound_table = NULL;
    cgnat->num_public_ips = 0;
    
    for (int i = 0; i < MAX_PUBLIC_IPS; i++) {
        for (int j = 0; j < TOTAL_PORTS_PER_IP; j++) {
            cgnat->port_pool[i][j].in_use = 0;
            cgnat->port_pool[i][j].port = PORT_RANGE_START + j;
        }
    }
    
    cgnat->stats_total_connections = 0;
    cgnat->stats_active_connections = 0;
    cgnat->stats_port_exhaustion_events = 0;
    cgnat->stats_packets_translated = 0;
    
    printf("[CGNAT] Initialized with support for %d customers\n", MAX_CUSTOMERS);
    return cgnat;
}

void cgnat_destroy(cgnat_t *cgnat) {
    if (!cgnat) return;
    
    nat_entry_outbound_t *out_curr, *out_tmp;
    HASH_ITER(hh_out, cgnat->outbound_table, out_curr, out_tmp) {
        HASH_DELETE(hh_out, cgnat->outbound_table, out_curr);
        free(out_curr);
    }
    
    nat_entry_inbound_t *in_curr, *in_tmp;
    HASH_ITER(hh_in, cgnat->inbound_table, in_curr, in_tmp) {
        HASH_DELETE(hh_in, cgnat->inbound_table, in_curr);
        free(in_tmp);
    }
    
    free(cgnat);
    printf("[CGNAT] Destroyed and cleaned up\n");
}

int cgnat_add_public_ip(cgnat_t *cgnat, const char *ip_str) {
    if (cgnat->num_public_ips >= MAX_PUBLIC_IPS) {
        fprintf(stderr, "[CGNAT] Cannot add more than %d public IPs\n", MAX_PUBLIC_IPS);
        return -1;
    }
    
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1) {
        fprintf(stderr, "[CGNAT] Invalid IP address: %s\n", ip_str);
        return -1;
    }
    
    uint32_t ip = ntohl(addr.s_addr);
    cgnat->public_ips[cgnat->num_public_ips] = ip;
    
    for (int j = 0; j < TOTAL_PORTS_PER_IP; j++) {
        cgnat->port_pool[cgnat->num_public_ips][j].pub_ip = ip;
    }
    
    printf("[CGNAT] Added public IP: %s (%d ports available)\n", ip_str, TOTAL_PORTS_PER_IP);
    cgnat->num_public_ips++;
    return 0;
}

static int allocate_port(cgnat_t *cgnat, uint32_t *pub_ip, uint16_t *pub_port) {
    static int last_ip_index = 0;
    
    for (int attempts = 0; attempts < MAX_PUBLIC_IPS; attempts++) {
        int ip_idx = (last_ip_index + attempts) % cgnat->num_public_ips;
        
        for (int port_idx = 0; port_idx < TOTAL_PORTS_PER_IP; port_idx++) {
            if (!cgnat->port_pool[ip_idx][port_idx].in_use) {
                cgnat->port_pool[ip_idx][port_idx].in_use = 1;
                *pub_ip = cgnat->port_pool[ip_idx][port_idx].pub_ip;
                *pub_port = cgnat->port_pool[ip_idx][port_idx].port;
                last_ip_index = (ip_idx + 1) % cgnat->num_public_ips;
                return 0;
            }
        }
    }
    
    cgnat->stats_port_exhaustion_events++;
    fprintf(stderr, "[CGNAT] Port exhaustion! All ports in use.\n");
    return -1;
}

static void release_port(cgnat_t *cgnat, uint32_t pub_ip, uint16_t pub_port) {
    for (int i = 0; i < cgnat->num_public_ips; i++) {
        if (cgnat->public_ips[i] == pub_ip) {
            int port_idx = pub_port - PORT_RANGE_START;
            if (port_idx >= 0 && port_idx < TOTAL_PORTS_PER_IP) {
                cgnat->port_pool[i][port_idx].in_use = 0;
            }
            return;
        }
    }
}

uint64_t make_outbound_key(uint32_t priv_ip, uint16_t priv_port, uint8_t protocol) {
    return ((uint64_t)priv_ip << 24) | ((uint64_t)priv_port << 8) | protocol;
}

uint64_t make_inbound_key(uint32_t pub_ip, uint16_t pub_port, uint8_t protocol) {
    return ((uint64_t)pub_ip << 24) | ((uint64_t)pub_port << 8) | protocol;
}

int cgnat_translate_outbound(cgnat_t *cgnat, packet_info_t *pkt) {
    if (cgnat->num_public_ips == 0) {
        fprintf(stderr, "[CGNAT] No public IPs configured\n");
        return -1;
    }
    
    uint64_t key = make_outbound_key(pkt->src_ip, pkt->src_port, pkt->protocol);
    nat_entry_outbound_t *entry = NULL;
    
    HASH_FIND(hh_out, cgnat->outbound_table, &key, sizeof(key), entry);
    
    if (entry) {
        entry->last_activity = time(NULL);
        pkt->src_ip = entry->pub_ip;
        pkt->src_port = entry->pub_port;
        cgnat->stats_packets_translated++;
        return 0;
    }
    
    entry = (nat_entry_outbound_t*)malloc(sizeof(nat_entry_outbound_t));
    if (!entry) {
        fprintf(stderr, "[CGNAT] Memory allocation failed\n");
        return -1;
    }
    
    entry->priv_ip = pkt->src_ip;
    entry->priv_port = pkt->src_port;
    entry->protocol = pkt->protocol;
    
    if (allocate_port(cgnat, &entry->pub_ip, &entry->pub_port) != 0) {
        free(entry);
        return -1;
    }
    
    entry->state = STATE_NEW;
    entry->last_activity = time(NULL);
    
    HASH_ADD(hh_out, cgnat->outbound_table, priv_ip, sizeof(key), entry);
    
    nat_entry_inbound_t *in_entry = (nat_entry_inbound_t*)malloc(sizeof(nat_entry_inbound_t));
    if (!in_entry) {
        fprintf(stderr, "[CGNAT] Memory allocation failed for inbound entry\n");
        release_port(cgnat, entry->pub_ip, entry->pub_port);
        HASH_DELETE(hh_out, cgnat->outbound_table, entry);
        free(entry);
        return -1;
    }
    
    in_entry->pub_ip = entry->pub_ip;
    in_entry->pub_port = entry->pub_port;
    in_entry->priv_ip = entry->priv_ip;
    in_entry->priv_port = entry->priv_port;
    in_entry->protocol = entry->protocol;
    in_entry->state = STATE_NEW;
    in_entry->last_activity = time(NULL);
    
    uint64_t in_key = make_inbound_key(in_entry->pub_ip, in_entry->pub_port, in_entry->protocol);
    HASH_ADD(hh_in, cgnat->inbound_table, pub_ip, sizeof(in_key), in_entry);
    
    pkt->src_ip = entry->pub_ip;
    pkt->src_port = entry->pub_port;
    
    cgnat->stats_total_connections++;
    cgnat->stats_active_connections++;
    cgnat->stats_packets_translated++;
    
    return 0;
}

int cgnat_translate_inbound(cgnat_t *cgnat, packet_info_t *pkt) {
    uint64_t key = make_inbound_key(pkt->dst_ip, pkt->dst_port, pkt->protocol);
    nat_entry_inbound_t *entry = NULL;
    
    HASH_FIND(hh_in, cgnat->inbound_table, &key, sizeof(key), entry);
    
    if (!entry) {
        return -1;
    }
    
    entry->last_activity = time(NULL);
    pkt->dst_ip = entry->priv_ip;
    pkt->dst_port = entry->priv_port;
    cgnat->stats_packets_translated++;
    
    return 0;
}

void cgnat_cleanup_expired(cgnat_t *cgnat) {
    time_t now = time(NULL);
    int cleaned = 0;
    
    nat_entry_outbound_t *out_curr, *out_tmp;
    HASH_ITER(hh_out, cgnat->outbound_table, out_curr, out_tmp) {
        int timeout = (out_curr->protocol == PROTO_TCP) ? TCP_TIMEOUT : UDP_TIMEOUT;
        
        if (now - out_curr->last_activity > timeout) {
            release_port(cgnat, out_curr->pub_ip, out_curr->pub_port);
            
            uint64_t in_key = make_inbound_key(out_curr->pub_ip, out_curr->pub_port, out_curr->protocol);
            nat_entry_inbound_t *in_entry = NULL;
            HASH_FIND(hh_in, cgnat->inbound_table, &in_key, sizeof(in_key), in_entry);
            if (in_entry) {
                HASH_DELETE(hh_in, cgnat->inbound_table, in_entry);
                free(in_entry);
            }
            
            HASH_DELETE(hh_out, cgnat->outbound_table, out_curr);
            free(out_curr);
            
            cgnat->stats_active_connections--;
            cleaned++;
        }
    }
    
    if (cleaned > 0) {
        printf("[CGNAT] Cleaned up %d expired connections\n", cleaned);
    }
}

void cgnat_print_stats(cgnat_t *cgnat) {
    printf("\n========== CGNAT Statistics ==========\n");
    printf("Public IPs configured: %d\n", cgnat->num_public_ips);
    printf("Total ports available: %d\n", cgnat->num_public_ips * TOTAL_PORTS_PER_IP);
    printf("Total connections (lifetime): %lu\n", cgnat->stats_total_connections);
    printf("Active connections: %lu\n", cgnat->stats_active_connections);
    printf("Packets translated: %lu\n", cgnat->stats_packets_translated);
    printf("Port exhaustion events: %lu\n", cgnat->stats_port_exhaustion_events);
    
    int ports_in_use = 0;
    for (int i = 0; i < cgnat->num_public_ips; i++) {
        for (int j = 0; j < TOTAL_PORTS_PER_IP; j++) {
            if (cgnat->port_pool[i][j].in_use) {
                ports_in_use++;
            }
        }
    }
    printf("Ports currently in use: %d\n", ports_in_use);
    
    if (cgnat->num_public_ips > 0) {
        double utilization = (double)ports_in_use / (cgnat->num_public_ips * TOTAL_PORTS_PER_IP) * 100.0;
        printf("Port pool utilization: %.2f%%\n", utilization);
    }
    printf("======================================\n\n");
}
