-- Static assets testing (CSS, JS, images) with long-term caching
local etags = {}
local request_count = 0

local assets = {
    {path = "/style.css", cache_rate = 90},
    {path = "/script.js", cache_rate = 90},
    {path = "/logo.png", cache_rate = 95},
    {path = "/favicon.ico", cache_rate = 98},
    {path = "/fonts/main.woff", cache_rate = 95}
}

request = function()
    request_count = request_count + 1
    
    local asset = assets[((request_count - 1) % #assets) + 1]
    
    if math.random(100) <= asset.cache_rate and etags[asset.path] then
        return wrk.format("GET", asset.path, {["If-None-Match"] = etags[asset.path]})
    else
        return wrk.format("GET", asset.path)
    end
end

response = function(status, headers, body)
    if headers["ETag"] then
        for _, asset in ipairs(assets) do
            etags[asset.path] = headers["ETag"]
        end
    end
end 