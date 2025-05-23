-- Real-world simulation: mixed file types, caching, and user patterns
local etags = {}
local request_count = 0

local files = {
    {path = "/index.html", weight = 40, cacheable = true},
    {path = "/style.css", weight = 15, cacheable = true},
    {path = "/script.js", weight = 15, cacheable = true},
    {path = "/logo.png", weight = 10, cacheable = true},
    {path = "/api/data", weight = 10, cacheable = false},
    {path = "/about.html", weight = 5, cacheable = true},
    {path = "/contact.html", weight = 3, cacheable = true},
    {path = "/favicon.ico", weight = 2, cacheable = true}
}

local weighted_files = {}
for _, file in ipairs(files) do
    for i = 1, file.weight do
        table.insert(weighted_files, file)
    end
end

local encodings = {
    "gzip, deflate, br",
    "gzip, deflate",
    "gzip",
    "identity",
    "*"
}

local user_agents = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 14_7_1 like Mac OS X)"
}

request = function()
    request_count = request_count + 1
    
    local file = weighted_files[math.random(#weighted_files)]
    local headers = {}
    
    headers["Accept-Encoding"] = encodings[math.random(#encodings)]
    
    if math.random(10) <= 3 then
        headers["User-Agent"] = user_agents[math.random(#user_agents)]
    end
    
    if file.cacheable and etags[file.path] and math.random(100) <= 60 then
        headers["If-None-Match"] = etags[file.path]
    end
    
    return wrk.format("GET", file.path, headers)
end

response = function(status, headers, body)
    if headers["ETag"] then
        for _, file in ipairs(files) do
            etags[file.path] = headers["ETag"]
        end
    end
end 