-- Various HTML files testing with caching
local etags = {}
local request_count = 0

local html_files = {
    "/index.html",
    "/about.html", 
    "/contact.html",
    "/services.html",
    "/blog.html"
}

request = function()
    request_count = request_count + 1
    
    local file = html_files[((request_count - 1) % #html_files) + 1]
    
    if request_count % 2 == 0 and etags[file] then
        return wrk.format("GET", file, {["If-None-Match"] = etags[file]})
    else
        return wrk.format("GET", file)
    end
end

response = function(status, headers, body)
    if headers["ETag"] then
        for _, file in ipairs(html_files) do
            etags[file] = headers["ETag"]
        end
    end
end 