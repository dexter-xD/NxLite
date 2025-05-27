#ifndef WORKER_H
#define WORKER_H

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "log.h"
#include "http.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <limits.h>  
#include <time.h>
#include <sched.h>   
#include <signal.h>  
#include "shutdown.h"
#include "common.h"
#include "mempool.h"
#include "http.h"  

#define BUFFER_SIZE 8192
#define BUFFER_POOL_SIZE 10000
#define MAX_CONNECTIONS 100000
#define CONNECTION_POOL_SIZE 1000
#define SEND_BUFFER_SIZE 65536
#define RECV_BUFFER_SIZE 65536
#define RATE_LIMIT_WINDOW 60  
#define RATE_LIMIT_MAX_REQUESTS 100 
#define RATE_LIMIT_TABLE_SIZE 1024

#define BAN_DURATION 300 
#define MAX_VIOLATIONS_BEFORE_BAN 3 
#define SLOW_LORIS_TIMEOUT 10 
#define MAX_CONCURRENT_CONNECTIONS_PER_IP 10 

typedef struct {
    int fd;
    int timer_fd;  
    time_t last_activity;  
    char *buffer;  
    int keep_alive;  
    int has_pending_response;  
    http_response_t pending_response;
    time_t connection_start;
    char client_ip[INET_ADDRSTRLEN];
    int bytes_received;
} client_conn_t;

typedef struct {
    int epoll_fd;
    int server_fd;
    struct epoll_event *events;
    int is_running;
    int keep_alive_timeout;  
    client_conn_t *clients;  
    int client_count;
    mempool_t buffer_pool;  
    int cpu_id;  
    int *connection_pool;  
    int pool_size;
    int pool_count;
} worker_t;

typedef struct {
    char ip[INET_ADDRSTRLEN];
    time_t window_start;
    int request_count;
    time_t last_request;
    int violation_count;
    time_t ban_until;
    int connection_count;
} rate_limit_entry_t;

int worker_init(worker_t *worker, int server_fd, int cpu_id);

void worker_run(worker_t *worker);
void worker_cleanup(worker_t *worker);
void worker_handle_connection(worker_t *worker, int client_fd);
void worker_handle_client_data(worker_t *worker, int client_fd);
void worker_handle_client_write(worker_t *worker, int client_fd);
void worker_handle_timeout(worker_t *worker, int timer_fd);
int worker_add_client(worker_t *worker, int client_fd);
void worker_remove_client(worker_t *worker, int client_fd);

#endif 