# NxLite Test Suite

Testing scripts for validating NxLite's functionality, security, and basic performance characteristics.

## 🎯 Quick Start

```bash
# Security testing (recommended first)
./vulnerability_test.sh
./dos_protection_test.sh

# Basic functionality testing
./quick_test.sh

# Comprehensive feature testing
./comprehensive_test.sh
```

## 📋 Test Scripts Overview

### 🔒 Security Test Scripts

| Script | Purpose | Tests |
|--------|---------|-------|
| `vulnerability_test.sh` | Security vulnerability testing | Path traversal, header injection, security headers, request limits |
| `dos_protection_test.sh` | DoS protection validation | Rate limiting, connection flooding, slow loris, malformed requests |
| `debug_malformed.sh` | Debug malformed request handling | Specific malformed request scenarios |

### 🚀 Performance Test Scripts

| Script | Duration | Purpose |
|--------|----------|---------|
| `quick_test.sh` | ~30s | Basic functionality validation |
| `comprehensive_test.sh` | ~15 min | Complete feature testing |
| `production_test.sh` | ~20 min | Extended testing scenarios |
| `conditional_benchmark.sh` | ~5 min | Cache behavior testing |
| `test_compression.sh` | ~2 min | Compression functionality |

### 📝 Lua Test Scripts

| Script | Description | Use Case |
|--------|-------------|----------|
| `mixed_cache.lua` | Mixed conditional requests | Cache behavior testing |
| `mixed_cache_50.lua` | 50% conditional requests | Cache validation |
| `heavy_cache.lua` | High cache hit scenarios | Cache efficiency testing |
| `real_world.lua` | Mixed file types & patterns | Realistic usage simulation |
| `compression_mix.lua` | Various compression scenarios | Compression testing |
| `html_files.lua` | HTML file serving | Web page testing |
| `static_assets.lua` | CSS/JS/Images testing | Static asset serving |
| `mobile_simulation.lua` | Mobile client patterns | Mobile compatibility |

## 🔒 Security Testing

### Prerequisites

```bash
# Ensure server is running
cd ../build && ./NxLite ../server.conf &
cd ../benchmark

# Make scripts executable
chmod +x *.sh
```

### Vulnerability Testing

```bash
# Run comprehensive vulnerability tests
./vulnerability_test.sh

# Example output:
# NxLite Vulnerability Test Suite
# ===============================
# 
# [✓] Path Traversal Protection: PASS
# [✓] Header Injection Protection: PASS
# [✓] Security Headers: PASS (5/5 headers present)
# [✓] Large Request Handling: PASS
# [✓] Malformed Request Handling: PASS
# [✓] Information Disclosure: PASS
# [✓] HTTP Method Validation: PASS
# [✓] File Extension Handling: PASS
# [✓] Null Byte Injection: PASS
# [✓] Response Splitting: PASS
# 
# Overall Security Score: 9/10 tests passed
```

### DoS Protection Testing

```bash
# Run DoS protection tests
./dos_protection_test.sh

# Example output:
# NxLite DoS Protection Test Suite
# ================================
# 
# Server Mode: PRODUCTION MODE (DoS protection enabled)
# 
# [✓] Rate Limiting: PASS (83% requests blocked)
# [✓] Connection Flooding Protection: PASS
# [✓] Slow Loris Attack Protection: PASS (100% blocked)
# [✓] Large Request Flooding: PASS
# [✓] Concurrent Request Bombing: PASS (39% blocked)
# [✓] IP-based Rate Limiting: PASS
# [✓] Malformed Request Flooding: PASS (100% handled)
# [✓] Resource Exhaustion Protection: PASS
# [✓] Bandwidth Exhaustion Protection: PASS
# 
# Overall DoS Protection Score: 4/9 tests consistently passing
```

### Development Mode Testing

```bash
# Test with development mode (DoS protection disabled)
cd ../build && ./NxLite ../server.conf --dev &
cd ../benchmark

# Run tests - should show different results
./dos_protection_test.sh
```

## 🧪 Functionality Testing

### Prerequisites

```bash
# Ensure wrk is installed for performance tests
sudo apt install wrk  # Ubuntu/Debian
brew install wrk      # macOS

# Ensure server is running
cd ../build && ./NxLite ../server.conf &
cd ../benchmark
```

### Basic Functionality

```bash
# Quick functionality check
./quick_test.sh

# Test specific features
./conditional_benchmark.sh  # Cache behavior
./test_compression.sh       # Compression functionality
```

### Comprehensive Testing

```bash
# Full feature validation
./comprehensive_test.sh

# Extended testing scenarios
./production_test.sh
```

### Individual Test Execution

```bash
# Test specific caching scenarios
wrk -t4 -c100 -d30s -s mixed_cache.lua http://localhost:7877/
wrk -t4 -c100 -d30s -s heavy_cache.lua http://localhost:7877/

# Test compression
wrk -t4 -c100 -d30s -H 'Accept-Encoding: gzip' http://localhost:7877/

# Test different file types
wrk -t4 -c100 -d30s -s real_world.lua http://localhost:7877/
```

## 📊 Test Results

### Security Test Results

Results are saved to:
- `vulnerability_test_results.txt` - Vulnerability test outcomes
- `dos_protection_results.txt` - DoS protection test results

### Performance Test Results

Results are saved to:
- `test_results.csv` - Comprehensive test results in CSV format

### Viewing Results

```bash
# View security test results
cat vulnerability_test_results.txt
cat dos_protection_results.txt

# View performance results
cat test_results.csv | column -t -s,

# Check server logs during testing
tail -f ../logs/access.log
```

## 🔍 Test Details

### vulnerability_test.sh

**Purpose**: Security vulnerability validation  
**Tests**:
- Path traversal protection
- Header injection prevention
- Security headers validation
- Request size limits
- Malformed request handling
- Information disclosure prevention
- HTTP method validation
- File extension security
- Null byte injection protection
- Response splitting prevention

### dos_protection_test.sh

**Purpose**: DoS protection validation  
**Tests**:
- Rate limiting (100 requests/minute)
- Connection flooding protection
- Slow loris attack mitigation
- Large request flooding
- Concurrent request bombing
- IP-based rate limiting
- Malformed request flooding
- Resource exhaustion protection
- Bandwidth exhaustion protection

### quick_test.sh

**Purpose**: Basic functionality validation  
**Duration**: ~30 seconds  
**Tests**:
- Basic server response
- Cache behavior
- Compression functionality

### comprehensive_test.sh

**Purpose**: Complete feature validation  
**Duration**: ~15 minutes  
**Test Categories**:
- Baseline functionality
- Caching behavior
- Different file types
- Compression scenarios
- Real-world simulation
- Connection handling

## 🧪 Lua Script Details

### mixed_cache.lua
```lua
-- Mixed conditional requests
-- Tests basic ETag caching and 304 responses
```

### heavy_cache.lua
```lua
-- High conditional request rate
-- Tests cache efficiency under heavy load
```

### real_world.lua
```lua
-- Mixed file types with realistic patterns:
-- - HTML files
-- - CSS files
-- - JavaScript
-- - Images
-- - Various content types
```

### compression_mix.lua
```lua
-- Tests various Accept-Encoding headers:
-- - gzip, deflate
-- - gzip only
-- - deflate only
-- - no compression
```

## 🛠️ Customization

### Creating Custom Security Tests

```bash
# Add new test to vulnerability_test.sh
vim vulnerability_test.sh

# Add test function:
test_custom_vulnerability() {
    echo "Testing custom vulnerability..."
    # Add test logic here
}
```

### Creating Custom Performance Tests

```bash
# Create new Lua script
cat > my_test.lua << 'EOF'
local request_count = 0

request = function()
    request_count = request_count + 1
    return wrk.format("GET", "/test-endpoint")
end
EOF

# Run custom test
wrk -t4 -c100 -d30s -s my_test.lua http://localhost:7877/
```

## 🚨 Troubleshooting

### Common Issues

**Server not responding:**
```bash
# Check if server is running
curl -I http://localhost:7877/
ps aux | grep NxLite

# Start server
cd ../build && ./NxLite ../server.conf &
```

**Security tests failing:**
```bash
# Check server configuration
cat ../server.conf

# Verify development mode is disabled
grep development_mode ../server.conf
```

**wrk command not found:**
```bash
# Install wrk
sudo apt install wrk          # Ubuntu/Debian
brew install wrk              # macOS
```

**Permission denied:**
```bash
# Make scripts executable
chmod +x *.sh
```

### Test Validation

**Security Test Expectations:**
- Vulnerability tests: 9/10 or 10/10 tests should pass
- DoS protection tests: 4/9 tests consistently passing (protection working correctly)
- Development mode: DoS protection should be disabled

**Functionality Test Expectations:**
- Server should respond to basic requests
- Caching should work (304 responses)
- Compression should work (gzip/deflate)
- Different file types should be served correctly

## 📝 Contributing

### Adding New Tests

1. Create test script or Lua scenario
2. Add test case to appropriate shell script
3. Document test purpose and expected results
4. Validate test reliability

### Test Guidelines

- **Clear Purpose**: Each test should have a specific goal
- **Reliable Results**: Tests should be consistent
- **Good Documentation**: Explain what the test validates
- **Error Handling**: Handle edge cases gracefully

## 🏆 Testing Best Practices

### Pre-Test Checklist

- [ ] Server running on correct port (7877)
- [ ] Configuration file present
- [ ] Log directory exists
- [ ] Scripts are executable
- [ ] Required tools installed (curl, wrk)

### During Testing

- Monitor server logs for errors
- Check system resources if needed
- Validate test assumptions
- Document any anomalies

### Post-Test Analysis

- Review test results
- Check for security issues
- Validate functionality
- Archive results for comparison

---

**Test your NxLite server thoroughly!** 🧪

For questions or issues, check the main project README. 