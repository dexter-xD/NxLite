# NxLite Benchmark Suite

Scripts for validating NxLite's performance, edge caching, compression, and scalability under various real-world scenarios.

## 🎯 Quick Start

```bash
# Fast validation (recommended first test)
./quick_test.sh

# Full comprehensive testing
./comprehensive_test.sh

# Production-ready stress testing
./production_test.sh
```

## 📋 Test Scripts Overview

### 🚀 Main Test Scripts

| Script | Duration | Purpose | Use Case |
|--------|----------|---------|----------|
| `quick_test.sh` | ~30s | Fast validation | Development, CI/CD |
| `comprehensive_test.sh` | ~15 min | Complete feature testing | Release validation |
| `production_test.sh` | ~20 min | Production simulation | Pre-deployment testing |

### 📝 Lua Test Scripts

| Script | Description | Cache Rate | Scenario |
|--------|-------------|------------|----------|
| `mixed_cache.lua` | 33% conditional requests | 33% | Typical web traffic |
| `mixed_cache_50.lua` | 50% conditional requests | 50% | Return visitors |
| `heavy_cache.lua` | 80% conditional requests | 80% | High repeat traffic |
| `real_world.lua` | Mixed file types & patterns | 60% | Production simulation |
| `compression_mix.lua` | Mixed compression testing | 40% | Compression validation |
| `html_files.lua` | HTML file testing | 50% | Web page serving |
| `static_assets.lua` | CSS/JS/Images testing | 90%+ | Asset delivery |
| `mobile_simulation.lua` | Mobile client patterns | 70% | Mobile optimization |

## 🏃‍♂️ Running Tests

### Prerequisites

```bash
# Ensure wrk is installed
sudo apt install wrk  # Ubuntu/Debian
brew install wrk      # macOS

# Ensure server is running
cd ../build && ./nxlite &
cd ../benchmark
```

### Basic Usage

```bash
# Make scripts executable
chmod +x *.sh

# Quick performance check
./quick_test.sh

# Example output:
# Quick NxLite Performance Test
# =============================
# 
# 1. Baseline Performance
#    Baseline RPS: 194,598.43
# 
# 2. Edge Caching (33% conditional)
#    Cached RPS: 263,696.32
# 
# 3. Heavy Caching (80% conditional)
#    Heavy Cache RPS: 272,816.16
# 
# Cache Improvement: 35.5%
# Heavy Cache Improvement: 40.2%
```

### Individual Test Execution

```bash
# Test specific caching scenarios
wrk -t4 -c100 -d30s -s mixed_cache.lua http://localhost:7877/
wrk -t4 -c100 -d30s -s heavy_cache.lua http://localhost:7877/

# Test compression
wrk -t4 -c100 -d30s -H 'Accept-Encoding: gzip' http://localhost:7877/

# Stress testing
wrk -t8 -c500 -d30s -s mixed_cache.lua http://localhost:7877/

# Real-world simulation
wrk -t4 -c100 -d60s -s real_world.lua http://localhost:7877/
```

## 📊 Expected Results

### Performance Baselines

| Test Scenario | Expected RPS | Transfer Rate | Latency |
|---------------|--------------|---------------|---------|
| **Baseline** | 80k-100k | 0.8-1.0 GB/s | 1-2ms |
| **33% Cache** | 200k-300k | 1.5-2.5 GB/s | 200-400μs |
| **50% Cache** | 250k-350k | 1.0-2.0 GB/s | 200-350μs |
| **80% Cache** | 250k-400k | 400-800 MB/s | 300-500μs |
| **Stress (500c)** | 300k-500k | 2.5-3.5 GB/s | 1-3ms |

### Key Performance Indicators

- **Cache Hit Improvement**: 150-300% RPS increase
- **Bandwidth Reduction**: 50-80% with heavy caching
- **Latency Reduction**: 60-75% improvement
- **Stress Performance**: 400k+ RPS sustainable

## 🔍 Test Details

### quick_test.sh

**Purpose**: Fast development validation  
**Duration**: ~30 seconds  
**Tests**:
- Baseline performance (10s)
- 33% caching (10s)
- 80% caching (10s)

**Usage**:
```bash
./quick_test.sh
```

**Output**: RPS comparison with percentage improvements

---

### comprehensive_test.sh

**Purpose**: Complete feature validation  
**Duration**: ~15 minutes  
**Test Categories**:

1. **Baseline Tests**
   - Standard GET requests
   - ~~HEAD requests~~ (temporarily disabled)

2. **Edge Caching Tests**
   - 33%, 50%, 80% conditional request scenarios
   - ETag validation
   - 304 Not Modified responses

3. **Different File Types**
   - HTML files with caching
   - Static assets (CSS, JS, images)

4. **Compression Tests**
   - Gzip compression
   - Mixed compression scenarios

5. **Real-World Simulation**
   - Mixed content types
   - Realistic user patterns
   - Mobile client simulation

6. **Stress Tests**
   - High concurrency (500 connections)
   - Keep-alive connection testing

**Usage**:
```bash
./comprehensive_test.sh
```

**Output**: Detailed CSV results in `test_results.csv`

---

### production_test.sh

**Purpose**: Production readiness validation  
**Duration**: ~20 minutes  
**Test Phases**:

1. **Warm-up Phase**
   - Server and cache warming

2. **Production Load Tests**
   - Low load (50 concurrent, 60s)
   - Medium load (200 concurrent, 60s)
   - High load (500 concurrent, 30s)

3. **Stress Testing**
   - Traffic spikes (1000 concurrent, 15s)
   - Sustained load (300 concurrent, 2 minutes)

4. **Edge Cases**
   - Mobile simulation
   - Mixed API/static content

**Usage**:
```bash
./production_test.sh
```

**Output**: Production readiness checklist and recommendations

## 🧪 Lua Script Details

### mixed_cache.lua
```lua
-- 33% conditional requests (every 3rd request)
-- Simulates typical web traffic with some returning visitors
-- Tests: Basic ETag caching, 304 responses
```

### heavy_cache.lua
```lua
-- 80% conditional requests (4 out of 5 requests)
-- Simulates high repeat visitor scenarios
-- Tests: Heavy cache optimization, bandwidth reduction
```

### real_world.lua
```lua
-- Mixed file types with realistic weights:
-- - 40% index.html
-- - 15% CSS files
-- - 15% JavaScript
-- - 10% Images
-- - 10% API calls (non-cacheable)
-- - 10% Other pages
-- 60% cache hit rate for cacheable content
```

### compression_mix.lua
```lua
-- Tests various Accept-Encoding headers:
-- - gzip, deflate, br
-- - gzip, deflate
-- - gzip only
-- - deflate only
-- - identity (no compression)
-- - * (any compression)
-- 40% conditional requests
```

## 📈 Analysis and Reporting

### Automated Analysis

```bash
# View results in formatted table
cat test_results.csv | column -t -s,

# Compare baseline vs cached performance
grep -E "(Baseline|Cache)" test_results.csv

# Check for performance regressions
./quick_test.sh > current_results.txt
diff baseline_results.txt current_results.txt
```

### Manual Analysis

```bash
# Check server logs during testing
tail -f ../build/logs/nxlite.log

# Monitor cache hit rates
grep "Cache hit" ../build/logs/nxlite.log | wc -l
grep "Cache miss" ../build/logs/nxlite.log | wc -l

# Memory usage monitoring
ps aux | grep nxlite
top -p $(pgrep nxlite)
```

## 🛠️ Customization

### Creating Custom Tests

```bash
# Create new Lua script
cat > my_test.lua << 'EOF'
local request_count = 0

request = function()
    request_count = request_count + 1
    return wrk.format("GET", "/my-endpoint")
end

response = function(status, headers, body)
    -- Custom response handling
end
EOF

# Run custom test
wrk -t4 -c100 -d30s -s my_test.lua http://localhost:7877/
```

### Modifying Test Parameters

```bash
# Edit script variables
vim comprehensive_test.sh

# Key variables:
# SERVER_URL="http://localhost:7877"
# TEST_DURATION="30s"  
# THREADS=4
# CONNECTIONS=100
```

## 🚨 Troubleshooting

### Common Issues

**Server not responding:**
```bash
# Check if server is running
curl -I http://localhost:7877/
ps aux | grep nxlite

# Start server
cd ../build && ./nxlite &
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

**Low performance results:**
```bash
# Check system limits
ulimit -n
cat /proc/sys/fs/file-max

# Optimize system
sudo sysctl -w fs.file-max=100000
sudo sysctl -w net.core.somaxconn=65536
```

### Performance Validation

**Expected vs Actual Results:**

| Metric | Expected | Action if Below |
|--------|----------|-----------------|
| Baseline RPS | >80k | Check system resources |
| Cache Improvement | >150% | Verify cache implementation |
| Stress RPS | >300k | Check concurrency limits |
| Latency | <2ms | Investigate bottlenecks |

## 📝 Contributing

### Adding New Tests

1. Create Lua script for test scenario
2. Add test case to appropriate shell script
3. Document expected results
4. Validate against baseline performance

### Test Guidelines

- **Consistent Parameters**: Use standard thread/connection counts
- **Meaningful Duration**: Balance accuracy vs test time
- **Real-World Relevance**: Simulate actual usage patterns
- **Clear Documentation**: Explain test purpose and expectations

## 🏆 Benchmarking Best Practices

### Pre-Test Checklist

- [ ] Server running on target port
- [ ] System limits optimized
- [ ] No other high-load processes
- [ ] Sufficient disk space for logs
- [ ] Network stability verified

### During Testing

- Monitor system resources
- Check for error messages
- Validate cache behavior
- Compare against baselines

### Post-Test Analysis

- Review performance trends
- Identify bottlenecks
- Document configuration changes
- Archive results for comparison

---

**Ready to benchmark NxLite's incredible performance!** 🚀

For questions or issues, check the main project README or open an issue. 