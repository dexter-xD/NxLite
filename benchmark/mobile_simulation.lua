-- Mobile client simulation with caching patterns
local etags = {}
local request_count = 0

local mobile_files = {
    "/index.html",
    "/style.css",
    "/script.js",
    "/images/logo.png"
}

local mobile_agents = {
    "Mozilla/5.0 (iPhone; CPU iPhone OS 14_7_1 like Mac OS X) AppleWebKit/605.1.15",
    "Mozilla/5.0 (Android 11; Mobile; rv:68.0) Gecko/68.0 Firefox/88.0",
    "Mozilla/5.0 (Linux; Android 11; SM-G991B) AppleWebKit/537.36",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 15_0 like Mac OS X) AppleWebKit/605.1.15"
}

request = function()
    request_count = request_count + 1
    
    local file = mobile_files[((request_count - 1) % #mobile_files) + 1]
    local headers = {}
    
    headers["User-Agent"] = mobile_agents[math.random(#mobile_agents)]
    
    if request_count % 10 <= 7 and etags[file] then
        headers["If-None-Match"] = etags[file]
    end
    
    headers["Accept-Encoding"] = "gzip, deflate"
    
    return wrk.format("GET", file, headers)
end

response = function(status, headers, body)
    if headers["ETag"] then
        for _, file in ipairs(mobile_files) do
            etags[file] = headers["ETag"]
        end
    end
end 