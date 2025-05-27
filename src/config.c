#include "config.h"

static config_t config_instance;

config_t* config_get_instance(void) {
    return &config_instance;
}

void config_init(config_t *config) {
    memset(config, 0, sizeof(config_t));
    config->port = 8080;
    config->worker_count = 4;
    strncpy(config->root_dir, "./static", sizeof(config->root_dir) - 1);
    strncpy(config->log_file, "./logs/access.log", sizeof(config->log_file) - 1);
    config->max_connections = 10000;
    config->keep_alive_timeout = 60;
    config->development_mode = 0;
}

static void trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

static int parse_config_line(config_t *config, const char *line) {
    char key[64], value[256];
    
    if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
        return 0;
    }

    if (sscanf(line, "%63[^=]=%255[^\n]", key, value) != 2) {
        return -1;
    }

    trim_whitespace(key);
    trim_whitespace(value);

    if (strcmp(key, "port") == 0) {
        config->port = atoi(value);
    } else if (strcmp(key, "worker_processes") == 0) {
        config->worker_count = atoi(value);
    } else if (strcmp(key, "root") == 0) {
        strncpy(config->root_dir, value, sizeof(config->root_dir) - 1);
    } else if (strcmp(key, "log") == 0) {
        strncpy(config->log_file, value, sizeof(config->log_file) - 1);
    } else if (strcmp(key, "max_connections") == 0) {
        config->max_connections = atoi(value);
    } else if (strcmp(key, "keep_alive_timeout") == 0) {
        config->keep_alive_timeout = atoi(value);
    } else if (strcmp(key, "development_mode") == 0) {
        config->development_mode = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    }

    return 0;
}

int config_load(config_t *config, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (parse_config_line(config, line) != 0) {
            fprintf(stderr, "Error parsing config line: %s", line);
        }
    }

    fclose(file);
    return 0;
}

int config_reload(config_t *config, const char *filename) {
    config_t new_config;
    config_init(&new_config);
    
    if (config_load(&new_config, filename) != 0) {
        return -1;
    }

    config->worker_count = new_config.worker_count;
    strncpy(config->root_dir, new_config.root_dir, sizeof(config->root_dir) - 1);
    strncpy(config->log_file, new_config.log_file, sizeof(config->log_file) - 1);
    config->keep_alive_timeout = new_config.keep_alive_timeout;
    config->development_mode = new_config.development_mode;

    return 0;
} 