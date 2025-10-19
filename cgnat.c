#include "cgnat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

static uint32_t hash_outbound(uint32_t priv_ip, uint16_t priv_port, uint8_t protocol) {
    uint64_t key = ((uint64_t)priv_ip << 24) | ((uint64_t)priv_port << 8) | protocol;
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return (uint32_t)(key & (HASH_TABLE_SIZE - 1));
}

static uint32_t hash_inbound(uint32_t pub_ip, uint16_t pub_port, uint8_t protocol) {
    uint64_t key = ((uint64_t)pub_ip << 24) | ((uint64_t)pub_port << 8) | protocol;
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return (uint32_t)(key & (HASH_TABLE_SIZE - 1));
}

cgnat_t* cgnat_init(void) {
    cgnat_t *cgnat = (cgnat_t*)calloc(1, sizeof(cgnat_t));
    if (!cgnat) {
        fprintf(stderr, "Failed to allocate CGNAT structure\n");
        return NULL;
    }
    
    cgnat->num_public_ips = 0;
    cgnat->nat_entries_count = 0;
    cgnat->next_free_entry = 0;
    
    for (int i = 0; i < MAX_NAT_ENTRIES; i++) {
        cgnat->nat_table[i].in_use = 0;
        cgnat->nat_table[i].next_outbound = NULL;
        cgnat->nat_table[i].next_inbound = NULL;
    }
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        cgnat->outbound_hash[i].head = NULL;
        cgnat->inbound_hash[i].head = NULL;
    }
    
    for (int i = 0; i < MAX_PUBLIC_IPS; i++) {
        cgnat->next_port_index[i] = 0;
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
    
    for (int attempt = 0; attempt < MAX_PUBLIC_IPS; attempt++) {
        int ip_idx = (last_ip_index + attempt) % cgnat->num_public_ips;
        int start_port_idx = cgnat->next_port_index[ip_idx];
        
        for (int i = 0; i < TOTAL_PORTS_PER_IP; i++) {
            int port_idx = (start_port_idx + i) % TOTAL_PORTS_PER_IP;
            
            if (!cgnat->port_pool[ip_idx][port_idx].in_use) {
                cgnat->port_pool[ip_idx][port_idx].in_use = 1;
                *pub_ip = cgnat->port_pool[ip_idx][port_idx].pub_ip;
                *pub_port = cgnat->port_pool[ip_idx][port_idx].port;
                
                cgnat->next_port_index[ip_idx] = (port_idx + 1) % TOTAL_PORTS_PER_IP;
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

static nat_entry_t* find_outbound_entry(cgnat_t *cgnat, uint32_t priv_ip, uint16_t priv_port, uint8_t protocol) {
    uint32_t hash = hash_outbound(priv_ip, priv_port, protocol);
    nat_entry_t *entry = cgnat->outbound_hash[hash].head;
    
    while (entry) {
        if (entry->in_use &&
            entry->priv_ip == priv_ip &&
            entry->priv_port == priv_port &&
            entry->protocol == protocol) {
            return entry;
        }
        entry = entry->next_outbound;
    }
    return NULL;
}

static nat_entry_t* find_inbound_entry(cgnat_t *cgnat, uint32_t pub_ip, uint16_t pub_port, uint8_t protocol) {
    uint32_t hash = hash_inbound(pub_ip, pub_port, protocol);
    nat_entry_t *entry = cgnat->inbound_hash[hash].head;
    
    while (entry) {
        if (entry->in_use &&
            entry->pub_ip == pub_ip &&
            entry->pub_port == pub_port &&
            entry->protocol == protocol) {
            return entry;
        }
        entry = entry->next_inbound;
    }
    return NULL;
}

static nat_entry_t* allocate_nat_entry(cgnat_t *cgnat) {
    for (int i = 0; i < MAX_NAT_ENTRIES; i++) {
        int idx = (cgnat->next_free_entry + i) % MAX_NAT_ENTRIES;
        if (!cgnat->nat_table[idx].in_use) {
            cgnat->nat_table[idx].in_use = 1;
            cgnat->nat_table[idx].next_outbound = NULL;
            cgnat->nat_table[idx].next_inbound = NULL;
            cgnat->nat_entries_count++;
            cgnat->next_free_entry = (idx + 1) % MAX_NAT_ENTRIES;
            return &cgnat->nat_table[idx];
        }
    }
    fprintf(stderr, "[CGNAT] NAT table full! Cannot create new entry.\n");
    return NULL;
}

static void add_to_hash_tables(cgnat_t *cgnat, nat_entry_t *entry) {
    uint32_t out_hash = hash_outbound(entry->priv_ip, entry->priv_port, entry->protocol);
    entry->next_outbound = cgnat->outbound_hash[out_hash].head;
    cgnat->outbound_hash[out_hash].head = entry;
}

static void add_to_inbound_hash(cgnat_t *cgnat, nat_entry_t *entry) {
    uint32_t in_hash = hash_inbound(entry->pub_ip, entry->pub_port, entry->protocol);
    entry->next_inbound = cgnat->inbound_hash[in_hash].head;
    cgnat->inbound_hash[in_hash].head = entry;
}

static void remove_from_hash_tables(cgnat_t *cgnat, nat_entry_t *entry) {
    uint32_t out_hash = hash_outbound(entry->priv_ip, entry->priv_port, entry->protocol);
    nat_entry_t **curr = &cgnat->outbound_hash[out_hash].head;
    while (*curr) {
        if (*curr == entry) {
            *curr = entry->next_outbound;
            break;
        }
        curr = &((*curr)->next_outbound);
    }
    
    uint32_t in_hash = hash_inbound(entry->pub_ip, entry->pub_port, entry->protocol);
    curr = &cgnat->inbound_hash[in_hash].head;
    while (*curr) {
        if (*curr == entry) {
            *curr = entry->next_inbound;
            break;
        }
        curr = &((*curr)->next_inbound);
    }
}

static void update_tcp_state(nat_entry_t *entry, packet_info_t *pkt) {
    (void)pkt;
    
    switch (entry->state) {
        case STATE_CLOSED:
            entry->state = STATE_SYN_SENT;
            break;
        case STATE_SYN_SENT:
        case STATE_SYN_RECEIVED:
            entry->state = STATE_ESTABLISHED;
            break;
        case STATE_ESTABLISHED:
            break;
        case STATE_FIN_WAIT:
            entry->state = STATE_CLOSING;
            break;
        case STATE_CLOSING:
            entry->state = STATE_TIME_WAIT;
            break;
        case STATE_TIME_WAIT:
            entry->state = STATE_CLOSED;
            break;
        default:
            break;
    }
}

int cgnat_translate_outbound(cgnat_t *cgnat, packet_info_t *pkt) {
    if (cgnat->num_public_ips == 0) {
        fprintf(stderr, "[CGNAT] No public IPs configured\n");
        return -1;
    }
    
    nat_entry_t *entry = find_outbound_entry(cgnat, pkt->src_ip, pkt->src_port, pkt->protocol);
    
    if (entry) {
        entry->last_activity = time(NULL);
        
        if (pkt->protocol == PROTO_TCP) {
            update_tcp_state(entry, pkt);
        }
        
        pkt->src_ip = entry->pub_ip;
        pkt->src_port = entry->pub_port;
        cgnat->stats_packets_translated++;
        return 0;
    }
    
    entry = allocate_nat_entry(cgnat);
    if (!entry) {
        return -1;
    }
    
    entry->priv_ip = pkt->src_ip;
    entry->priv_port = pkt->src_port;
    entry->protocol = pkt->protocol;
    
    if (allocate_port(cgnat, &entry->pub_ip, &entry->pub_port) != 0) {
        entry->in_use = 0;
        cgnat->nat_entries_count--;
        return -1;
    }
    
    entry->state = (pkt->protocol == PROTO_TCP) ? STATE_SYN_SENT : STATE_UDP_ACTIVE;
    entry->last_activity = time(NULL);
    
    add_to_hash_tables(cgnat, entry);
    add_to_inbound_hash(cgnat, entry);
    
    pkt->src_ip = entry->pub_ip;
    pkt->src_port = entry->pub_port;
    
    cgnat->stats_total_connections++;
    cgnat->stats_active_connections++;
    cgnat->stats_packets_translated++;
    
    return 0;
}

int cgnat_translate_inbound(cgnat_t *cgnat, packet_info_t *pkt) {
    nat_entry_t *entry = find_inbound_entry(cgnat, pkt->dst_ip, pkt->dst_port, pkt->protocol);
    
    if (!entry) {
        return -1;
    }
    
    entry->last_activity = time(NULL);
    
    if (pkt->protocol == PROTO_TCP) {
        update_tcp_state(entry, pkt);
    }
    
    pkt->dst_ip = entry->priv_ip;
    pkt->dst_port = entry->priv_port;
    cgnat->stats_packets_translated++;
    
    return 0;
}

void cgnat_cleanup_expired(cgnat_t *cgnat) {
    time_t now = time(NULL);
    int cleaned = 0;
    
    for (int i = 0; i < MAX_NAT_ENTRIES; i++) {
        if (cgnat->nat_table[i].in_use) {
            int timeout = (cgnat->nat_table[i].protocol == PROTO_TCP) ? TCP_TIMEOUT : UDP_TIMEOUT;
            
            if (cgnat->nat_table[i].state == STATE_CLOSED || 
                cgnat->nat_table[i].state == STATE_TIME_WAIT ||
                (now - cgnat->nat_table[i].last_activity > timeout)) {
                
                remove_from_hash_tables(cgnat, &cgnat->nat_table[i]);
                release_port(cgnat, cgnat->nat_table[i].pub_ip, cgnat->nat_table[i].pub_port);
                cgnat->nat_table[i].in_use = 0;
                cgnat->nat_entries_count--;
                cgnat->stats_active_connections--;
                cleaned++;
            }
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
    printf("NAT table entries: %d / %d\n", cgnat->nat_entries_count, MAX_NAT_ENTRIES);
    
    if (cgnat->num_public_ips > 0) {
        double utilization = (double)ports_in_use / (cgnat->num_public_ips * TOTAL_PORTS_PER_IP) * 100.0;
        printf("Port pool utilization: %.2f%%\n", utilization);
    }
    printf("======================================\n\n");
}
