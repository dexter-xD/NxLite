#!/bin/bash
   
# First request - get the ETag
ETAG=$(curl -s -I http://localhost:7877/index.html | grep ETag | awk '{print $2}' | tr -d '\r')
echo "ETag: $ETAG"

# Compare with regular requests
echo "Running benchmark with regular requests..."
ab -n 100000 -c 1000 http://localhost:7877/index.html

# Run benchmark with ab using the ETag
echo "Running benchmark with conditional requests..."
ab -n 100000 -c 1000 -H "If-None-Match: $ETAG" http://localhost:7877/index.html

# Run benchmark with wrk regular requests
echo "Running benchmark with wrk regular requests..."
wrk -t4 -c100 -d30s http://localhost:7877/

# Run benchmark with wrk using the ETag where every 3rd request is conditional (simulating repeat visitors)
echo "Running benchmark with wrk... (every 3rd request is conditional)"
wrk -t4 -c100 -d30s -s mixed_cache.lua http://localhost:7877/

# Run benchmark with hey regular requests
echo "Running benchmark with hey regular requests..."
hey -n 100000 -c 1000 http://localhost:7877/index.html

# Run benchmark with hey
echo "Running benchmark with hey..."
hey -n 100000 -c 1000 -H "If-None-Match: $ETAG" http://localhost:7877/index.html

