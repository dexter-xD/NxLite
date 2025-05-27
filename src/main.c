#include "master.h"
#include "config.h"
#include "log.h"
#include "shutdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/resource.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

void handle_shutdown_signal(int signo) {
    LOG_INFO("Received signal %d, initiating shutdown", signo);
    shutdown_requested = 1;
}

void setup_signal_handlers(void) {
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = master_handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    signal(SIGPIPE, SIG_IGN);
}

void ensure_directories_exist(const char *path) {
    if (!path) {
        fprintf(stderr, "Invalid path provided\n");
        return;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        perror("Failed to allocate memory for path");
        return;
    }

    char *last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
    } else {
        free(path_copy);
        return;
    }


    if (strlen(path_copy) == 0) {
        free(path_copy);
        return;
    }


    char *p = path_copy;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (strlen(path_copy) > 0) {
                struct stat st = {0};
                if (stat(path_copy, &st) == -1) {
                    if (mkdir(path_copy, 0755) == -1) {
                        fprintf(stderr, "Failed to create directory %s: %s\n", 
                                path_copy, strerror(errno));
                        free(path_copy);
                        return;
                    }
                    fprintf(stderr, "Created directory: %s\n", path_copy);
                }
            }
            *p = '/';
        }
        p++;
    }

    struct stat st = {0};
    if (stat(path_copy, &st) == -1) {
        if (mkdir(path_copy, 0755) == -1) {
            fprintf(stderr, "Failed to create directory %s: %s\n", 
                    path_copy, strerror(errno));
        } else {
            fprintf(stderr, "Created directory: %s\n", path_copy);
        }
    }

    free(path_copy);
}

static int set_resource_limits(void) {
    struct rlimit rlim;
    
    rlim.rlim_cur = rlim.rlim_max = 200000;
    if (setrlimit(RLIMIT_NOFILE, &rlim) == -1) {
        LOG_WARN("Failed to set RLIMIT_NOFILE: %s (continuing anyway)", strerror(errno));
    }
    
    rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &rlim) == -1) {
        LOG_WARN("Failed to set RLIMIT_CORE: %s (continuing anyway)", strerror(errno));
    }
    
    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [options] [config_file]\n", program_name);
    printf("Options:\n");
    printf("  -d, --dev         Enable development mode (disables DoS protection)\n");
    printf("  -h, --help        Show this help message\n");
    printf("\nDefault config file: config/server.conf\n");
}

int main(int argc, char *argv[]) {
    const char *config_file = "config/server.conf";
    int development_mode = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dev") == 0) {
            development_mode = 1;
            printf("Development mode enabled - DoS protection disabled\n");
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            config_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    char abs_config_path[PATH_MAX];
    if (realpath(config_file, abs_config_path) == NULL) {
        fprintf(stderr, "Failed to resolve config file path: %s\n", strerror(errno));
        return 1;
    }
    
    config_t *config = config_get_instance();
    if (config_load(config, abs_config_path) != 0) {
        fprintf(stderr, "Failed to load configuration from %s\n", abs_config_path);
        return 1;
    }
    
    if (development_mode) {
        config->development_mode = 1;
    }
    
    ensure_directories_exist(config->log_file);
    ensure_directories_exist(config->root_dir);
    
    if (log_init(config->log_file) != 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }
    
    if (set_resource_limits() != 0) {
        LOG_ERROR("Failed to set resource limits");
        return 1;
    }
    
    setup_signal_handlers();
    
    master_t master;
    if (master_init(&master, config->port, config->worker_count) != 0) {
        LOG_ERROR("Failed to initialize master process");
        log_cleanup();
        return 1;
    }
    
    LOG_INFO("Starting server on port %d with %d workers", config->port, config->worker_count);
    
    if (config->development_mode) {
        LOG_WARN("DEVELOPMENT MODE ACTIVE - DoS protection disabled!");
        LOG_WARN("This should NEVER be used in production!");
    }
    
    master_run(&master);
    
    master_cleanup(&master);
    log_cleanup();
    
    LOG_INFO("Server shutdown complete");
    
    return 0;
} 