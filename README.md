# NxLite

A lightweight HTTP server written in C with caching and compression features.

## ✨ Features

### Core HTTP Server
- **HTTP/1.1 Support**: GET and HEAD methods
- **Static File Serving**: Serves files from a configurable document root
- **Keep-Alive Connections**: Persistent connections support
- **MIME Type Detection**: Automatic content-type headers
- **ETag Support**: Conditional requests with If-None-Match headers

### Caching System
- **In-Memory Response Caching**: Hash-based cache for static assets
- **304 Not Modified Responses**: Efficient handling of unchanged content
- **Configurable Cache TTL**: Time-based cache expiration
- **Cache Size Limits**: Configurable maximum number of cached responses

### Compression
- **Gzip/Deflate Support**: Automatic content compression
- **Content-Type Aware**: Compression based on MIME types
- **Client Negotiation**: Respects Accept-Encoding headers

### Security Features
- **Path Traversal Protection**: Prevents directory traversal attacks
- **Request Size Limits**: Configurable maximum request sizes
- **Input Sanitization**: Prevents log injection attacks
- **Security Headers**: X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, etc.
- **DoS Protection**: Rate limiting, connection limits, and attack mitigation

### Architecture
- **Master-Worker Model**: Multi-process architecture
- **Event-Driven I/O**: Uses epoll for non-blocking operations
- **Memory Pooling**: Custom memory management
- **CPU Affinity**: Worker processes bound to specific CPU cores

## 🛠️ Building and Setup

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
git clone <repository-url>
cd NxLite

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)
```

## 🚀 Running the Server

### Basic Usage

```bash
# Run with default configuration
./build/NxLite

# Run with custom config file
./build/NxLite server.conf

# Run in background
./build/NxLite server.conf &
```

### Development Mode

Development mode disables DoS protection features for easier testing:

```bash
# Enable development mode
./build/NxLite server.conf --dev
./build/NxLite server.conf -d

# Or via configuration file
echo "development_mode=true" >> server.conf
```

**Note**: Development mode should never be used in production environments.

## ⚙️ Configuration

### Example Configuration (`server.conf`)

```ini
# Basic Settings
port=7877
worker_processes=8
root=../static

# Connection Settings
max_connections=100000
keep_alive_timeout=120

# Caching
cache_timeout=3600
cache_size=10000

# Development
development_mode=false

# Logging
log=./logs/access.log
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
| `development_mode` | false | Enable/disable development mode |
| `log` | ./logs/access.log | Access log file path |

## 📋 Logging and Monitoring

### Log Files

The server generates logs in the configured log directory:

```bash
# View access logs
tail -f logs/access.log

# Check for errors
grep "ERROR\|WARN" logs/access.log
```

### Log Format

Access logs include:
- Timestamp
- Log level (INFO, WARN, ERROR, DEBUG)
- Process information
- Request details
- Response status
- Security events

### Health Checks

```bash
# Basic connectivity test
curl -I http://localhost:7877/

# Test compression
curl -H "Accept-Encoding: gzip" -v http://localhost:7877/

# Test caching
curl -H "If-None-Match: \"some-etag\"" http://localhost:7877/
```

## 🧪 Testing

### Security Testing

```bash
cd benchmark

# Run vulnerability tests
./vulnerability_test.sh

# Run DoS protection tests
./dos_protection_test.sh
```

### Functionality Testing

```bash
cd benchmark

# Basic functionality testing
./quick_test.sh

# Comprehensive feature testing
./comprehensive_test.sh

# Load testing
./production_test.sh
```

### Testing Suite

The `benchmark/` directory contains a comprehensive testing suite with:

- **Security Tests**: Vulnerability scanning and DoS protection validation
- **Functionality Tests**: HTTP features, caching, and compression testing
- **Load Tests**: Stress testing and concurrent connection handling
- **Lua Scripts**: Various test scenarios for different use cases

See [benchmark/README.md](benchmark/README.md) for detailed testing documentation.

### Manual Testing

```bash
# Test basic functionality
curl http://localhost:7877/

# Test different file types
curl http://localhost:7877/index.html
curl http://localhost:7877/style.css
curl http://localhost:7877/script.js

# Test compression
curl -H "Accept-Encoding: gzip" http://localhost:7877/large-file.html
```

## 🔧 Development

### Debug Mode

For debugging, you can run the server with additional logging:

```bash
# Enable debug logging (if implemented)
./build/NxLite server.conf --debug

# Run with valgrind for memory checking
valgrind --leak-check=full ./build/NxLite server.conf
```

### Directory Structure

```
NxLite/
├── src/           # Source code
├── include/       # Header files
├── static/        # Default document root
├── logs/          # Log files
├── benchmark/     # Testing scripts
├── build/         # Build output
└── server.conf    # Configuration file
```

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

### Development Setup

```bash
# Install development tools
sudo apt install valgrind gdb clang-format

# Run tests
cd benchmark
./vulnerability_test.sh
./dos_protection_test.sh
```
