#!/bin/bash

# Production-Ready NxLite Testing
# Simulates realistic production loads and scenarios

set -e

SERVER_URL="http://localhost:7877"
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}      Production Testing Suite          ${NC}"  
echo -e "${BLUE}========================================${NC}"
echo ""

if ! curl -s $SERVER_URL/ > /dev/null 2>&1; then
    echo -e "${RED}Error: Server not running at $SERVER_URL${NC}"
    echo "Please start the server first: cd build && ./nxlite"
    exit 1
fi

echo -e "${GREEN}✓ Server is running${NC}"
echo ""

run_production_test() {
    local test_name="$1"
    local description="$2"
    local command="$3"
    
    echo -e "${YELLOW}Testing: $test_name${NC}"
    echo "Scenario: $description"
    echo ""
    
    local result=$(eval $command 2>&1)
    local rps=$(echo "$result" | grep "Requests/sec:" | awk '{print $2}')
    local transfer=$(echo "$result" | grep "Transfer/sec:" | awk '{print $2$3}')
    local latency_avg=$(echo "$result" | grep "Latency" | head -1 | awk '{print $2}')
    local latency_99=$(echo "$result" | grep "99%" | awk '{print $2}')
    local errors=$(echo "$result" | grep "errors" | wc -l)
    
    echo -e "${GREEN}Results:${NC}"
    echo "  ┌─ Throughput: $rps req/sec"
    echo "  ├─ Transfer: $transfer" 
    echo "  ├─ Avg Latency: $latency_avg"
    echo "  ├─ 99th Percentile: $latency_99"
    echo "  └─ Errors: $errors"
    echo ""
    echo "────────────────────────────────────────"
    echo ""
}

echo -e "${BLUE}1. Warm-up Phase${NC}"
echo "Warming up server and caches..."
wrk -t2 -c10 -d10s $SERVER_URL/ > /dev/null 2>&1
wrk -t2 -c10 -d5s -s mixed_cache.lua $SERVER_URL/ > /dev/null 2>&1
echo -e "${GREEN}✓ Warm-up complete${NC}"
echo ""

echo -e "${BLUE}2. Production Load Tests${NC}"
echo "========================================"

run_production_test "Low_Load_Baseline" \
    "Typical low traffic (50 concurrent users)" \
    "wrk -t2 -c50 -d60s $SERVER_URL/"

run_production_test "Low_Load_Cached" \
    "Low traffic with 60% repeat visitors" \
    "wrk -t2 -c50 -d60s -s real_world.lua $SERVER_URL/"

run_production_test "Medium_Load_Baseline" \
    "Medium traffic (200 concurrent users)" \
    "wrk -t4 -c200 -d60s $SERVER_URL/"

run_production_test "Medium_Load_Cached" \
    "Medium traffic with realistic caching" \
    "wrk -t4 -c200 -d60s -s real_world.lua $SERVER_URL/"

run_production_test "High_Load_Baseline" \
    "High traffic peak (500 concurrent users)" \
    "wrk -t8 -c500 -d30s $SERVER_URL/"

run_production_test "High_Load_Cached" \
    "High traffic with heavy caching (80%)" \
    "wrk -t8 -c500 -d30s -s heavy_cache.lua $SERVER_URL/"

echo -e "${BLUE}3. Stress Testing${NC}"
echo "========================================"

run_production_test "Spike_Test" \
    "Traffic spike simulation (1000 users, 15s)" \
    "wrk -t12 -c1000 -d15s -s mixed_cache.lua $SERVER_URL/"

run_production_test "Sustained_Load" \
    "Sustained high load (300 users, 2 minutes)" \
    "wrk -t6 -c300 -d120s -s real_world.lua $SERVER_URL/"

echo -e "${BLUE}4. Edge Cases${NC}"
echo "========================================"

run_production_test "Mobile_Simulation" \
    "Mobile clients (lower concurrency, slower connections)" \
    "wrk -t2 -c30 -d30s --timeout 10s -s real_world.lua $SERVER_URL/"

run_production_test "API_Heavy" \
    "Mixed static/dynamic content (simulated API calls)" \
    "wrk -t4 -c100 -d30s -s real_world.lua $SERVER_URL/"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}      Production Testing Complete       ${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

echo -e "${YELLOW}Summary & Recommendations:${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Key Metrics to Monitor:"
echo "  • Throughput > 50k RPS for medium load"
echo "  • Latency 99th percentile < 50ms"
echo "  • Zero connection errors under normal load"
echo "  • Cache hit ratio > 60% for repeat traffic"
echo ""
echo "Production Readiness Checklist:"
echo "  ☐ Monitor memory usage during sustained load"
echo "  ☐ Check file descriptor limits"
echo "  ☐ Verify log rotation is configured"
echo "  ☐ Test graceful shutdown under load"
echo "  ☐ Configure appropriate cache timeouts"
echo ""
echo -e "${BLUE}For detailed analysis, check server logs:${NC}"
echo "  tail -f build/logs/nxlite.log"
echo "" 