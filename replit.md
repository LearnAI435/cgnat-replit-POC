# CGNAT Implementation in C

## Overview
A Carrier-Grade NAT (CGNAT) system implemented in C for managing 20,000 customers using only 10 public IP addresses. The system implements efficient NAT translation tables, port pool management, and connection state tracking.

## Recent Changes
- **2025-10-19**: Initial project setup with core CGNAT implementation

## Project Architecture

### Core Components
1. **NAT Translation Tables**: Hash-based bidirectional mapping for fast lookups
   - Outbound table: Maps private IP:port → public IP:port
   - Inbound table: Maps public IP:port → private IP:port

2. **Port Pool Management**: Distributes 655,350 ports (10 IPs × 65,535 ports each)
   - Dynamic port allocation with availability tracking
   - Port recycling after connection timeout

3. **Connection State Tracking**: Manages active sessions with timeouts
   - TCP session tracking with state awareness
   - UDP session tracking with idle timeout
   - Automatic cleanup of expired connections

4. **Packet Processing**: NAT translation pipeline
   - Outbound NAT: Rewrites source IP:port
   - Inbound NAT: Rewrites destination IP:port

### Key Design Decisions
- Using uthash library for efficient hash table operations
- Memory-efficient data structures to support 20k+ concurrent sessions
- Configurable timeout values for different protocols
- Statistics tracking for monitoring and debugging

## Build & Run
```bash
make
./cgnat
```

## Configuration
- Public IP pool: 10 configurable public IP addresses
- Private subnet: Supports customer IP ranges
- Port range: Dynamic ports 1024-65535
- Timeouts: TCP (300s), UDP (60s), configurable

## User Preferences
- Standard C coding style with clear comments
- Modular design with separation of concerns
