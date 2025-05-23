#!/bin/bash

# Quick NxLite Performance Test
# Fast validation of core functionality

set -e

SERVER_URL="http://localhost:7877"
BLUE='\033[0;34m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}Quick NxLite Performance Test${NC}"
echo "============================="
echo ""

# Test 1: Baseline
echo -e "${YELLOW}1. Baseline Performance${NC}"
baseline_result=$(wrk -t4 -c100 -d10s $SERVER_URL/ 2>&1)
baseline_rps=$(echo "$baseline_result" | grep "Requests/sec:" | awk '{print $2}')
echo "   Baseline RPS: $baseline_rps"
echo ""

# Test 2: Edge Caching
echo -e "${YELLOW}2. Edge Caching (33% conditional)${NC}"
cache_result=$(wrk -t4 -c100 -d10s -s mixed_cache.lua $SERVER_URL/ 2>&1)
cache_rps=$(echo "$cache_result" | grep "Requests/sec:" | awk '{print $2}')
echo "   Cached RPS: $cache_rps"
echo ""

# Test 3: Heavy Caching
echo -e "${YELLOW}3. Heavy Caching (80% conditional)${NC}"
heavy_result=$(wrk -t4 -c100 -d10s -s heavy_cache.lua $SERVER_URL/ 2>&1)
heavy_rps=$(echo "$heavy_result" | grep "Requests/sec:" | awk '{print $2}')
echo "   Heavy Cache RPS: $heavy_rps"
echo ""

# Calculate improvements
if [[ -n "$baseline_rps" && -n "$cache_rps" ]]; then
    improvement=$(awk "BEGIN {printf \"%.1f\", ($cache_rps - $baseline_rps) * 100 / $baseline_rps}")
    echo -e "${GREEN}Cache Improvement: ${improvement}%${NC}"
fi

if [[ -n "$baseline_rps" && -n "$heavy_rps" ]]; then
    heavy_improvement=$(awk "BEGIN {printf \"%.1f\", ($heavy_rps - $baseline_rps) * 100 / $baseline_rps}")
    echo -e "${GREEN}Heavy Cache Improvement: ${heavy_improvement}%${NC}"
fi

echo ""
echo -e "${BLUE}Quick test completed!${NC}"
echo "Run './comprehensive_test.sh' for detailed analysis." 