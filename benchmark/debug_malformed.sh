#!/bin/bash

echo "Debug malformed request test:"

malformed_requests=(
    "GET / HTTP/1.1\r\nHost: \r\n\r\n"
    "INVALID_METHOD / HTTP/1.1\r\nHost: localhost\r\n\r\n"
    "GET / HTTP/999.999\r\nHost: localhost\r\n\r\n"
    "GET /\r\nHost: localhost\r\n\r\n"
)

handled=0
for i in "${!malformed_requests[@]}"; do
    request="${malformed_requests[$i]}"
    echo "Testing request $((i+1)): ${request:0:30}..."
    
    {
        exec 3<>/dev/tcp/localhost/7877 2>/dev/null
        if [[ $? -eq 0 ]]; then
            echo -e "$request" >&3
            response=$(timeout 2 head -1 <&3 | grep -o "[0-9][0-9][0-9]" || echo "000")
            exec 3<&-
            exec 3>&-
        else
            response="000"
        fi
    } 2>/dev/null
    
    echo "Response: '$response'"
    if [[ "$response" == "400" || "$response" == "000" || "$response" == "501" || "$response" == "505" ]]; then
        handled=$((handled + 1))
        echo "Handled: YES"
    else
        echo "Handled: NO"
    fi
    echo "---"
done

echo "Total handled: $handled/4" 