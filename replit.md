# CGNAT Implementation in C

## Overview
A Carrier-Grade NAT (CGNAT) system implemented in C for managing 20,000 customers using only 10 public IP addresses. The system implements efficient NAT translation tables with hash-based lookups, optimized port pool management, and connection state tracking with TCP/UDP state machines.

## Recent Changes
- **2025-10-19**: Initial project setup with core CGNAT implementation
- **2025-10-19**: Implemented hash table-based NAT translation (O(1) lookups)
- **2025-10-19**: Added optimized port pool allocator with rotating cursor
- **2025-10-19**: Implemented TCP/UDP state transition tracking
- **2025-10-19**: Fixed dual-linkage hash table (separate next_outbound/next_inbound pointers)
- **2025-10-19**: Successfully stress tested with 20K connections (5.4M conn/sec, 5.6M pkt/sec)
- **2025-10-19**: Added web-based monitoring dashboard with real-time statistics
- **2025-10-19**: Implemented HTTP API server with JSON endpoints
- **2025-10-19**: Added thread safety with pthread mutexes for concurrent access

## Project Architecture

### Core Components

1. **Hash Table NAT Translation**
   - Dual hash tables: outbound (private→public) and inbound (public→private)
   - 65,536 buckets for fast O(1) average-case lookups
   - Separate linkage pointers (next_outbound, next_inbound) prevent hash chain corruption
   - Supports 50,000 concurrent NAT entries

2. **Port Pool Management**
   - Distributes 645,120 ports (10 IPs × 64,512 usable ports each)
   - Round-robin allocation across public IPs for load balancing
   - Rotating cursor per IP for O(1) amortized port allocation
   - Automatic port recycling after connection timeout

3. **Connection State Tracking**
   - TCP state machine: CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT → CLOSING → TIME_WAIT
   - UDP state tracking: UDP_ACTIVE with idle timeout
   - Protocol-aware timeouts (TCP: 300s, UDP: 60s)
   - Automatic cleanup of expired connections

4. **Packet Processing Pipeline**
   - Outbound NAT: Hash lookup → allocate port if new → rewrite source IP:port
   - Inbound NAT: Hash lookup → rewrite destination IP:port
   - State updates on each packet translation
   - Statistics tracking for monitoring

### Key Design Decisions

- **Hash Function**: Custom integer hash function for uniform distribution
- **Memory Layout**: Fixed-size arrays for predictable memory usage (~10 MB)
- **Dual Linkage**: Each NAT entry maintains two separate hash chain pointers
- **Port Allocation**: Rotating cursor avoids full scans even under high utilization
- **State Management**: Proper TCP/UDP state transitions for connection lifecycle
- **Thread Safety**: pthread_mutex_t protects all CGNAT state access for concurrent operations
- **Web Architecture**: Lightweight HTTP server with JSON APIs and background traffic simulator

## Build & Run

### Build
```bash
make              # Build all targets (cgnat, stress_test, web_server)
make clean        # Clean build artifacts
```

### Run Main Program
```bash
./cgnat           # Interactive CLI demo with simulations
```

### Run Web Dashboard
```bash
make web          # Build and run web dashboard
./web_server      # Run directly (serves on port 5000)
```

### Run Stress Test
```bash
make stress       # Build and run stress test
./stress_test     # Run directly
```

## System Capacity

- **Public IPs**: 10 configured (203.0.113.1-10)
- **Total Ports**: 645,120 usable ports (1024-65535 per IP)
- **Max Customers**: 20,000 simultaneous connections (verified)
- **NAT Table**: 50,000 entry capacity
- **Hash Buckets**: 65,536 for both outbound and inbound tables

## Performance (Verified)

- **Connection Creation**: 5.4 million connections/sec
- **Packet Translation**: 5.6 million packets/sec
- **Lookup Complexity**: O(1) average case
- **Port Allocation**: O(1) amortized
- **Memory Usage**: ~10 MB total

## Stress Test Results

```
✓ 20,000 connections created: 0 failures
✓ 50,000 packets translated: 0 failures  
✓ Creation rate: ~5.4M connections/sec
✓ Translation rate: ~5.6M packets/sec
✓ Port pool utilization: 3.10% with 20K connections
✓ System stable under load
```

## Files

- `cgnat.h` - Header file with data structures and function declarations
- `cgnat.c` - Core CGNAT implementation (hash tables, NAT translation, port management, thread-safe)
- `main.c` - Interactive CLI demo program with traffic simulations
- `web_server.c` - HTTP API server with real-time monitoring endpoints and background traffic simulator
- `dashboard.html` - Responsive web UI with live charts, metrics, and connection tables
- `stress_test.c` - Performance validation tool for 20K connections
- `Makefile` - Build system
- `README.md` - Detailed documentation

## Interactive Commands

### CLI Mode (`./cgnat`):
- `stats` - Display system statistics
- `sim` - Simulate customer traffic
- `pool` - Demonstrate port pooling (100 concurrent connections)
- `cleanup` - Clean expired connections
- `quit` - Exit program

### Web Dashboard (`./web_server`):
- Runs on port 5000 with automatic traffic simulation
- Dashboard at `/` - Real-time monitoring UI
- API at `/api/stats` - JSON statistics endpoint
- API at `/api/connections` - JSON active connections list
- Auto-refreshes every 2 seconds

## User Preferences

- Standard C11 coding style with clear comments
- Modular design with separation of concerns
- Performance-oriented data structures
- Comprehensive error handling and logging
