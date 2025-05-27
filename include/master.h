#ifndef MASTER_H
#define MASTER_H

#include <sys/types.h>
#include <netinet/in.h>
#include "server.h"
#include "log.h"
#include "config.h"
#include "worker.h"
#include "shutdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sched.h>


#define MAX_WORKERS 32
#define DEFAULT_WORKER_COUNT 4

typedef struct {
    int server_fd;
    int port;
    int worker_count;
    int is_running;
    int is_shutting_down;
} master_t;

int master_init(master_t *master, int port, int worker_count);
void master_run(master_t *master);
void master_cleanup(master_t *master);
void master_handle_signal(int signum);
master_t* master_get_instance(void);

#endif 