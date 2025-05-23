-- Mixed compression testing with caching
local etags = {}
local request_count = 0

local encodings = {
    "gzip, deflate, br",
    "gzip, deflate", 
    "gzip",
    "deflate",
    "identity",
    "*",
    ""
}

request = function()
    request_count = request_count + 1
    local url_path = "/index.html"
    local headers = {}
    
    local encoding = encodings[((request_count - 1) % #encodings) + 1]
    if encoding ~= "" then
        headers["Accept-Encoding"] = encoding
    end
    
    if request_count % 5 <= 2 and etags[url_path] then
        headers["If-None-Match"] = etags[url_path]
    end
    
    return wrk.format("GET", url_path, headers)
end

response = function(status, headers, body)
    if headers["ETag"] then
        local url_path = "/index.html"
        etags[url_path] = headers["ETag"]
    end
end 