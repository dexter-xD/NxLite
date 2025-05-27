#include "log.h"

// Security: Sanitize input for logging to prevent log injection
static void sanitize_for_log(char *str, size_t max_len) {
    if (!str) return;
    
    size_t len = strnlen(str, max_len);
    for (size_t i = 0; i < len; i++) {
        // Replace control characters and non-printable chars
        if (str[i] < 32 || str[i] > 126) {
            str[i] = '?';
        }
    }
}

static FILE *log_file = NULL;
static log_level_t current_level = LOG_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

int log_init(const char *filename) {
    if (log_file != NULL) {
        fclose(log_file);
    }

    log_file = fopen(filename, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return -1;
    }

    return 0;
}

void log_set_level(log_level_t level) {
    current_level = level;
}

static void get_timestamp(char *buffer, size_t size) {
    struct timeval tv;
    struct tm *tm;
    
    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm);
    snprintf(buffer + strlen(buffer), size - strlen(buffer), ".%06ld", tv.tv_usec);
}

void log_message(log_level_t level, const char *format, ...) {
    if (level < current_level) {
        return;
    }

    pthread_mutex_lock(&log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    if (log_file != NULL) {
        fprintf(log_file, "[%s] [%s] ", timestamp, level_strings[level]);
        
        va_list args;
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        
        fprintf(log_file, "\n");
        fflush(log_file);
    }

    if (level >= LOG_ERROR) {
        fprintf(stderr, "[%s] [%s] ", timestamp, level_strings[level]);
        
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        
        fprintf(stderr, "\n");
    }

    pthread_mutex_unlock(&log_mutex);
}

void log_access(const char *client_ip, const char *method, const char *uri, 
                int status, long response_size) {
    pthread_mutex_lock(&log_mutex);

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    if (log_file != NULL) {
        // Security: Sanitize inputs to prevent log injection
        char safe_method[32], safe_uri[512];
        strncpy(safe_method, method ? method : "-", sizeof(safe_method) - 1);
        strncpy(safe_uri, uri ? uri : "-", sizeof(safe_uri) - 1);
        safe_method[sizeof(safe_method) - 1] = '\0';
        safe_uri[sizeof(safe_uri) - 1] = '\0';
        
        sanitize_for_log(safe_method, sizeof(safe_method));
        sanitize_for_log(safe_uri, sizeof(safe_uri));
        
        fprintf(log_file, "%s - - [%s] \"%s %s\" %d %ld\n",
                client_ip ? client_ip : "-", timestamp, safe_method, safe_uri, status, response_size);
        fflush(log_file);
    }

    pthread_mutex_unlock(&log_mutex);
}

void log_cleanup(void) {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    
    pthread_mutex_unlock(&log_mutex);
    pthread_mutex_destroy(&log_mutex);
} 