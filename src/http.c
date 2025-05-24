#include "http.h"
#include <pthread.h>


static const struct {
    int code;
    const char *text;
} status_messages[] = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {505, "HTTP Version Not Supported"},
    {0, NULL}
};

static const struct {
    const char *ext;
    const char *type;
} mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
    {".txt", "text/plain"},
    {".pdf", "application/pdf"},
    {NULL, "application/octet-stream"}
};

#define CACHE_SIZE 10000    
#define CACHE_TIMEOUT 3600      

static char header_buffer[8192];

typedef struct {
    unsigned long cache_hits;
    unsigned long cache_misses;
    unsigned long cache_evictions;
    unsigned long cache_allocations;
    unsigned long cache_frees;
    size_t total_memory_used;
    size_t max_memory_used;
    time_t last_cleanup_time;
} cache_stats_t;

static cache_stats_t cache_stats = {0};

static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int hash_key(const char *key) {
    unsigned int hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % CACHE_SIZE;
}

typedef struct {
    char path[PATH_MAX];
    char *response;
    size_t response_len;
    time_t timestamp;
    char vary_key[256];
    char etag[64];
} cache_entry_t;

static cache_entry_t response_cache[CACHE_SIZE];
static int cache_index = 0;

static void update_cache_stats_on_allocation(size_t size) {
    cache_stats.cache_allocations++;
    cache_stats.total_memory_used += size;
    if (cache_stats.total_memory_used > cache_stats.max_memory_used) {
        cache_stats.max_memory_used = cache_stats.total_memory_used;
    }
}

static void update_cache_stats_on_free(size_t size) {
    cache_stats.cache_frees++;
    if (cache_stats.total_memory_used >= size) {
        cache_stats.total_memory_used -= size;
    } else {
        cache_stats.total_memory_used = 0;
    }
}

static void cleanup_expired_cache_entries(void) {
    time_t now = time(NULL);
    int cleaned = 0;
    
    if (now - cache_stats.last_cleanup_time < 300) {
        return;
    }
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (response_cache[i].path[0] != '\0' && 
            (now - response_cache[i].timestamp >= CACHE_TIMEOUT)) {
            
            if (response_cache[i].response) {
                update_cache_stats_on_free(response_cache[i].response_len);
                free(response_cache[i].response);
                response_cache[i].response = NULL;
            }
            
            memset(&response_cache[i], 0, sizeof(cache_entry_t));
            cleaned++;
            cache_stats.cache_evictions++;
        }
    }
    
    cache_stats.last_cleanup_time = now;
    if (cleaned > 0) {
        LOG_DEBUG("Cleaned up %d expired cache entries, total memory: %zu bytes", 
                  cleaned, cache_stats.total_memory_used);
    }
}

static void generate_vary_key(const char *path, const http_request_t *request, char *key, size_t key_size) {
    if (!request) {
        strncpy(key, path, key_size - 1);
        key[key_size - 1] = '\0';
        return;
    }
    
    snprintf(key, key_size, "%s:", path);
    
    for (int i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i][0], "Accept-Encoding") == 0) {
            size_t current_len = strlen(key);
            const char *encoding = request->headers[i][1];
            if (strstr(encoding, "gzip")) {
                snprintf(key + current_len, key_size - current_len, "gzip");
            } else if (strstr(encoding, "deflate")) {
                snprintf(key + current_len, key_size - current_len, "deflate");
            } else {
                snprintf(key + current_len, key_size - current_len, "none");
            }
            break;
        }
    }
    
    if (strchr(key, ':') && key[strlen(key)-1] == ':') {
        size_t current_len = strlen(key);
        if (current_len + 4 < key_size) { 
            snprintf(key + current_len, key_size - current_len, "none");
        }
    }
}

static cache_entry_t *find_cached_response(const char *path, const http_request_t *request) {
    char vary_key[256];
    generate_vary_key(path, request, vary_key, sizeof(vary_key));
    
    LOG_DEBUG("Cache lookup: path='%s', vary_key='%s'", path, vary_key);
    
    pthread_mutex_lock(&cache_mutex);
    
    cleanup_expired_cache_entries();
    
    unsigned int hash_idx = hash_key(vary_key);
    if (response_cache[hash_idx].path[0] != '\0' && 
        strcmp(response_cache[hash_idx].path, path) == 0 &&
        strcmp(response_cache[hash_idx].vary_key, vary_key) == 0 &&
        time(NULL) - response_cache[hash_idx].timestamp < CACHE_TIMEOUT) {
        LOG_DEBUG("Cache hit (hash) for %s with vary key %s", path, vary_key);
        cache_entry_t *result = &response_cache[hash_idx];
        cache_stats.cache_hits++;
        pthread_mutex_unlock(&cache_mutex);
        return result;
    }
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (i == (int)hash_idx) continue;
        
        if (response_cache[i].path[0] != '\0') {
            LOG_DEBUG("Cache entry %d: path='%s', vary_key='%s', age=%ld", 
                      i, response_cache[i].path, response_cache[i].vary_key, 
                      time(NULL) - response_cache[i].timestamp);
        }
        if (response_cache[i].path[0] != '\0' && 
            strcmp(response_cache[i].path, path) == 0 &&
            strcmp(response_cache[i].vary_key, vary_key) == 0 &&
            time(NULL) - response_cache[i].timestamp < CACHE_TIMEOUT) {
            LOG_DEBUG("Cache hit (linear) for %s with vary key %s", path, vary_key);
            cache_entry_t *result = &response_cache[i];
            cache_stats.cache_hits++;
            pthread_mutex_unlock(&cache_mutex);
            return result;
        }
    }
    LOG_DEBUG("Cache miss for %s with vary key %s", path, vary_key);
    cache_stats.cache_misses++;
    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

static void cache_response(const char *path, const char *response, size_t response_len, const http_request_t *request, const char *etag) {
    char vary_key[256];
    generate_vary_key(path, request, vary_key, sizeof(vary_key));
    
    const size_t MAX_CACHE_ENTRY_SIZE = 5 * 1024 * 1024;
    if (response_len > MAX_CACHE_ENTRY_SIZE) {
        LOG_DEBUG("Response too large to cache: %zu bytes (max: %zu)", response_len, MAX_CACHE_ENTRY_SIZE);
        return;
    }
    
    pthread_mutex_lock(&cache_mutex);
    
    const size_t MAX_TOTAL_CACHE_MEMORY = 100 * 1024 * 1024; // 100MB total cache limit
    if (cache_stats.total_memory_used + response_len > MAX_TOTAL_CACHE_MEMORY) {
        LOG_DEBUG("Cache memory limit reached (%zu + %zu > %zu), triggering cleanup", 
                  cache_stats.total_memory_used, response_len, MAX_TOTAL_CACHE_MEMORY);
        cleanup_expired_cache_entries();
        
        if (cache_stats.total_memory_used + response_len > MAX_TOTAL_CACHE_MEMORY) {
            LOG_WARN("Cache memory limit exceeded even after cleanup, skipping cache for this response");
            pthread_mutex_unlock(&cache_mutex);
            return;
        }
    }
    
    unsigned int hash_idx = hash_key(vary_key);
    cache_entry_t *entry = &response_cache[hash_idx];
    
    if (entry->path[0] != '\0' && 
        (strcmp(entry->path, path) != 0 || strcmp(entry->vary_key, vary_key) != 0)) {
        entry = &response_cache[cache_index];
        cache_index = (cache_index + 1) % CACHE_SIZE;
    }
    
    if (entry->response) {
        update_cache_stats_on_free(entry->response_len);
        free(entry->response);
        entry->response = NULL;
    }
    
    memset(entry, 0, sizeof(cache_entry_t));
    
    strncpy(entry->path, path, PATH_MAX - 1);
    entry->path[PATH_MAX - 1] = '\0';
    
    strncpy(entry->vary_key, vary_key, sizeof(entry->vary_key) - 1);
    entry->vary_key[sizeof(entry->vary_key) - 1] = '\0';
    strncpy(entry->etag, etag, sizeof(entry->etag) - 1);
    entry->etag[sizeof(entry->etag) - 1] = '\0';
    
    LOG_DEBUG("Cache population: path='%s', vary_key='%s', etag='%s'", path, entry->vary_key, etag);
    
    entry->response = malloc(response_len);
    if (entry->response) {
        memcpy(entry->response, response, response_len);
        entry->response_len = response_len;
        entry->timestamp = time(NULL);
        update_cache_stats_on_allocation(response_len);
        LOG_DEBUG("Cached response for %s with vary key %s (%zu bytes, total cache memory: %zu bytes)", 
                  path, entry->vary_key, response_len, cache_stats.total_memory_used);
    } else {
        LOG_ERROR("Failed to allocate memory for cached response (%zu bytes)", response_len);
        memset(entry, 0, sizeof(cache_entry_t));
    }
    
    pthread_mutex_unlock(&cache_mutex);
}

int http_parse_request(const char *buffer, size_t length, http_request_t *request) {
    char *line_start = (char *)buffer;
    char *line_end;
    
    request->keep_alive = 0;
    
    line_end = strstr(line_start, "\r\n");
    if (!line_end) return -1;
    
    char method[16], uri[2048], version[16];
    if (sscanf(line_start, "%15s %2047s %15s", method, uri, version) != 3) {
        return -1;
    }
    
    strncpy(request->method, method, sizeof(request->method) - 1);
    strncpy(request->uri, uri, sizeof(request->uri) - 1);
    strncpy(request->version, version, sizeof(request->version) - 1);
    
    line_start = line_end + 2;
    request->header_count = 0;
    
    while (line_start < (char *)buffer + length && request->header_count < MAX_HEADERS) {
        line_end = strstr(line_start, "\r\n");
        if (!line_end) break;
        
        if (line_end == line_start) break;
        
        char *colon = strchr(line_start, ':');
        if (colon) {
            *colon = '\0';
            char *value = colon + 1;
            while (*value == ' ') value++;
            
            strncpy(request->headers[request->header_count][0], line_start, MAX_HEADER_SIZE - 1);
            strncpy(request->headers[request->header_count][1], value, MAX_HEADER_SIZE - 1);
            
            if (strcasecmp(line_start, "Connection") == 0) {
                LOG_DEBUG("Found Connection header: %s", value);
            }
            
            request->header_count++;
        }
        
        line_start = line_end + 2;
    }
    
    request->keep_alive = (strcmp(request->version, "HTTP/1.1") == 0);
    
    for (int i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i][0], "Connection") == 0) {
            if (strcasecmp(request->headers[i][1], "close") == 0) {
                request->keep_alive = 0;
                LOG_DEBUG("Connection: close header found, disabling keep-alive");
            } else if (strcasecmp(request->headers[i][1], "keep-alive") == 0) {
                request->keep_alive = 1;
                LOG_DEBUG("Connection: keep-alive header found, enabling keep-alive");
            }
            break;
        }
    }
    
    LOG_DEBUG("Request parsed: %s %s %s, keep-alive=%d", 
              request->method, request->uri, request->version, request->keep_alive);
    
    return 0;
}

void http_create_response(http_response_t *response, int status_code) {
    memset(response, 0, sizeof(http_response_t));
    response->status_code = status_code;
    response->keep_alive = 0;  
    response->file_fd = -1; 
    
    for (int i = 0; status_messages[i].code != 0; i++) {
        if (status_messages[i].code == status_code) {
            response->status_text = status_messages[i].text;
            break;
        }
    }
    
    response->compression_type = COMPRESSION_NONE;
    response->compressed_body = NULL;
    response->compressed_length = 0;
    response->compression_level = COMPRESSION_LEVEL_NONE;
    
    http_add_header(response, "Server", "NxLite");
}

void http_add_header(http_response_t *response, const char *name, const char *value) {
    if (response->header_count < MAX_HEADERS) {
        strncpy(response->headers[response->header_count][0], name, MAX_HEADER_SIZE - 1);
        strncpy(response->headers[response->header_count][1], value, MAX_HEADER_SIZE - 1);
        response->header_count++;
    }
}

const char *http_get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return mime_types[0].type;
    
    for (int i = 0; mime_types[i].ext != NULL; i++) {
        if (strcasecmp(ext, mime_types[i].ext) == 0) {
            return mime_types[i].type;
        }
    }
    
    return mime_types[0].type;
}

int http_serve_file(const char *path, http_response_t *response, const http_request_t *request) {
    char full_path[PATH_MAX];
    
    strncpy(full_path, path, PATH_MAX - 1);
    full_path[PATH_MAX - 1] = '\0';
    
    size_t path_len = strlen(full_path);
    if (full_path[path_len - 1] == '/') {
        strncat(full_path, "index.html", PATH_MAX - path_len - 1);
    }
    
    LOG_DEBUG("Serving file: %s", full_path);
    
    int file_fd = open(full_path, O_RDONLY | O_NONBLOCK);
    if (file_fd == -1) {
        LOG_WARN("Failed to open file %s: %s", full_path, strerror(errno));
        return -1;
    }
    
    struct stat st;
    if (fstat(file_fd, &st) == -1) {
        LOG_ERROR("Failed to stat file %s: %s", full_path, strerror(errno));
        close(file_fd);
        return -1;
    }
    
    if (!S_ISREG(st.st_mode)) {
        LOG_WARN("Not a regular file: %s", full_path);
        close(file_fd);
        return -1;
    }
    
    const char *mime_type = http_get_mime_type(full_path);
    http_add_header(response, "Content-Type", mime_type);
    
    int is_compressible = http_should_compress_mime_type(mime_type);
    
    if (is_compressible && response->compression_type != COMPRESSION_NONE && st.st_size <= 10 * 1024 * 1024) {
        void *file_content = malloc(st.st_size);
        if (file_content) {
            ssize_t bytes_read = pread(file_fd, file_content, st.st_size, 0);
            if (bytes_read == st.st_size) {
                response->body = file_content;
                response->body_length = st.st_size;
                response->is_file = 0;
                close(file_fd);
                
                int compression_level = COMPRESSION_LEVEL_DEFAULT;
                
                if (strncasecmp(mime_type, "text/html", 9) == 0 || 
                    strncasecmp(mime_type, "text/css", 8) == 0 ||
                    strncasecmp(mime_type, "application/javascript", 22) == 0) {
                    compression_level = COMPRESSION_LEVEL_DEFAULT;
                }
                else if (strncasecmp(mime_type, "image/", 6) == 0 ||
                        strncasecmp(mime_type, "application/octet-stream", 24) == 0) {
                    compression_level = COMPRESSION_LEVEL_MIN;
                }
                else if (strncasecmp(mime_type, "application/font", 16) == 0 ||
                        strncasecmp(mime_type, "image/svg+xml", 13) == 0) {
                    compression_level = COMPRESSION_LEVEL_MAX;
                }
                
                if (http_compress_content(response, response->compression_type, compression_level) == 0) {
                    if (response->compression_type == COMPRESSION_GZIP) {
                        http_add_header(response, "Content-Encoding", "gzip");
                        LOG_DEBUG("Applied gzip compression: %zu bytes -> %zu bytes", 
                                  response->body_length, response->compressed_length);
                    } else if (response->compression_type == COMPRESSION_DEFLATE) {
                        http_add_header(response, "Content-Encoding", "deflate");
                        LOG_DEBUG("Applied deflate compression: %zu bytes -> %zu bytes", 
                                  response->body_length, response->compressed_length);
                    }
                    
                    char content_length[32];
                    snprintf(content_length, sizeof(content_length), "%zu", response->compressed_length);
                    http_add_header(response, "Content-Length", content_length);
                } else {
                    char content_length[32];
                    snprintf(content_length, sizeof(content_length), "%zu", response->body_length);
                    http_add_header(response, "Content-Length", content_length);
                }
            } else {
                free(file_content);
                response->body_length = st.st_size;
                response->file_fd = file_fd;
                response->is_file = 1;
                
                char content_length[32];
                snprintf(content_length, sizeof(content_length), "%ld", (long)st.st_size);
                http_add_header(response, "Content-Length", content_length);
            }
        } else {
            response->body_length = st.st_size;
            response->file_fd = file_fd;
            response->is_file = 1;
            
            char content_length[32];
            snprintf(content_length, sizeof(content_length), "%ld", (long)st.st_size);
            http_add_header(response, "Content-Length", content_length);
        }
    } else {
        response->body_length = st.st_size;
        response->file_fd = file_fd;
        response->is_file = 1;
        
        char content_length[32];
        snprintf(content_length, sizeof(content_length), "%ld", (long)st.st_size);
        http_add_header(response, "Content-Length", content_length);
    }
    
    char last_modified[64];
    struct tm *tm_info = gmtime(&st.st_mtime);
    strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    http_add_header(response, "Last-Modified", last_modified);
    
    char etag[64];
    snprintf(etag, sizeof(etag), "\"%lx-%lx-%lx\"", 
             (unsigned long)st.st_ino, 
             (unsigned long)st.st_size, 
             (unsigned long)st.st_mtime);
    http_add_header(response, "ETag", etag);
    
    http_add_header(response, "Vary", "Accept-Encoding, User-Agent");
    
    const char *ext = strrchr(full_path, '.');
    if (ext) {
        if (strcasecmp(ext, ".css") == 0 || strcasecmp(ext, ".js") == 0) {
            http_add_header(response, "Cache-Control", "public, max-age=86400, must-revalidate");
        } 
        else if (strcasecmp(ext, ".png") == 0 || 
                 strcasecmp(ext, ".jpg") == 0 || 
                 strcasecmp(ext, ".jpeg") == 0 || 
                 strcasecmp(ext, ".gif") == 0 || 
                 strcasecmp(ext, ".ico") == 0) {
            http_add_header(response, "Cache-Control", "public, max-age=604800, immutable");
        }
        else if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
            http_add_header(response, "Cache-Control", "public, max-age=300, must-revalidate");
        }
        else if (strcasecmp(ext, ".pdf") == 0 || 
                 strcasecmp(ext, ".doc") == 0 || 
                 strcasecmp(ext, ".docx") == 0) {
            http_add_header(response, "Cache-Control", "public, max-age=86400");
        }
        else {
            http_add_header(response, "Cache-Control", "public, max-age=3600");
        }
        
        if (st.st_size < 1024 * 1024 && response->compressed_body == NULL) {
            char *file_content = malloc(st.st_size);
            if (file_content) {
                ssize_t bytes_read = pread(file_fd, file_content, st.st_size, 0);
                if (bytes_read == st.st_size) {
                    char header[4096];
                    int header_len = 0;
                    
                    header_len += snprintf(header + header_len, sizeof(header) - header_len,
                                          "HTTP/1.1 200 OK\r\n");
                    
                    for (int i = 0; i < response->header_count; i++) {
                        header_len += snprintf(header + header_len, sizeof(header) - header_len,
                                             "%s: %s\r\n", 
                                             response->headers[i][0], 
                                             response->headers[i][1]);
                    }
                    
                    header_len += snprintf(header + header_len, sizeof(header) - header_len,
                                          "Connection: keep-alive\r\n");
                    
                    header_len += snprintf(header + header_len, sizeof(header) - header_len, "\r\n");
                    
                    char *complete_response = malloc(header_len + st.st_size);
                    if (complete_response) {
                        memcpy(complete_response, header, header_len);
                        memcpy(complete_response + header_len, file_content, st.st_size);
                        cache_response(full_path, complete_response, header_len + st.st_size, request, etag);
                        free(complete_response);
                    }
                }
                free(file_content);
            }
        }
    } else {
        http_add_header(response, "Cache-Control", "no-cache, no-store, must-revalidate");
    }
    
    return 0;
}

int http_should_keep_alive(const http_request_t *request) {
    if (strcmp(request->version, "HTTP/1.1") == 0) {
        for (int i = 0; i < request->header_count; i++) {
            if (strcasecmp(request->headers[i][0], "Connection") == 0) {
                if (strcasecmp(request->headers[i][1], "close") == 0) {
                    LOG_DEBUG("HTTP/1.1 request with Connection: close, disabling keep-alive");
                    return 0;
                }
                break;
            }
        }
        LOG_DEBUG("HTTP/1.1 request without Connection: close, enabling keep-alive");
        return 1;
    }
    
    if (strcmp(request->version, "HTTP/1.0") == 0) {
        for (int i = 0; i < request->header_count; i++) {
            if (strcasecmp(request->headers[i][0], "Connection") == 0) {
                if (strcasecmp(request->headers[i][1], "keep-alive") == 0) {
                    LOG_DEBUG("HTTP/1.0 request with Connection: keep-alive, enabling keep-alive");
                    return 1;
                }
                break;
            }
        }
        LOG_DEBUG("HTTP/1.0 request without Connection: keep-alive, disabling keep-alive");
        return 0;
    }
    
    LOG_DEBUG("Unknown HTTP version, disabling keep-alive");
    return 0;
}

int http_send_response(int client_fd, http_response_t *response) {
    if (response->is_cached && response->cached_response) {
        ssize_t total_sent = 0;
        size_t remaining = response->body_length;
        const char *ptr = response->cached_response;
        
        while (remaining > 0) {
            ssize_t sent = send(client_fd, ptr + total_sent, remaining, MSG_NOSIGNAL);
            if (sent <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return 0;  
                } else if (errno == EPIPE || errno == ECONNRESET) {
                    LOG_DEBUG("Client disconnected during send: %s", strerror(errno));
                    return -1;
                }
                LOG_ERROR("Failed to send cached response: %s", strerror(errno));
                return -1;
            }
            
            total_sent += sent;
            remaining -= sent;
        }
        
        return 1;  
    }
    
    int header_len = 0;
    
    header_len += snprintf(header_buffer + header_len, sizeof(header_buffer) - header_len,
                          "HTTP/1.1 %d %s\r\n", 
                          response->status_code, 
                          response->status_text ? response->status_text : "Unknown");
    
    for (int i = 0; i < response->header_count; i++) {
        header_len += snprintf(header_buffer + header_len, sizeof(header_buffer) - header_len,
                              "%s: %s\r\n", 
                              response->headers[i][0], 
                              response->headers[i][1]);
    }
    
    if (response->keep_alive) {
        header_len += snprintf(header_buffer + header_len, sizeof(header_buffer) - header_len,
                              "Connection: keep-alive\r\n");
    } else {
        header_len += snprintf(header_buffer + header_len, sizeof(header_buffer) - header_len,
                              "Connection: close\r\n");
    }
    
    header_len += snprintf(header_buffer + header_len, sizeof(header_buffer) - header_len, "\r\n");
    
    if (response->is_file && response->file_fd >= 0) {
        ssize_t sent = send(client_fd, header_buffer, header_len, MSG_MORE | MSG_NOSIGNAL);
        if (sent != header_len) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            } else if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("Client disconnected during header send: %s", strerror(errno));
                return -1;
            }
            LOG_ERROR("Failed to send headers: %s", strerror(errno));
            return -1;
        }
        
        off_t offset = response->file_offset; 
        size_t __attribute__((unused)) total_sent = 0;
        size_t remaining = response->body_length - offset;
        
        const size_t CHUNK_SIZE = 1024 * 1024;
        
        while (remaining > 0) {
            size_t to_send = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
            ssize_t sent = sendfile(client_fd, response->file_fd, &offset, to_send);
            
            if (sent <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    response->file_offset = offset;
                    return 0;  
                } else if (errno == EPIPE || errno == ECONNRESET) {
                    LOG_DEBUG("Client disconnected during file send: %s", strerror(errno));
                    return -1;
                }
                LOG_ERROR("Failed to send file: %s", strerror(errno));
                return -1;
            }
            
            total_sent += sent;
            remaining -= sent;
        }
        
        int off = 0;
        setsockopt(client_fd, IPPROTO_TCP, TCP_CORK, &off, sizeof(off));
        
        return 1;  
    }
    
    if (response->compressed_body && response->compressed_length > 0) {
        ssize_t sent = send(client_fd, header_buffer, header_len, MSG_MORE | MSG_NOSIGNAL);
        if (sent != header_len) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  
            } else if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("Client disconnected during header send: %s", strerror(errno));
                return -1;
            }
            LOG_ERROR("Failed to send headers: %s", strerror(errno));
            return -1;
        }
        
        sent = send(client_fd, response->compressed_body, response->compressed_length, MSG_NOSIGNAL);
        if (sent != (ssize_t)response->compressed_length) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  
            } else if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("Client disconnected during compressed body send: %s", strerror(errno));
                return -1;
            }
            LOG_ERROR("Failed to send compressed body: %s", strerror(errno));
            return -1;
        }
        
        return 1;  
    }
    
    if (response->body && response->body_length > 0) {
        ssize_t sent = send(client_fd, header_buffer, header_len, MSG_MORE | MSG_NOSIGNAL);
        if (sent != header_len) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  
            } else if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("Client disconnected during header send: %s", strerror(errno));
                return -1;
            }
            LOG_ERROR("Failed to send headers: %s", strerror(errno));
            return -1;
        }
        
        sent = send(client_fd, response->body, response->body_length, MSG_NOSIGNAL);
        if (sent != (ssize_t)response->body_length) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;  
            } else if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("Client disconnected during body send: %s", strerror(errno));
                return -1;
            }
            LOG_ERROR("Failed to send body: %s", strerror(errno));
            return -1;
        }
        
        return 1;  
    }
    
    ssize_t sent = send(client_fd, header_buffer, header_len, MSG_NOSIGNAL);
    if (sent != header_len) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  
        } else if (errno == EPIPE || errno == ECONNRESET) {
            LOG_DEBUG("Client disconnected during header send: %s", strerror(errno));
            return -1;
        }
        LOG_ERROR("Failed to send headers: %s", strerror(errno));
        return -1;
    }
    
    return 1;  
}

void http_free_response(http_response_t *response) {
    if (!response) {
        return;
    }
    
    if (response->is_file && response->file_fd != -1) {
        close(response->file_fd);
        response->file_fd = -1; 
        response->is_file = 0;
    }
    
    if (response->body) {
        free(response->body);
        response->body = NULL;
        response->body_length = 0;
    }
    
    if (response->compressed_body) {
        free(response->compressed_body);
        response->compressed_body = NULL;
        response->compressed_length = 0;
    }
    
    response->is_cached = 0;
    response->cached_response = NULL;
}

static int validate_and_resolve_path(const char *root_dir, const char *request_path, char *resolved_path, size_t resolved_path_size) {
    if (strstr(request_path, "..") != NULL) {
        LOG_WARN("Path traversal attempt detected: %s", request_path);
        return -1;
    }
        
    
    if (strlen(request_path) != strcspn(request_path, "\0")) {
        LOG_WARN("Null byte in path: %s", request_path);
        return -1;
    }
    
    char temp_path[PATH_MAX];
    int written = snprintf(temp_path, sizeof(temp_path), "%s%s", root_dir, request_path);
    if (written < 0 || (size_t)written >= sizeof(temp_path)) {
        LOG_ERROR("Path too long: %s%s", root_dir, request_path);
        return -1;
    }
    
    char canonical_path[PATH_MAX];
    if (realpath(temp_path, canonical_path) == NULL) {
        char temp_dir[PATH_MAX];
        strncpy(temp_dir, temp_path, sizeof(temp_dir) - 1);
        temp_dir[sizeof(temp_dir) - 1] = '\0';
        
        char *last_slash = strrchr(temp_dir, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            
            char canonical_dir[PATH_MAX];
            if (realpath(temp_dir, canonical_dir) == NULL) {
                return -1;
            }
            
            char canonical_root[PATH_MAX];
            if (realpath(root_dir, canonical_root) == NULL) {
                LOG_ERROR("Cannot resolve root directory: %s", root_dir);
                return -1;
            }
            
            size_t root_len = strlen(canonical_root);
            if (strncmp(canonical_dir, canonical_root, root_len) != 0 ||
                (canonical_dir[root_len] != '\0' && canonical_dir[root_len] != '/')) {
                LOG_WARN("Path traversal attempt - parent directory outside root: %s", canonical_dir);
                return -1;
            }
            
            size_t dir_len = strlen(canonical_dir);
            size_t file_len = strlen(last_slash + 1);
            if (dir_len + 1 + file_len >= sizeof(canonical_path)) {
                LOG_ERROR("Reconstructed path too long");
                return -1;
            }
            strncpy(canonical_path, canonical_dir, sizeof(canonical_path) - 1);
            canonical_path[sizeof(canonical_path) - 1] = '\0';
            strncat(canonical_path, "/", sizeof(canonical_path) - strlen(canonical_path) - 1);
            strncat(canonical_path, last_slash + 1, sizeof(canonical_path) - strlen(canonical_path) - 1);
        } else {
            char canonical_root[PATH_MAX];
            if (realpath(root_dir, canonical_root) == NULL) {
                LOG_ERROR("Cannot resolve root directory: %s", root_dir);
                return -1;
            }
            strncpy(canonical_path, temp_path, sizeof(canonical_path) - 1);
            canonical_path[sizeof(canonical_path) - 1] = '\0';
        }
    }
    
    char canonical_root[PATH_MAX];
    if (realpath(root_dir, canonical_root) == NULL) {
        LOG_ERROR("Cannot resolve root directory: %s", root_dir);
        return -1;
    }
    
    size_t root_len = strlen(canonical_root);
    if (strncmp(canonical_path, canonical_root, root_len) != 0 ||
        (canonical_path[root_len] != '\0' && canonical_path[root_len] != '/')) {
        LOG_WARN("Path traversal attempt detected: %s resolves to %s, outside of root %s", 
                 request_path, canonical_path, canonical_root);
        return -1;
    }
    
    if (strlen(canonical_path) >= resolved_path_size) {
        LOG_ERROR("Resolved path too long: %s", canonical_path);
        return -1;
    }
    
    strncpy(resolved_path, canonical_path, resolved_path_size - 1);
    resolved_path[resolved_path_size - 1] = '\0';
    
    LOG_DEBUG("Path validated: %s -> %s", request_path, resolved_path);
    return 0;
}

void http_handle_request(const http_request_t *request, http_response_t *response) {
    http_create_response(response, 200);

    int is_head = 0;
    if (strcmp(request->method, "GET") == 0) {
        is_head = 0;
    } else if (strcmp(request->method, "HEAD") == 0) {
        is_head = 1;
    } else {
        response->status_code = 501;
        response->status_text = "Not Implemented";
        response->keep_alive = 0;  
        return;
    }

    config_t *config = config_get_instance();

    char file_path[PATH_MAX];
    const char *request_path = strcmp(request->uri, "/") == 0 ? "/index.html" : request->uri;

    if (validate_and_resolve_path(config->root_dir, request_path, file_path, sizeof(file_path)) != 0) {
        LOG_WARN("Invalid or unsafe path requested: %s", request_path);
        response->status_code = 403;
        response->status_text = "Forbidden";
        response->keep_alive = 0;
        return;
    }
    
    cache_entry_t *cache = find_cached_response(file_path, request);
    if (cache) {
        LOG_DEBUG("Using cached response for %s", file_path);
        const char *if_none = NULL;
        for (int i = 0; i < request->header_count; i++) {
            if (strcasecmp(request->headers[i][0], "If-None-Match") == 0) {
                if_none = request->headers[i][1];
                break;
            }
        }

        if (if_none) {
            LOG_DEBUG("Checking cached ETag: client sent '%s', cached has '%s'", if_none, cache->etag);
            
            char if_none_copy[1024];
            strncpy(if_none_copy, if_none, sizeof(if_none_copy) - 1);
            if_none_copy[sizeof(if_none_copy) - 1] = '\0';
            
            char *token = strtok(if_none_copy, ",");
            int matched = 0;
            
            while (token && !matched) {
                while (isspace(*token)) token++;
                
                if (strcmp(token, "*") == 0) {
                    matched = 1;
                    break;
                }
                
                char clean_token[256] = {0};
                const char *start = token;
                if (*start == 'W' && *(start+1) == '/') {
                    start += 2;
                }
                
                if (*start == '"') {
                    start++;
                    const char *end = strrchr(start, '"');
                    if (end) {
                        size_t len = end - start;
                        strncpy(clean_token, start, len);
                        clean_token[len] = '\0';
                    } else {
                        strncpy(clean_token, start, sizeof(clean_token) - 1);
                    }
                } else {
                    strncpy(clean_token, start, sizeof(clean_token) - 1);
                }
                
                char *p = clean_token + strlen(clean_token) - 1;
                while (p >= clean_token && isspace(*p)) {
                    *p = '\0';
                    p--;
                }
                
                char cached_etag[64] = {0};
                const char *etag_start = cache->etag;
                if (*etag_start == '"') {
                    etag_start++;
                    const char *etag_end = strrchr(etag_start, '"');
                    if (etag_end) {
                        size_t len = etag_end - etag_start;
                        strncpy(cached_etag, etag_start, len);
                        cached_etag[len] = '\0';
                    }
                } else {
                    strncpy(cached_etag, etag_start, sizeof(cached_etag) - 1);
                }
                
                LOG_DEBUG("Comparing cleaned ETags: client '%s' vs cached '%s'", clean_token, cached_etag);
                
                if (strcmp(clean_token, cached_etag) == 0) {
                    matched = 1;
                    break;
                }
                
                token = strtok(NULL, ",");
            }
            
            if (matched) {
                LOG_DEBUG("Cached ETag match found, returning 304 Not Modified");
                response->status_code = 304;
                response->status_text = "Not Modified";
                response->body_length = 0;
                response->is_cached = 0;
                http_add_header(response, "ETag", cache->etag);
                response->keep_alive = http_should_keep_alive(request);
                return;
            }
        }

        response->is_cached = 1;
        response->cached_response = cache->response;
        response->body_length = cache->response_len;
        response->keep_alive = http_should_keep_alive(request);

        if (is_head) {
            response->body_length = 0;
        }

        return;
    }

    struct stat st;
    if (stat(file_path, &st) == -1) {
        LOG_WARN("File not found: %s", file_path);
        response->status_code = 404;
        response->status_text = "Not Found";
        response->keep_alive = 0;
        return;
    }

    const char* if_none_match = NULL;
    for (int i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i][0], "If-None-Match") == 0) {
            if_none_match = request->headers[i][1];
            break;
        }
    }

    const char* if_modified_since = NULL;
    for (int i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i][0], "If-Modified-Since") == 0) {
            if_modified_since = request->headers[i][1];
            break;
        }
    }

    char etag[64];
    snprintf(etag, sizeof(etag), "\"%lx-%lx-%lx\"", 
             (unsigned long)st.st_ino, 
             (unsigned long)st.st_size, 
             (unsigned long)st.st_mtime);

    if (if_none_match) {
        LOG_DEBUG("Checking ETag: client sent '%s', server has '%s'", if_none_match, etag);
        
        char if_none_match_copy[1024];
        strncpy(if_none_match_copy, if_none_match, sizeof(if_none_match_copy) - 1);
        if_none_match_copy[sizeof(if_none_match_copy) - 1] = '\0';
        
        char *token = strtok(if_none_match_copy, ",");
        int matched = 0;
        
        while (token && !matched) {
            while (isspace(*token)) token++;
            
            if (strcmp(token, "*") == 0) {
                matched = 1;
                break;
            }
            
            char clean_token[256] = {0};
            const char *start = token;
            if (*start == 'W' && *(start+1) == '/') {
                start += 2;
            }
            
            if (*start == '"') {
                start++;
                const char *end = strrchr(start, '"');
                if (end) {
                    size_t len = end - start;
                    strncpy(clean_token, start, len);
                    clean_token[len] = '\0';
                } else {
                    strncpy(clean_token, start, sizeof(clean_token) - 1);
                }
            } else {
                strncpy(clean_token, start, sizeof(clean_token) - 1);
            }
            
            char *p = clean_token + strlen(clean_token) - 1;
            while (p >= clean_token && isspace(*p)) {
                *p = '\0';
                p--;
            }
            
            char our_etag[64] = {0};
            const char *etag_start = etag;
            if (*etag_start == '"') {
                etag_start++;
                const char *etag_end = strrchr(etag_start, '"');
                if (etag_end) {
                    size_t len = etag_end - etag_start;
                    strncpy(our_etag, etag_start, len);
                    our_etag[len] = '\0';
                }
            } else {
                strncpy(our_etag, etag_start, sizeof(our_etag) - 1);
            }
            
            LOG_DEBUG("Comparing cleaned ETags: client '%s' vs server '%s'", clean_token, our_etag);
            
            if (strcmp(clean_token, our_etag) == 0) {
                matched = 1;
                break;
            }
            
            token = strtok(NULL, ",");
        }
        
        if (matched) {
            LOG_DEBUG("ETag match found, returning 304 Not Modified");
            response->status_code = 304;
            response->status_text = "Not Modified";
            
            http_add_header(response, "ETag", etag);
            
            const char *ext = strrchr(file_path, '.');
            if (ext) {
                if (strcasecmp(ext, ".css") == 0 || strcasecmp(ext, ".js") == 0) {
                    http_add_header(response, "Cache-Control", "public, max-age=86400, must-revalidate");
                } 
                else if (strcasecmp(ext, ".png") == 0 || 
                         strcasecmp(ext, ".jpg") == 0 || 
                         strcasecmp(ext, ".jpeg") == 0 || 
                         strcasecmp(ext, ".gif") == 0 || 
                         strcasecmp(ext, ".ico") == 0) {
                    http_add_header(response, "Cache-Control", "public, max-age=604800, immutable");
                }
                else if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
                    http_add_header(response, "Cache-Control", "public, max-age=300, must-revalidate");
                }
                else {
                    http_add_header(response, "Cache-Control", "public, max-age=3600");
                }
            }
            
            http_add_header(response, "Vary", "Accept-Encoding, User-Agent");
            
            response->keep_alive = http_should_keep_alive(request);
            return;
        }
    }

    if (if_modified_since) {
        LOG_DEBUG("Checking If-Modified-Since: %s", if_modified_since);
        
        struct tm tm_since;
        memset(&tm_since, 0, sizeof(struct tm));
        int parsed = 0;
        
        if (!parsed && strptime(if_modified_since, "%a, %d %b %Y %H:%M:%S GMT", &tm_since) != NULL) {
            parsed = 1;
        }
        
        if (!parsed && strptime(if_modified_since, "%A, %d-%b-%y %H:%M:%S GMT", &tm_since) != NULL) {
            parsed = 1;
        }
        
        if (!parsed && strptime(if_modified_since, "%a %b %d %H:%M:%S %Y", &tm_since) != NULL) {
            parsed = 1;
        }
        
        if (parsed) {   
            tm_since.tm_isdst = -1;
            time_t since_time = mktime(&tm_since);
            
            if (since_time != -1) {
                since_time += timezone;
                
                struct tm *tm_file = gmtime(&st.st_mtime);
                char file_time_str[64];
                strftime(file_time_str, sizeof(file_time_str), "%a, %d %b %Y %H:%M:%S GMT", tm_file);
                
                LOG_DEBUG("Comparing times: file time %s (%ld) vs if-modified-since %s (%ld)", 
                          file_time_str, (long)st.st_mtime, if_modified_since, (long)since_time);
                
                if (difftime(st.st_mtime, since_time) <= 0) {
                    LOG_DEBUG("File not modified since %s, returning 304", if_modified_since);
                    response->status_code = 304;
                    response->status_text = "Not Modified";
                    
                    http_add_header(response, "ETag", etag);
                    
                    char last_modified[64];
                    strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S GMT", tm_file);
                    http_add_header(response, "Last-Modified", last_modified);
                    
                    http_add_header(response, "Vary", "Accept-Encoding, User-Agent");
                    
                    response->keep_alive = http_should_keep_alive(request);
                    return;
                } else {
                    LOG_DEBUG("File was modified since %s, returning full response", if_modified_since);
                }
            } else {
                LOG_WARN("Failed to convert parsed time to time_t");
            }
        } else {
            LOG_WARN("Failed to parse If-Modified-Since date: %s", if_modified_since);
        }
    }

    const char *content_type = http_get_mime_type(file_path);
    
    int is_compressible = http_should_compress_mime_type(content_type);
    
    compression_type_t compression_type = COMPRESSION_NONE;
    if (is_compressible) {
        compression_type = http_negotiate_compression(request);
    }
    
    response->compression_type = compression_type;

    if (http_serve_file(file_path, response, request) != 0) {
        response->status_code = 404;
        response->status_text = "Not Found";
        response->keep_alive = 0;  
        return;
    }

    response->keep_alive = http_should_keep_alive(request);
    
    if (compression_type != COMPRESSION_NONE && !response->is_file && response->body && 
        response->body_length > 0 && response->compressed_body == NULL) {
        int compression_level = COMPRESSION_LEVEL_DEFAULT;
        
        if (strncasecmp(content_type, "text/html", 9) == 0 || 
            strncasecmp(content_type, "text/css", 8) == 0 ||
            strncasecmp(content_type, "application/javascript", 22) == 0) {
            compression_level = COMPRESSION_LEVEL_DEFAULT;
        }
        else if (strncasecmp(content_type, "image/", 6) == 0 ||
                 strncasecmp(content_type, "application/octet-stream", 24) == 0) {
            compression_level = COMPRESSION_LEVEL_MIN;
        }   
        else if (strncasecmp(content_type, "application/font", 16) == 0 ||
                 strncasecmp(content_type, "image/svg+xml", 13) == 0) {
            compression_level = COMPRESSION_LEVEL_MAX;
        }
        
        if (http_compress_content(response, compression_type, compression_level) == 0) {
            if (compression_type == COMPRESSION_GZIP) {
                http_add_header(response, "Content-Encoding", "gzip");
            } else if (compression_type == COMPRESSION_DEFLATE) {
                http_add_header(response, "Content-Encoding", "deflate");
            }
            
            char content_length[32];
            snprintf(content_length, sizeof(content_length), "%zu", response->compressed_length);
            
            int found = 0;
            for (int i = 0; i < response->header_count; i++) {
                if (strcasecmp(response->headers[i][0], "Content-Length") == 0) {
                    strncpy(response->headers[i][1], content_length, MAX_HEADER_SIZE - 1);
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                http_add_header(response, "Content-Length", content_length);
            }
        }
    }
    
    if (response->keep_alive) {
        char timeout_str[32];
        snprintf(timeout_str, sizeof(timeout_str), "timeout=%d", config->keep_alive_timeout);
        http_add_header(response, "Keep-Alive", timeout_str);
        LOG_DEBUG("Keep-alive enabled for request: %s %s", request->method, request->uri);
    } else {
        LOG_DEBUG("Keep-alive disabled for request: %s %s", request->method, request->uri);
    }

    if (is_head) {
        response->is_file = 0;
        response->is_cached = 0;
        response->body_length = 0;
    }
}

int http_should_compress_mime_type(const char *mime_type) {
    if (!mime_type) {
        return 0;
    }
    
    static const char *compressible_types[] = {
        "text/",
        "application/javascript",
        "application/json",
        "application/xml",
        "application/xhtml+xml",
        "image/svg+xml",
        "application/x-font-ttf",
        "application/x-font-opentype",
        "application/vnd.ms-fontobject",
        "application/font-woff",
        "application/font-woff2",
        NULL
    };
    
    for (int i = 0; compressible_types[i] != NULL; i++) {
        if (strncasecmp(mime_type, compressible_types[i], strlen(compressible_types[i])) == 0) {
            return 1;
        }
    }
    
    return 0;
}

compression_type_t http_negotiate_compression(const http_request_t *request) {
    if (!request) {
        return COMPRESSION_NONE;
    }
    
    for (int i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i][0], "Accept-Encoding") == 0) {
            const char *encodings = request->headers[i][1];
            
            if (strstr(encodings, "gzip") != NULL) {
                LOG_DEBUG("Client accepts gzip compression");
                return COMPRESSION_GZIP;
            }
            
            if (strstr(encodings, "deflate") != NULL) {
                LOG_DEBUG("Client accepts deflate compression");
                return COMPRESSION_DEFLATE;
            }
        }
    }
    
    return COMPRESSION_NONE;
}

int http_compress_content(http_response_t *response, compression_type_t type, int level) {
    if (!response || !response->body || response->body_length == 0) {
        return -1;
    }
    
    if (response->compressed_body) {
        return 0;
    }
    
    if (type != COMPRESSION_GZIP && type != COMPRESSION_DEFLATE) {
        return -1;
    }
    
    if (level < COMPRESSION_LEVEL_MIN || level > COMPRESSION_LEVEL_MAX) {
        level = COMPRESSION_LEVEL_DEFAULT;
    }
    
    size_t buffer_size = response->body_length + 128;
    unsigned char *compressed = malloc(buffer_size);
    
    if (!compressed) {
        LOG_ERROR("Failed to allocate memory for compression");
        return -1;
    }
    
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    int window_bits = (type == COMPRESSION_GZIP) ? (15 + 16) : 15;
    
    if (deflateInit2(&strm, level, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        LOG_ERROR("Failed to initialize zlib compression");
        free(compressed);
        return -1;
    }
    
    strm.avail_in = response->body_length;
    strm.next_in = (Bytef*)response->body;
    strm.avail_out = buffer_size;
    strm.next_out = compressed;
    
    int ret = deflate(&strm, Z_FINISH);
    
    if (ret != Z_STREAM_END) {
        free(compressed);
        deflateEnd(&strm);
        
        buffer_size = response->body_length * 2;
        compressed = malloc(buffer_size);
        
        if (!compressed) {
            LOG_ERROR("Failed to allocate memory for compression retry");
            return -1;
        }
        
        memset(&strm, 0, sizeof(strm));
        
        if (deflateInit2(&strm, level, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            LOG_ERROR("Failed to initialize zlib compression for retry");
            free(compressed);
            return -1;
        }
        
        strm.avail_in = response->body_length;
        strm.next_in = (Bytef*)response->body;
        strm.avail_out = buffer_size;
        strm.next_out = compressed;
        
        ret = deflate(&strm, Z_FINISH);
        
        if (ret != Z_STREAM_END) {
            LOG_ERROR("Failed to compress data even with larger buffer");
            free(compressed);
            deflateEnd(&strm);
            return -1;
        }
    }
    
    response->compressed_body = compressed;
    response->compressed_length = strm.total_out;
    response->compression_type = type;
    response->compression_level = level;
    
    LOG_DEBUG("Compressed %zu bytes to %zu bytes (%d%% reduction)",
              response->body_length, response->compressed_length,
              (int)(100 - (response->compressed_length * 100.0 / response->body_length)));
    
    deflateEnd(&strm);
    return 0;
}

void http_cache_cleanup(void) {
    pthread_mutex_lock(&cache_mutex);
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (response_cache[i].response) {
            update_cache_stats_on_free(response_cache[i].response_len);
            free(response_cache[i].response);
            response_cache[i].response = NULL;
        }
        memset(&response_cache[i], 0, sizeof(cache_entry_t));
    }
    
    cache_index = 0;
    
    LOG_INFO("Cache cleanup completed. Final stats - Hits: %lu, Misses: %lu, Evictions: %lu, "
             "Allocations: %lu, Frees: %lu, Max Memory: %zu bytes", 
             cache_stats.cache_hits, cache_stats.cache_misses, cache_stats.cache_evictions,
             cache_stats.cache_allocations, cache_stats.cache_frees, cache_stats.max_memory_used);
    
    memset(&cache_stats, 0, sizeof(cache_stats_t));
    
    pthread_mutex_unlock(&cache_mutex);
    pthread_mutex_destroy(&cache_mutex);
}

void http_get_cache_stats(unsigned long *hits, unsigned long *misses, unsigned long *evictions, 
                         size_t *memory_used, size_t *max_memory_used) {
    pthread_mutex_lock(&cache_mutex);
    
    if (hits) *hits = cache_stats.cache_hits;
    if (misses) *misses = cache_stats.cache_misses;
    if (evictions) *evictions = cache_stats.cache_evictions;
    if (memory_used) *memory_used = cache_stats.total_memory_used;
    if (max_memory_used) *max_memory_used = cache_stats.max_memory_used;
    
    pthread_mutex_unlock(&cache_mutex);
} 