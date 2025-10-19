# CGNAT - Carrier-Grade NAT Implementation

A high-performance Carrier-Grade NAT (CGNAT) system implemented in C for managing 20,000 customers using only 10 public IP addresses.

## Features

- **Efficient Port Pooling**: Distributes 645,120 ports (10 IPs × 64,512 ports each) across customers
- **Bidirectional NAT Translation**: Fast lookup for both outbound and inbound traffic
- **Connection State Tracking**: Automatic timeout management for TCP (300s) and UDP (60s) sessions
- **Port Management**: Dynamic allocation with round-robin distribution across public IPs
- **Statistics & Monitoring**: Real-time tracking of connections, port usage, and performance metrics
- **Memory Efficient**: Supports up to 50,000 concurrent NAT table entries

## Architecture

### Core Components

1. **NAT Translation Table**
   - Array-based storage for fast lookups
   - Bidirectional mapping (private ↔ public IP:port pairs)
   - Indexed by connection parameters for O(n) average case lookup

2. **Port Pool Management**
   - 2D array structure: [10 public IPs][64,512 ports each]
   - Round-robin allocation across IPs for load distribution
   - Automatic port recycling after connection timeout

3. **Connection State Tracking**
   - Per-connection state (NEW, ESTABLISHED, CLOSING, CLOSED)
   - Protocol-aware timeouts (TCP vs UDP)
   - Last activity timestamp for cleanup

4. **Packet Processing Pipeline**
   - Outbound: Translates customer IP:port → public IP:port
   - Inbound: Reverse translation for return traffic

## Building

```bash
make
```

## Running

```bash
./cgnat
```

The program will:
1. Initialize the CGNAT system
2. Configure 10 public IP addresses (203.0.113.1-10)
3. Run traffic simulation demonstrations
4. Enter interactive mode for testing

## Interactive Commands

- `stats` - Display system statistics
- `sim` - Simulate customer traffic
- `pool` - Demonstrate port pooling (100 concurrent connections)
- `cleanup` - Clean expired connections
- `quit` - Exit the program

## Configuration

Edit `main.c` to customize:
- Public IP addresses
- Private subnet ranges
- Timeout values (TCP_TIMEOUT, UDP_TIMEOUT in cgnat.h)
- Maximum NAT entries (MAX_NAT_ENTRIES in cgnat.h)

## System Capacity

- **Public IPs**: 10 (configurable, max 10)
- **Total Ports**: 645,120 (64,512 usable ports per IP)
- **Max Customers**: 20,000 simultaneous connections
- **NAT Table**: 50,000 entries
- **Port Pool Utilization**: Tracks usage percentage

## Example Output

```
[CGNAT] System ready!
[CGNAT] Total port capacity: 645120 ports
[CGNAT] Can support simultaneous connections from 20000 customers

Packet 1 (before NAT): [OUT] 10.0.0.1:40000 -> 8.8.8.8:80 (TCP)
Packet 1 (after NAT):  [OUT] 203.0.113.1:1024 -> 8.8.8.8:80 (TCP)

========== CGNAT Statistics ==========
Public IPs configured: 10
Total ports available: 645120
Total connections (lifetime): 115
Active connections: 115
Packets translated: 120
Port pool utilization: 0.02%
```

## Technical Details

### NAT Translation Process

**Outbound (Customer → Internet)**:
1. Check if mapping exists for (private_ip, private_port, protocol)
2. If exists: reuse mapping, update last_activity
3. If new: allocate port from pool, create NAT entry
4. Rewrite packet source to (public_ip, public_port)

**Inbound (Internet → Customer)**:
1. Lookup (public_ip, public_port, protocol) in NAT table
2. If found: rewrite packet destination to (private_ip, private_port)
3. If not found: drop packet (no mapping)

### Port Allocation Strategy

- Round-robin across 10 public IPs for load distribution
- Sequential port allocation within each IP
- Automatic port release after timeout
- Port exhaustion detection and reporting

### Connection Cleanup

- Periodic scanning of NAT table
- Timeout-based expiration (300s TCP, 60s UDP)
- Automatic port and NAT entry recycling

## Use Cases

- ISP carrier-grade NAT for IPv4 address conservation
- Testing NAT behavior and port exhaustion scenarios
- Educational demonstration of CGNAT principles
- Network simulation and protocol testing

## Performance Characteristics

- **Translation Lookup**: O(n) average case (linear scan)
- **Port Allocation**: O(1) amortized (round-robin with index)
- **Memory Usage**: ~10 MB for full NAT table + port pools
- **Throughput**: Depends on CPU; single-threaded design

## Future Enhancements

- Hash-based NAT table for O(1) lookups
- Multi-threaded packet processing
- Protocol-specific ALGs (FTP, SIP, etc.)
- Connection persistence and session affinity
- Performance monitoring dashboard
- Dynamic timeout adjustment

## License

Open source implementation for educational purposes.
