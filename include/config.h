#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    int port;
    int worker_count;
    char root_dir[256];
    char log_file[256];
    int max_connections;
    int keep_alive_timeout;
    int development_mode;
} config_t;

void config_init(config_t *config);
int config_load(config_t *config, const char *filename);
int config_reload(config_t *config, const char *filename);
config_t* config_get_instance(void);

#endif 