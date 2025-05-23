-- 50% conditional requests (every 2nd request)
local etags = {}
local request_count = 0

request = function()
    request_count = request_count + 1
    local url_path = "/index.html"
    
    if request_count % 2 == 0 and etags[url_path] then
        return wrk.format("GET", url_path, {["If-None-Match"] = etags[url_path]})
    else
        return wrk.format("GET", url_path)
    end
end

response = function(status, headers, body)
    if headers["ETag"] then
        local url_path = "/index.html"
        etags[url_path] = headers["ETag"]
    end
end 