#!/bin/bash

# NxLite DoS Protection Testing Script
# Tests rate limiting, connection limits, and DoS attack mitigation

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
NC='\033[0m'

SERVER_URL="http://localhost:7877"
SERVER_HOST="localhost"
SERVER_PORT="7877"
RESULTS_FILE="dos_protection_results.txt"
TEMP_DIR="dos_test_temp"

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

print_header() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}   NxLite DoS Protection Test   ${NC}"
    echo -e "${BLUE}================================${NC}"
    echo ""
}

print_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
}

print_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    PASSED_TESTS=$((PASSED_TESTS + 1))
}

print_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    FAILED_TESTS=$((FAILED_TESTS + 1))
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

check_server() {
    print_info "Checking if server is running..."
    if ! curl -s "$SERVER_URL" > /dev/null 2>&1; then
        echo -e "${RED}Error: Server is not running at $SERVER_URL${NC}"
        echo "Please start the server first: ./NxLite"
        exit 1
    fi
    print_info "Server is running ✓"
    echo ""
}

check_dev_mode() {
    print_info "Checking server mode..."
    
    local rapid_requests=0
    local blocked_requests=0
    for i in {1..120}; do
        response=$(timeout 1 curl -s -o /dev/null -w "%{http_code}" --max-time 1 "$SERVER_URL/" 2>/dev/null || echo "000")
        if [[ "$response" == "200" ]]; then
            rapid_requests=$((rapid_requests + 1))
        elif [[ "$response" == "000" || "$response" == "429" || "$response" == "503" ]]; then
            blocked_requests=$((blocked_requests + 1))
        fi
    done
    
    if [[ $rapid_requests -gt 100 ]]; then
        print_warning "Server appears to be in DEVELOPMENT MODE"
        print_warning "DoS protection may be disabled!"
        echo "Development mode detected - some tests may not work as expected" >> "$RESULTS_FILE"
        return 1
    else
        print_info "Server appears to be in PRODUCTION MODE ✓"
        print_info "Rate limiting active: $blocked_requests requests blocked"
        echo "Production mode detected - DoS protection active" >> "$RESULTS_FILE"
        return 0
    fi
}

setup_test_env() {
    print_info "Setting up test environment..."
    mkdir -p "$TEMP_DIR"
    echo "" > "$RESULTS_FILE"
    print_info "Test environment ready ✓"
    echo ""
}

test_rate_limiting() {
    print_test "Rate Limiting Protection"
    
    print_info "Sending rapid requests to trigger rate limiting..."
    
    local success_count=0
    local blocked_count=0
    local total_requests=150
    
    for i in $(seq 1 $total_requests); do
        response=$(timeout 1 curl -s -o /dev/null -w "%{http_code}" --max-time 1 "$SERVER_URL/" 2>/dev/null || echo "000")
        
        if [[ "$response" == "200" ]]; then
            success_count=$((success_count + 1))
        elif [[ "$response" == "429" || "$response" == "503" || "$response" == "000" ]]; then
            blocked_count=$((blocked_count + 1))
        fi
        
    done
    
    local block_percentage=$((blocked_count * 100 / total_requests))
    
    if [[ $block_percentage -gt 50 ]]; then
        print_pass "Rate limiting active ($blocked_count/$total_requests blocked, ${block_percentage}%)"
        echo "Rate Limiting: PASS - $blocked_count/$total_requests blocked (${block_percentage}%)" >> "$RESULTS_FILE"
    elif [[ $blocked_count -gt 0 ]]; then
        print_pass "Rate limiting partially active ($blocked_count/$total_requests blocked, ${block_percentage}%)"
        echo "Rate Limiting: PASS - $blocked_count/$total_requests blocked (${block_percentage}%)" >> "$RESULTS_FILE"
    else
        print_fail "No rate limiting detected ($success_count/$total_requests succeeded)"
        echo "Rate Limiting: FAIL - No blocking detected" >> "$RESULTS_FILE"
    fi
    
    print_info "Waiting for rate limit reset..."
    sleep 5
}

test_connection_flooding() {
    print_test "Connection Flooding Protection"
    
    print_info "Attempting to flood server with connections..."
    
    local max_connections=50
    local successful_connections=0
    local pids=()
    
    for i in $(seq 1 $max_connections); do
        {
            exec 3<>/dev/tcp/$SERVER_HOST/$SERVER_PORT 2>/dev/null
            if [[ $? -eq 0 ]]; then
                echo "GET / HTTP/1.1" >&3
                echo "Host: $SERVER_HOST" >&3
                echo "Connection: keep-alive" >&3
                echo "" >&3
                sleep 2
                exec 3<&-
                exec 3>&-
                echo "success"
            else
                echo "failed"
            fi
        } > "$TEMP_DIR/conn_$i.log" 2>&1 &
        pids+=($!)
    done
    
    sleep 3
    
    for pid in "${pids[@]}"; do
        wait $pid 2>/dev/null || true
    done
    
    successful_connections=$(grep -l "success" "$TEMP_DIR"/conn_*.log 2>/dev/null | wc -l)
    
    if [[ $successful_connections -lt $max_connections ]]; then
        local blocked=$((max_connections - successful_connections))
        print_pass "Connection flooding limited ($blocked/$max_connections blocked)"
        echo "Connection Flooding: PASS - $blocked/$max_connections blocked" >> "$RESULTS_FILE"
    else
        print_fail "All connections accepted ($successful_connections/$max_connections)"
        echo "Connection Flooding: FAIL - All connections accepted" >> "$RESULTS_FILE"
    fi
    
    # Cleanup
    rm -f "$TEMP_DIR"/conn_*.log
    sleep 2
}

test_slow_loris() {
    print_test "Slow Loris Attack Protection"
    
    print_info "Simulating slow loris attack..."
    
    local slow_connections=10
    local pids=()
    local successful_attacks=0
    
    for i in $(seq 1 $slow_connections); do
        {
            exec 3<>/dev/tcp/$SERVER_HOST/$SERVER_PORT 2>/dev/null
            if [[ $? -eq 0 ]]; then
                echo -n "GET / HTTP/1.1" >&3
                sleep 1
                echo -n $'\r\n' >&3
                sleep 1
                echo -n "Host: " >&3
                sleep 1
                echo -n "$SERVER_HOST" >&3
                sleep 1
                echo -n $'\r\n' >&3
                sleep 5
                
                echo $'\r\n' >&3
                read -t 2 response <&3
                if [[ $? -eq 0 ]]; then
                    echo "completed"
                else
                    echo "timeout"
                fi
                exec 3<&-
                exec 3>&-
            else
                echo "failed"
            fi
        } > "$TEMP_DIR/slow_$i.log" 2>&1 &
        pids+=($!)
    done
    
    sleep 12
    
    for pid in "${pids[@]}"; do
        kill $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
    done
    
    successful_attacks=$(grep -l "completed" "$TEMP_DIR"/slow_*.log 2>/dev/null | wc -l)
    
    if [[ $successful_attacks -lt $slow_connections ]]; then
        local blocked=$((slow_connections - successful_attacks))
        print_pass "Slow loris attack mitigated ($blocked/$slow_connections blocked)"
        echo "Slow Loris: PASS - $blocked/$slow_connections blocked" >> "$RESULTS_FILE"
    else
        print_fail "Slow loris attack succeeded ($successful_attacks/$slow_connections)"
        echo "Slow Loris: FAIL - $successful_attacks/$slow_connections succeeded" >> "$RESULTS_FILE"
    fi
    
    rm -f "$TEMP_DIR"/slow_*.log
    sleep 2
}

test_large_requests() {
    print_test "Large Request Attack Protection"
    
    print_info "Sending oversized requests..."
    
    local large_requests=5
    local blocked=0
    
    for size in 100000 500000 1000000 5000000 10000000; do
        print_info "Testing ${size} byte request..."
        
        large_data=$(python3 -c "print('A' * $size)" 2>/dev/null || perl -E "say 'A' x $size")
        
        response=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
                   --data "$large_data" \
                   --max-time 10 \
                   "$SERVER_URL/" 2>/dev/null || echo "000")
        
        if [[ "$response" == "413" || "$response" == "400" || "$response" == "414" || "$response" == "000" ]]; then
            blocked=$((blocked + 1))
        fi
    done
    
    if [[ $blocked -eq $large_requests ]]; then
        print_pass "Large requests blocked ($blocked/$large_requests)"
        echo "Large Requests: PASS - All $blocked requests blocked" >> "$RESULTS_FILE"
    else
        print_fail "Some large requests accepted ($blocked/$large_requests blocked)"
        echo "Large Requests: FAIL - $blocked/$large_requests blocked" >> "$RESULTS_FILE"
    fi
}

test_concurrent_bombing() {
    print_test "Concurrent Request Bombing"
    
    print_info "Launching concurrent request bomb..."
    
    local concurrent_requests=100
    local pids=()
    local responses_file="$TEMP_DIR/responses.txt"
    echo "" > "$responses_file"
    
    for i in $(seq 1 $concurrent_requests); do
        {
            response=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$SERVER_URL/" 2>/dev/null || echo "000")
            echo "$response" >> "$responses_file"
        } &
        pids+=($!)
    done
    
    for pid in "${pids[@]}"; do
        wait $pid 2>/dev/null || true
    done
    
    local success_count=$(grep -c "200" "$responses_file" 2>/dev/null || echo "0")
    local error_count=$(grep -c -E "(429|503|000)" "$responses_file" 2>/dev/null || echo "0")
    local total_responses=$(wc -l < "$responses_file")
    
    if [[ $error_count -gt 0 ]]; then
        local block_percentage=$((error_count * 100 / total_responses))
        print_pass "Concurrent bombing limited ($error_count/$total_responses blocked, ${block_percentage}%)"
        echo "Concurrent Bombing: PASS - $error_count/$total_responses blocked (${block_percentage}%)" >> "$RESULTS_FILE"
    else
        print_fail "All concurrent requests succeeded ($success_count/$total_responses)"
        echo "Concurrent Bombing: FAIL - No blocking detected" >> "$RESULTS_FILE"
    fi
    
    rm -f "$responses_file"
    sleep 3
}

test_ip_rate_limiting() {
    print_test "IP-based Rate Limiting"
    
    print_info "Testing per-IP rate limiting..."
    
    local localhost_blocked=0
    local localhost_success=0
    for i in {1..50}; do
        response=$(timeout 1 curl -s -o /dev/null -w "%{http_code}" --max-time 1 "$SERVER_URL/" 2>/dev/null || echo "000")
        if [[ "$response" == "200" ]]; then
            localhost_success=$((localhost_success + 1))
        elif [[ "$response" == "429" || "$response" == "503" || "$response" == "000" ]]; then
            localhost_blocked=$((localhost_blocked + 1))
        fi
    done
    
    local block_percentage=$((localhost_blocked * 100 / 50))
    
    if [[ $block_percentage -gt 30 ]]; then
        print_pass "IP-based rate limiting active ($localhost_blocked/50 blocked, ${block_percentage}%)"
        echo "IP Rate Limiting: PASS - $localhost_blocked/50 blocked (${block_percentage}%)" >> "$RESULTS_FILE"
    elif [[ $localhost_blocked -gt 0 ]]; then
        print_pass "IP-based rate limiting partially active ($localhost_blocked/50 blocked, ${block_percentage}%)"
        echo "IP Rate Limiting: PASS - $localhost_blocked/50 blocked (${block_percentage}%)" >> "$RESULTS_FILE"
    else
        print_fail "No IP-based rate limiting detected"
        echo "IP Rate Limiting: FAIL - No blocking detected" >> "$RESULTS_FILE"
    fi
    
    sleep 3
}

test_malformed_flooding() {
    print_test "Malformed Request Flooding"
    
    print_info "Flooding with malformed requests..."
    
    local malformed_requests=(
        "INVALID_METHOD / HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET / HTTP/999.999\r\nHost: localhost\r\n\r\n"
        "GET /\r\nHost: \r\n\r\n"
        "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n"
        "GET / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 999999999\r\n\r\n"
    )
    
    local handled=0
    local total_malformed=$((${#malformed_requests[@]} * 10))  # Send each 10 times
    
    for request in "${malformed_requests[@]}"; do
        for i in {1..10}; do
            {
                exec 3<>/dev/tcp/localhost/7877 2>/dev/null
                if [[ $? -eq 0 ]]; then
                    echo -e "$request" >&3
                    response=$(timeout 1 head -1 <&3 | grep -o "[0-9][0-9][0-9]" || echo "000")
                    exec 3<&-
                    exec 3>&-
                else
                    response="000"
                fi
            } 2>/dev/null
            
            if [[ "$response" == "400" || "$response" == "000" ]]; then
                handled=$((handled + 1))
            fi
        done
    done
    
    if [[ $handled -gt $((total_malformed / 2)) ]]; then
        print_pass "Malformed request flooding handled ($handled/$total_malformed)"
        echo "Malformed Flooding: PASS - $handled/$total_malformed handled" >> "$RESULTS_FILE"
    else
        print_fail "Malformed request flooding not handled ($handled/$total_malformed)"
        echo "Malformed Flooding: FAIL - $handled/$total_malformed handled" >> "$RESULTS_FILE"
    fi
}

test_resource_exhaustion() {
    print_test "Resource Exhaustion Protection"
    
    print_info "Testing resource exhaustion scenarios..."
    
    local keepalive_pids=()
    local max_keepalive=20
    
    for i in $(seq 1 $max_keepalive); do
        {
            curl -s --keepalive-time 30 --max-time 10 "$SERVER_URL/" > /dev/null 2>&1
        } &
        keepalive_pids+=($!)
    done
    
    sleep 2
    response=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$SERVER_URL/" 2>/dev/null || echo "000")
    
    for pid in "${keepalive_pids[@]}"; do
        kill $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
    done
    
    if [[ "$response" == "200" ]]; then
        print_pass "Server remains responsive under resource pressure"
        echo "Resource Exhaustion: PASS - Server responsive" >> "$RESULTS_FILE"
    else
        print_fail "Server unresponsive under resource pressure (response: $response)"
        echo "Resource Exhaustion: FAIL - Server unresponsive" >> "$RESULTS_FILE"
    fi
}

test_bandwidth_exhaustion() {
    print_test "Bandwidth Exhaustion Protection"
    
    print_info "Testing bandwidth exhaustion..."
    
    local bandwidth_pids=()
    local concurrent_downloads=10
    
    for i in $(seq 1 $concurrent_downloads); do
        {
            curl -s --max-time 5 "$SERVER_URL/" > /dev/null 2>&1
        } &
        bandwidth_pids+=($!)
    done
    
    sleep 1
    start_time=$(date +%s%N)
    response=$(curl -s -o /dev/null -w "%{http_code}" --max-time 10 "$SERVER_URL/" 2>/dev/null || echo "000")
    end_time=$(date +%s%N)
    response_time=$(((end_time - start_time) / 1000000))  # Convert to milliseconds
    
    for pid in "${bandwidth_pids[@]}"; do
        kill $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
    done
    
    if [[ "$response" == "200" && $response_time -lt 5000 ]]; then
        print_pass "Server handles bandwidth pressure well (${response_time}ms response)"
        echo "Bandwidth Exhaustion: PASS - Response time ${response_time}ms" >> "$RESULTS_FILE"
    else
        print_fail "Server struggles with bandwidth pressure (${response_time}ms, code: $response)"
        echo "Bandwidth Exhaustion: FAIL - Response time ${response_time}ms" >> "$RESULTS_FILE"
    fi
}

cleanup() {
    print_info "Cleaning up test environment..."
    rm -rf "$TEMP_DIR"
    print_info "Cleanup complete ✓"
}

print_summary() {
    echo ""
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}        Test Summary            ${NC}"
    echo -e "${BLUE}================================${NC}"
    echo -e "Total Tests: $TOTAL_TESTS"
    echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
    
    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "${GREEN}All DoS protection tests passed! ✓${NC}"
    else
        echo -e "${YELLOW}Some tests failed. Check results for details.${NC}"
    fi
    
    echo ""
    echo "Detailed results saved to: $RESULTS_FILE"
    echo ""
}

main() {
    print_header
    check_server
    
    if check_dev_mode; then
        echo ""
    else
        echo ""
        print_warning "Running tests anyway, but results may not be accurate in dev mode"
        echo ""
    fi
    
    setup_test_env
    
    echo "Starting DoS protection tests..."
    echo ""
    
    test_rate_limiting
    test_connection_flooding
    test_slow_loris
    test_large_requests
    test_concurrent_bombing
    test_ip_rate_limiting
    test_malformed_flooding
    test_resource_exhaustion
    test_bandwidth_exhaustion
    
    cleanup
    print_summary
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi 