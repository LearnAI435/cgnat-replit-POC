#ifndef CGNAT_H
#define CGNAT_H

#include <stdint.h>
#include <netinet/in.h>
#include <time.h>

#define MAX_PUBLIC_IPS 10
#define MAX_CUSTOMERS 20000
#define PORT_RANGE_START 1024
#define PORT_RANGE_END 65535
#define TOTAL_PORTS_PER_IP (PORT_RANGE_END - PORT_RANGE_START + 1)
#define MAX_NAT_ENTRIES 50000

#define TCP_TIMEOUT 300
#define UDP_TIMEOUT 60

typedef enum {
    PROTO_TCP = 6,
    PROTO_UDP = 17
} protocol_t;

typedef enum {
    STATE_NEW = 0,
    STATE_ESTABLISHED,
    STATE_CLOSING,
    STATE_CLOSED
} conn_state_t;

typedef struct nat_entry {
    uint32_t priv_ip;
    uint16_t priv_port;
    uint32_t pub_ip;
    uint16_t pub_port;
    uint8_t protocol;
    conn_state_t state;
    time_t last_activity;
    uint8_t in_use;
    struct nat_entry *next;
} nat_entry_t;

typedef struct {
    uint32_t pub_ip;
    uint16_t port;
    uint8_t in_use;
} port_entry_t;

typedef struct {
    uint32_t public_ips[MAX_PUBLIC_IPS];
    int num_public_ips;
    
    port_entry_t port_pool[MAX_PUBLIC_IPS][TOTAL_PORTS_PER_IP];
    
    nat_entry_t nat_table[MAX_NAT_ENTRIES];
    int nat_entries_count;
    
    uint64_t stats_total_connections;
    uint64_t stats_active_connections;
    uint64_t stats_port_exhaustion_events;
    uint64_t stats_packets_translated;
} cgnat_t;

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint8_t protocol;
    size_t payload_len;
} packet_info_t;

cgnat_t* cgnat_init(void);
void cgnat_destroy(cgnat_t *cgnat);

int cgnat_add_public_ip(cgnat_t *cgnat, const char *ip_str);

int cgnat_translate_outbound(cgnat_t *cgnat, packet_info_t *pkt);
int cgnat_translate_inbound(cgnat_t *cgnat, packet_info_t *pkt);

void cgnat_cleanup_expired(cgnat_t *cgnat);
void cgnat_print_stats(cgnat_t *cgnat);

#endif
