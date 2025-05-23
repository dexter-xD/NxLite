#!/bin/bash

# Comprehensive NxLite Performance Testing Script
# Tests edge caching, different file types, and real-world scenarios

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

SERVER_URL="http://localhost:7877"
TEST_DURATION="30s"
THREADS=4
CONNECTIONS=100

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}     NxLite Comprehensive Testing     ${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

run_test() {
    local test_name="$1"
    local test_cmd="$2"
    local description="$3"
    
    echo -e "${YELLOW}Running: $test_name${NC}"
    echo -e "Description: $description"
    echo -e "Command: $test_cmd"
    echo ""
    
    local output=$(eval $test_cmd 2>&1)
    
    local rps=$(echo "$output" | grep "Requests/sec:" | awk '{print $2}')
    local transfer=$(echo "$output" | grep "Transfer/sec:" | awk '{print $2$3}')
    local latency_avg=$(echo "$output" | grep "Latency" | awk '{print $2}')
    local latency_max=$(echo "$output" | grep "Latency" | awk '{print $4}')
    local total_requests=$(echo "$output" | grep "requests in" | awk '{print $1}')
    local total_transfer=$(echo "$output" | grep "read$" | awk '{print $5$6}')
    
    echo -e "${GREEN}Results:${NC}"
    echo "  Requests/sec: $rps"
    echo "  Transfer/sec: $transfer"
    echo "  Avg Latency: $latency_avg"
    echo "  Max Latency: $latency_max"
    echo "  Total Requests: $total_requests"
    echo "  Total Transfer: $total_transfer"
    echo ""
    echo "----------------------------------------"
    echo ""
    
    echo "$test_name,$rps,$transfer,$latency_avg,$latency_max,$total_requests,$total_transfer" >> test_results.csv
}

echo "Test,RPS,Transfer/sec,Avg_Latency,Max_Latency,Total_Requests,Total_Transfer" > test_results.csv

echo -e "${BLUE}1. Baseline Performance Tests${NC}"
echo "=============================================="

run_test "Baseline_GET" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION $SERVER_URL/" \
    "Basic GET requests without caching headers"


echo -e "${BLUE}2. Edge Caching Tests${NC}"
echo "=============================================="

run_test "Cache_Mixed_33pct" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -s mixed_cache.lua $SERVER_URL/" \
    "33% conditional requests (every 3rd request)"

run_test "Cache_Mixed_50pct" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -s mixed_cache_50.lua $SERVER_URL/" \
    "50% conditional requests (every 2nd request)"

run_test "Cache_Heavy_80pct" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -s heavy_cache.lua $SERVER_URL/" \
    "80% conditional requests (realistic repeat visitors)"

echo -e "${BLUE}3. Different File Types${NC}"
echo "=============================================="

run_test "HTML_Files" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -s html_files.lua $SERVER_URL/" \
    "Various HTML files with caching"

run_test "Static_Assets" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -s static_assets.lua $SERVER_URL/" \
    "CSS, JS, images with long-term caching"

echo -e "${BLUE}4. Compression Tests${NC}"
echo "=============================================="

run_test "Compression_Gzip" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -H 'Accept-Encoding: gzip' $SERVER_URL/" \
    "Gzip compression enabled"

run_test "Compression_Mixed" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -s compression_mix.lua $SERVER_URL/" \
    "Mixed compression support simulation"

echo -e "${BLUE}5. Real-World Simulation${NC}"
echo "=============================================="

run_test "RealWorld_Burst" \
    "wrk -t8 -c200 -d60s -s real_world.lua $SERVER_URL/" \
    "High concurrency, varied requests, 60s test"

run_test "RealWorld_Mobile" \
    "wrk -t2 -c50 -d30s -s mobile_simulation.lua $SERVER_URL/" \
    "Mobile client simulation with slower connections"

echo -e "${BLUE}6. Stress Tests${NC}"
echo "=============================================="

run_test "Stress_High_Concurrency" \
    "wrk -t8 -c500 -d30s -s mixed_cache.lua $SERVER_URL/" \
    "High concurrency stress test"

run_test "Stress_Keep_Alive" \
    "wrk -t$THREADS -c$CONNECTIONS -d$TEST_DURATION -H 'Connection: keep-alive' -s mixed_cache.lua $SERVER_URL/" \
    "Keep-alive connections with caching"

echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}         Testing Complete!           ${NC}"
echo -e "${GREEN}======================================${NC}"
echo ""

echo -e "${YELLOW}Generating Summary Report...${NC}"
echo ""

echo -e "${BLUE}Performance Summary:${NC}"
echo "===================="
echo ""

best_rps=$(tail -n +2 test_results.csv | sort -t, -k2 -nr | head -1)
worst_rps=$(tail -n +2 test_results.csv | sort -t, -k2 -n | head -1)

echo "Best RPS: $(echo $best_rps | cut -d, -f1) - $(echo $best_rps | cut -d, -f2) req/sec"
echo "Worst RPS: $(echo $worst_rps | cut -d, -f1) - $(echo $worst_rps | cut -d, -f2) req/sec"
echo ""

baseline_rps=$(grep "Baseline_GET" test_results.csv | cut -d, -f2)
cache_rps=$(grep "Cache_Mixed_33pct" test_results.csv | cut -d, -f2)

if [[ -n "$baseline_rps" && -n "$cache_rps" ]]; then
    improvement=$(awk "BEGIN {printf \"%.1f\", ($cache_rps - $baseline_rps) * 100 / $baseline_rps}")
    echo -e "${GREEN}Cache Effectiveness: ${improvement}% improvement${NC}"
fi

echo ""
echo "Detailed results saved to: test_results.csv"
echo ""
echo "To analyze results:"
echo "  cat test_results.csv | column -t -s,"
echo ""
echo -e "${BLUE}Testing completed successfully!${NC}" 