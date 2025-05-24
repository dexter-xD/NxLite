# NxLite

A high-performance HTTP server written in C, designed for maximum efficiency, edge caching, and production-grade performance.

## 🚀 Performance Highlights

- **437,344 RPS** peak throughput (500 concurrent connections)
- **241.9% performance improvement** with edge caching
- **Sub-millisecond latency** (281μs average with caching)
- **81% bandwidth reduction** through intelligent caching
- **Production-ready scalability** handling 26+ million requests/minute

## ✨ Features

### Core Performance
- **Zero-Copy I/O**: Uses sendfile() for efficient file transfers
- **Non-Blocking Architecture**: Event-driven with epoll for maximum concurrency
- **Master-Worker Model**: Pre-fork architecture similar to Nginx
- **Memory Pooling**: Custom allocation system reduces fragmentation
- **Keep-Alive Support**: Persistent connections for reduced latency

### Edge Caching System
- **Intelligent Response Caching**: Hash-based in-memory cache for static assets
- **ETag Support**: RFC-compliant conditional requests with If-None-Match
- **304 Not Modified**: Optimized responses for unchanged content
- **Vary Header Handling**: Cache awareness of Accept-Encoding and User-Agent
- **Cache Invalidation**: Time-based expiration with configurable timeouts

### Compression Engine
- **Gzip/Deflate Support**: Automatic content compression for compatible clients
- **MIME-Type Awareness**: Intelligent compression based on content type
- **Adaptive Compression Levels**: Optimized settings per content type
- **Bandwidth Optimization**: Up to 81% reduction in data transfer

## 📊 Benchmark Results

### Edge Caching Performance

| Scenario | RPS | Improvement | Transfer Rate | Latency |
|----------|-----|-------------|---------------|---------|
| Baseline | 91,216 | - | 0.90 GB/s | 1.09ms |
| 33% Cache | 311,891 | **+241.9%** | 2.07 GB/s | 281μs |
| 50% Cache | 284,923 | **+212.3%** | 1.42 GB/s | 261μs |
| 80% Cache | 272,238 | **+198.5%** | 571 MB/s | 346μs |

### Stress Testing

| Test | Concurrency | RPS | Transfer Rate |
|------|-------------|-----|---------------|
| Medium Load | 200 | 311,891 | 2.07 GB/s |
| **High Load** | **500** | **437,344** | **2.90 GB/s** |
| Sustained (60s) | 300 | 272,238 | 571 MB/s |

### Compression Performance

| Mode | RPS | Transfer Rate | Compression Ratio |
|------|-----|---------------|-------------------|
| No Compression | 91,216 | 0.90 GB/s | - |
| Gzip | 34,065 | 100 MB/s | ~70% |
| Mixed Compression | 98,149 | 244 MB/s | ~60% |

## 🏗️ Architecture

### Master-Worker Model

```
┌─────────────────────────────────────────────────────────┐
│                    Master Process                       │
│  • Configuration Management  • Worker Monitoring        │
│  • Socket Creation          • Graceful Shutdown         │
└─────────────────┬───────────────────────────────────────┘
                  │
      ┌───────────┼───────────┬───────────┐
      │           │           │           │
┌─────▼────┐ ┌────▼────┐ ┌────▼────┐ ┌───▼──────┐
│ Worker 1 │ │ Worker 2│ │ Worker 3│ │ Worker N │
│ • epoll  │ │ • epoll │ │ • epoll │ │ • epoll  │
│ • cache  │ │ • cache │ │ • cache │ │ • cache  │
│ • gzip   │ │ • gzip  │ │ • gzip  │ │ • gzip   │
└──────────┘ └─────────┘ └─────────┘ └──────────┘
```

### Request Processing Pipeline

```
Client Request
      ↓
┌─────────────┐    Cache Hit?    ┌─────────────┐
│  Parse      │ ──────────────→  │  Cached     │
│  Headers    │                  │  Response   │
└─────┬───────┘                  └─────────────┘
      │ Cache Miss                      ↓
      ↓                           ┌─────────────┐
┌─────────────┐                   │  Send       │
│  File       │ ←──── ETag ────── │  304/200    │
│  System     │      Check       │  Response   │
└─────┬───────┘                   └─────────────┘
      │
      ↓
┌─────────────┐    Compress?     ┌─────────────┐
│  Content    │ ──────────────→  │  Gzip/      │
│  Type       │                  │  Deflate    │
│  Check      │                  │  Content    │
└─────┬───────┘                  └─────┬───────┘
      │                                │
      ↓                                ↓
┌─────────────┐                  ┌─────────────┐
│  Cache      │ ←────────────────│  Send       │
│  Response   │                  │  Response   │
└─────────────┘                  └─────────────┘
```

## 🛠️ Building and Running

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake libz-dev

# CentOS/RHEL
sudo yum install gcc cmake make zlib-devel

# macOS
brew install cmake
```

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/dexter-xd/NxLite.git
cd NxLite

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# The binary will be at: build/nxlite
```

### Running the Server

```bash
# Run with default configuration
./nxlite

# Run with custom config
./nxlite ../server.conf

# Background execution
nohup ./nxlite > server.log 2>&1 &
```

## ⚙️ Configuration

### Example Configuration (`server.conf`)

```ini
# Basic Settings
port=7877
worker_processes=auto
root=../static

# Performance Tuning
max_connections=100000
keep_alive_timeout=120
cache_timeout=3600
cache_size=10000

# Logging
log_level=INFO
access_log=./logs/access.log
error_log=./logs/error.log
```

### Configuration Options

| Parameter | Default | Description |
|-----------|---------|-------------|
| `port` | 7877 | Server listening port |
| `worker_processes` | 4 | Number of worker processes |
| `root` | ../static | Document root directory |
| `max_connections` | 10000 | Maximum concurrent connections |
| `keep_alive_timeout` | 60 | Keep-alive timeout (seconds) |
| `cache_timeout` | 3600 | Response cache TTL (seconds) |
| `cache_size` | 10000 | Maximum cached responses |

## 📈 Benchmarking

### Quick Performance Test

```bash
cd benchmark

# Fast validation (10 seconds)
./quick_test.sh

# Comprehensive testing (30 seconds per test)
./comprehensive_test.sh

# Production simulation (60+ seconds)
./production_test.sh
```

### Manual Testing with wrk

```bash
# Baseline performance
wrk -t4 -c100 -d30s http://localhost:7877/

# Edge caching test (33% conditional)
wrk -t4 -c100 -d30s -s mixed_cache.lua http://localhost:7877/

# Stress test (500 connections)
wrk -t8 -c500 -d30s -s mixed_cache.lua http://localhost:7877/

# Compression test
wrk -t4 -c100 -d30s -H 'Accept-Encoding: gzip' http://localhost:7877/
```

### Real-World Simulation

```bash
# Mixed content types with caching
wrk -t4 -c100 -d60s -s real_world.lua http://localhost:7877/

# Heavy caching scenario (80% repeat visitors)
wrk -t4 -c100 -d30s -s heavy_cache.lua http://localhost:7877/
```

## 🔧 Performance Tuning

### System Limits

```bash
# /etc/sysctl.conf
fs.file-max = 100000
net.core.somaxconn = 65536
net.ipv4.tcp_max_syn_backlog = 65536
net.ipv4.tcp_fin_timeout = 30
net.ipv4.tcp_keepalive_time = 300

# Apply changes
sudo sysctl -p
```

### Process Limits

```bash
# /etc/security/limits.conf
* soft nofile 100000
* hard nofile 100000

# Verify
ulimit -n
```

### Optimal Settings

```ini
# For 8-core server
worker_processes=8
max_connections=50000
keep_alive_timeout=120

# For high-traffic sites
cache_size=50000
cache_timeout=7200
```

## 📋 Monitoring and Logs

### Log Files

```bash
# Real-time access log
tail -f build/logs/access.log

# Error monitoring
tail -f build/logs/error.log

# Performance metrics
grep "Cache hit" build/logs/nxlite.log
```

### Health Checks

```bash
# Basic connectivity
curl -I http://localhost:7877/

# Cache validation
curl -H "If-None-Match: \"some-etag\"" http://localhost:7877/

# Compression check
curl -H "Accept-Encoding: gzip" -v http://localhost:7877/
```

## 🏆 Production Deployment

### Recommended Setup

1. **Load Balancer**: Place behind nginx/HAProxy for SSL termination
2. **Monitoring**: Use Prometheus + Grafana for metrics
3. **Logging**: Centralized logging with ELK stack
4. **Caching**: Configure appropriate cache sizes for your workload

### Docker Deployment

```dockerfile
FROM ubuntu:22.04
RUN apt update && apt install -y build-essential cmake libz-dev
COPY . /app
WORKDIR /app
RUN mkdir build && cd build && cmake .. && make
EXPOSE 7877
CMD ["./build/nxlite"]
```

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

### Development Setup

```bash
# Install development dependencies
sudo apt install valgrind gdb clang-format

# Run tests
cd benchmark && ./comprehensive_test.sh

# Memory leak check
valgrind --leak-check=full ./build/nxlite
```

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Inspired by nginx architecture
- Built with modern C performance practices
- Benchmarked against industry standards

---

**NxLite** - Where performance meets simplicity. Ready for production. 🚀
