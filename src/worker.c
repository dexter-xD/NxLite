#include "worker.h"

extern void setup_signal_handlers(void);


static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("Failed to get socket flags: %s", strerror(errno));
        return -1;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("Failed to set non-blocking mode: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

static int create_timeout_timer(int timeout_seconds) {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd == -1) {
        LOG_ERROR("Failed to create timer: %s", strerror(errno));
        return -1;
    }
    
    struct itimerspec its;
    its.it_interval.tv_sec = timeout_seconds;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = timeout_seconds;
    its.it_value.tv_nsec = 0;
    
    if (timerfd_settime(timer_fd, 0, &its, NULL) == -1) {
        LOG_ERROR("Failed to set timer: %s", strerror(errno));
        close(timer_fd);
        return -1;
    }
    
    return timer_fd;
}

static int add_to_epoll(worker_t *worker, int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOG_ERROR("Failed to add fd to epoll: %s", strerror(errno));
        return -1;
    }
    
    LOG_DEBUG("Epoll add: fd=%d, events=0x%x", fd, events);
    
    return 0;
}

static int remove_from_epoll(worker_t *worker, int fd) {
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        LOG_ERROR("Failed to remove fd from epoll: %s", strerror(errno));
        return -1;
    }
    
    LOG_DEBUG("Epoll remove: fd=%d", fd);
    
    return 0;
}

int worker_init(worker_t *worker, int server_fd, int cpu_id) {
    memset(worker, 0, sizeof(worker_t));
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        LOG_ERROR("Failed to set CPU affinity: %s", strerror(errno));
        return -1;
    }
    worker->cpu_id = cpu_id;
    
    if (mempool_init(&worker->buffer_pool, BUFFER_SIZE, BUFFER_POOL_SIZE) != 0) {
        LOG_ERROR("Failed to initialize buffer pool");
        return -1;
    }
    
    worker->epoll_fd = epoll_create1(0);
    if (worker->epoll_fd == -1) {
        LOG_ERROR("Failed to create epoll instance: %s", strerror(errno));
        mempool_cleanup(&worker->buffer_pool);
        return -1;
    }
    
    if (set_nonblocking(server_fd) == -1) {
        mempool_cleanup(&worker->buffer_pool);
        close(worker->epoll_fd);
        return -1;
    }
    
    if (add_to_epoll(worker, server_fd, EPOLLIN | EPOLLET) == -1) {
        mempool_cleanup(&worker->buffer_pool);
        close(worker->epoll_fd);
        return -1;
    }
    
    worker->server_fd = server_fd;
    worker->is_running = 1;
    worker->keep_alive_timeout = KEEP_ALIVE_TIMEOUT;
    
    worker->events = malloc(sizeof(struct epoll_event) * MAX_EVENTS);
    if (!worker->events) {
        LOG_ERROR("Failed to allocate events array");
        mempool_cleanup(&worker->buffer_pool);
        close(worker->epoll_fd);
        return -1;
    }
    
    worker->clients = calloc(MAX_CONNECTIONS, sizeof(client_conn_t));
    if (!worker->clients) {
        LOG_ERROR("Failed to allocate clients array");
        mempool_cleanup(&worker->buffer_pool);
        free(worker->events);
        close(worker->epoll_fd);
        return -1;
    }
    
    worker->connection_pool = malloc(sizeof(int) * CONNECTION_POOL_SIZE);
    if (!worker->connection_pool) {
        LOG_ERROR("Failed to allocate connection pool");
        mempool_cleanup(&worker->buffer_pool);
        free(worker->events);
        free(worker->clients);
        close(worker->epoll_fd);
        return -1;
    }
    worker->pool_size = CONNECTION_POOL_SIZE;
    worker->pool_count = 0;
    
    LOG_INFO("Worker running on CPU %d", worker->cpu_id);
    
    return 0;
}

static int optimize_tcp_socket(int fd) {
    int yes = 1;
    
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        LOG_ERROR("Failed to set TCP_NODELAY: %s", strerror(errno));
        return -1;
    }
    
    int defer_accept = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer_accept, sizeof(defer_accept)) == -1) {
        LOG_WARN("Failed to set TCP_DEFER_ACCEPT: %s (continuing anyway)", strerror(errno));
    }
    
    if (setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &yes, sizeof(yes)) == -1) {
        LOG_WARN("Failed to set TCP_QUICKACK: %s (continuing anyway)", strerror(errno));
    }
    
    int send_buf = SEND_BUFFER_SIZE;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf)) == -1) {
        LOG_ERROR("Failed to set SO_SNDBUF: %s", strerror(errno));
        return -1;
    }
    
    int recv_buf = RECV_BUFFER_SIZE;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf)) == -1) {
        LOG_ERROR("Failed to set SO_RCVBUF: %s", strerror(errno));
        return -1;
    }
    
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        LOG_WARN("Failed to set SO_KEEPALIVE: %s (continuing anyway)", strerror(errno));
    }
    
    return 0;
}

int worker_add_client(worker_t *worker, int client_fd) {
    if (worker->client_count >= MAX_CONNECTIONS) {
        LOG_ERROR("Too many clients");
        return -1;
    }
    
    if (optimize_tcp_socket(client_fd) == -1) {
        return -1;
    }
    
    if (set_nonblocking(client_fd) == -1) {
        return -1;
    }
    
    char *buffer = mempool_alloc(&worker->buffer_pool);
    if (!buffer) {
        LOG_ERROR("Failed to allocate buffer from pool");
        return -1;
    }
    
    int timer_fd = create_timeout_timer(worker->keep_alive_timeout);
    if (timer_fd == -1) {
        mempool_free(&worker->buffer_pool, buffer);
        return -1;
    }
    
    if (add_to_epoll(worker, timer_fd, EPOLLIN | EPOLLET) == -1) {
        mempool_free(&worker->buffer_pool, buffer);
        close(timer_fd);
        return -1;
    }
    
    worker->clients[worker->client_count].fd = client_fd;
    worker->clients[worker->client_count].timer_fd = timer_fd;
    worker->clients[worker->client_count].last_activity = time(NULL);
    worker->clients[worker->client_count].buffer = buffer;
    worker->clients[worker->client_count].keep_alive = 1; 
    worker->clients[worker->client_count].has_pending_response = 0;
    worker->client_count++;
    
    LOG_DEBUG("Buffer allocated for fd=%d", client_fd);
    
    return 0;
}

void worker_remove_client(worker_t *worker, int client_fd) {
    for (int i = 0; i < worker->client_count; i++) {
        if (worker->clients[i].fd == client_fd) {
            remove_from_epoll(worker, client_fd);
            remove_from_epoll(worker, worker->clients[i].timer_fd);
            
            if (worker->clients[i].buffer) {
                mempool_free(&worker->buffer_pool, worker->clients[i].buffer);
                LOG_DEBUG("Buffer freed for fd=%d", client_fd);
            }
            
            if (worker->clients[i].has_pending_response) {
                http_free_response(&worker->clients[i].pending_response);
                worker->clients[i].has_pending_response = 0;
            }
            
            close(client_fd);
            close(worker->clients[i].timer_fd);
            
            if (i < worker->client_count - 1) {
                worker->clients[i] = worker->clients[worker->client_count - 1];
            }
            worker->client_count--;
            
            LOG_INFO("Closed connection: fd=%d, clients=%d", client_fd, worker->client_count);
            
            break;
        }
    }
}

void worker_handle_timeout(worker_t *worker, int timer_fd) {
    for (int i = 0; i < worker->client_count; i++) {
        if (worker->clients[i].timer_fd == timer_fd) {
            time_t now = time(NULL);
            if (now - worker->clients[i].last_activity >= worker->keep_alive_timeout) {
                LOG_INFO("Client timeout: fd=%d, idle=%lds", worker->clients[i].fd, now - worker->clients[i].last_activity);
                worker_remove_client(worker, worker->clients[i].fd);
            }
            break;
        }
    }
}

void worker_handle_connection(worker_t *worker, int client_fd) {
    int opt = 1;
    
    if (setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set SO_KEEPALIVE for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set TCP_NODELAY for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    int snd_buf = 65536;
    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf)) < 0) {
        LOG_ERROR("Failed to set SO_SNDBUF for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    int rcv_buf = 65536;
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcv_buf, sizeof(rcv_buf)) < 0) {
        LOG_ERROR("Failed to set SO_RCVBUF for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    int keepidle = 60;  
    int keepintvl = 10; 
    int keepcnt = 6;    
    
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
        LOG_ERROR("Failed to set TCP_KEEPIDLE for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
        LOG_ERROR("Failed to set TCP_KEEPINTVL for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
        LOG_ERROR("Failed to set TCP_KEEPCNT for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    if (set_nonblocking(client_fd) == -1) {
        LOG_ERROR("Failed to set non-blocking mode for client: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = client_fd;
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
        LOG_ERROR("Failed to add client to epoll: %s", strerror(errno));
        close(client_fd);
        return;
    }
    
    int timer_fd = create_timeout_timer(worker->keep_alive_timeout);
    if (timer_fd == -1) {
        LOG_ERROR("Failed to create timeout timer for client");
        close(client_fd);
        return;
    }
    
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd;
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev) == -1) {
        LOG_ERROR("Failed to add timer to epoll: %s", strerror(errno));
        close(timer_fd);
        close(client_fd);
        return;
    }
    
    char *buffer = mempool_alloc(&worker->buffer_pool);
    if (!buffer) {
        LOG_ERROR("Failed to allocate buffer for client");
        close(timer_fd);
        close(client_fd);
        return;
    }
    
    if (worker->client_count >= MAX_CONNECTIONS) {
        LOG_WARN("Connection limit reached, rejecting new connection");
        mempool_free(&worker->buffer_pool, buffer);
        close(timer_fd);
        close(client_fd);
        return;
    }
    
    worker->clients[worker->client_count].fd = client_fd;
    worker->clients[worker->client_count].timer_fd = timer_fd;
    worker->clients[worker->client_count].last_activity = time(NULL);
    worker->clients[worker->client_count].buffer = buffer;
    worker->clients[worker->client_count].keep_alive = 1;  // Default to keep-alive
    worker->clients[worker->client_count].has_pending_response = 0;
    worker->client_count++;
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len) == 0) {
        LOG_INFO("Accepted connection: fd=%d, ip=%s, port=%d, clients=%d", client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), worker->client_count);
    }
    
    LOG_DEBUG("Buffer allocated for fd=%d", client_fd);
}

void worker_handle_client_data(worker_t *worker, int client_fd) {
    client_conn_t *client = NULL;
    for (int i = 0; i < worker->client_count; i++) {
        if (worker->clients[i].fd == client_fd) {
            client = &worker->clients[i];
            break;
        }
    }
    if (!client || !client->buffer) {
        return;
    }

    ssize_t bytes_read;
    int total_read = 0;
    int __attribute__((unused)) processed = 0;

    while ((bytes_read = recv(client_fd, client->buffer + total_read, BUFFER_SIZE - total_read - 1, 0)) > 0) {
        total_read += bytes_read;
        if ((size_t)total_read >= BUFFER_SIZE - 1) {
            break;
        }
    }

    if (total_read > 0) {
        client->buffer[total_read] = '\0';
        client->last_activity = time(NULL);
        int offset = 0;
        
        while (offset < total_read) {
            char *end = strstr(client->buffer + offset, "\r\n\r\n");
            if (!end) {
                if (offset > 0 && offset < total_read) {
                    memmove(client->buffer, client->buffer + offset, total_read - offset);
                }
                break;
            }

            int req_len = end - (client->buffer + offset) + 4;
            
            http_request_t request;
            if (http_parse_request(client->buffer + offset, req_len, &request) != 0) {
                LOG_ERROR("Failed to parse HTTP request from fd=%d", client_fd);
                http_response_t response;
                http_create_response(&response, 400);
                response.keep_alive = 0;  // Force close on error
                http_send_response(client_fd, &response);
                worker_remove_client(worker, client_fd);
                return;
            }

            http_response_t response;
            http_handle_request(&request, &response);
            
            client->keep_alive = response.keep_alive;
            
            int send_result = http_send_response(client_fd, &response);
            if (send_result == -1) {
                worker_remove_client(worker, client_fd);
                return;
            } else if (send_result == 0) {
                struct epoll_event ev;
                ev.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
                ev.data.fd = client_fd;
                
                if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) == -1) {
                    LOG_ERROR("Failed to modify client epoll events for write: %s", strerror(errno));
                    worker_remove_client(worker, client_fd);
                    return;
                }
                
                client->pending_response = response;
                client->has_pending_response = 1;
                
                LOG_DEBUG("Response send would block, switching to write monitoring for fd=%d", client_fd);
                return;
            }
            
            http_free_response(&response);
            
            processed++;
            offset += req_len;

            if (!client->keep_alive) {
                LOG_INFO("Closing connection: fd=%d (keep-alive disabled)", client_fd);
                worker_remove_client(worker, client_fd);
                return;
            }

            struct itimerspec its;
            its.it_interval.tv_sec = worker->keep_alive_timeout;
            its.it_interval.tv_nsec = 0;
            its.it_value.tv_sec = worker->keep_alive_timeout;
            its.it_value.tv_nsec = 0;
            
            if (timerfd_settime(client->timer_fd, 0, &its, NULL) == -1) {
                LOG_ERROR("Failed to reset keep-alive timer: %s", strerror(errno));
                worker_remove_client(worker, client_fd);
                return;
            }
        }

        if (offset < total_read) {
            memmove(client->buffer, client->buffer + offset, total_read - offset);
        }
    } else if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        LOG_INFO("Connection closed by client: fd=%d", client_fd);
        worker_remove_client(worker, client_fd);
    }
}

void worker_handle_client_write(worker_t *worker, int client_fd) {
    client_conn_t *client = NULL;
    for (int i = 0; i < worker->client_count; i++) {
        if (worker->clients[i].fd == client_fd) {
            client = &worker->clients[i];
            break;
        }
    }
    
    if (!client) {
        LOG_ERROR("Client not found for fd %d", client_fd);
        return;
    }
    
    client->last_activity = time(NULL);
    
    if (client->has_pending_response) {
        int send_result = http_send_response(client_fd, &client->pending_response);
        
        if (send_result == -1) {
            LOG_DEBUG("Failed to send pending response, closing connection fd=%d", client_fd);
            worker_remove_client(worker, client_fd);
            return;
        } else if (send_result == 0) {
            LOG_DEBUG("Pending response still would block for fd=%d", client_fd);
            return;
        }
        
        LOG_DEBUG("Successfully sent pending response for fd=%d", client_fd);
        
        http_free_response(&client->pending_response);
        client->has_pending_response = 0;
        
        if (!client->keep_alive) {
            LOG_INFO("Closing connection after sending pending response: fd=%d", client_fd);
            worker_remove_client(worker, client_fd);
            return;
        }
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = client_fd;
    
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) == -1) {
        LOG_ERROR("Failed to modify client epoll events: %s", strerror(errno));
        worker_remove_client(worker, client_fd);
        return;
    }
    
    LOG_DEBUG("Client fd %d ready for read operations", client_fd);
}

void worker_run(worker_t *worker) {
    LOG_INFO("Worker %d starting event loop on CPU %d", worker->cpu_id, worker->cpu_id);
    
    int max_accept_per_cycle = 2000;  
    int idle_cycles = 0;
    int max_idle_cycles = 5;  
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    time_t last_stats_time = time(NULL);
    unsigned long request_count = 0;
    unsigned long connection_count = 0;
    
    struct epoll_event *events = malloc(sizeof(struct epoll_event) * MAX_EVENTS * 2);
    if (!events) {
        LOG_ERROR("Failed to allocate extended events array");
        return;
    }
    
    while (worker->is_running && !shutdown_requested) {
        int timeout = 5; 
        int nfds = epoll_wait(worker->epoll_fd, events, MAX_EVENTS * 2, timeout);
        
        if (nfds == -1) {
            if (errno == EINTR) {
                if (shutdown_requested) {
                    LOG_INFO("Worker %d received shutdown signal", worker->cpu_id);
                    break;
                }
                continue;
            }
            
            LOG_ERROR("epoll_wait error: %s", strerror(errno));
            break;
        }
        
        if (nfds == 0) {
            idle_cycles++;
            if (idle_cycles >= max_idle_cycles) {
                if (idle_cycles < 20) {
                    usleep(1000); 
                } else if (idle_cycles < 100) {
                    usleep(5000); 
                } else {
                    usleep(10000); 
                }
            }
            continue;
        }
        
        idle_cycles = 0;
        
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            uint32_t event_flags = events[i].events;
            
            if (event_flags & (EPOLLERR | EPOLLHUP)) {
                if (fd == worker->server_fd) {
                    LOG_ERROR("Server socket error");
                    worker->is_running = 0;
                    break;
                } else {
                    LOG_DEBUG("Client socket error on fd %d", fd);
                    worker_remove_client(worker, fd);
                    continue;
                }
            }
            
            if (fd == worker->server_fd && (event_flags & EPOLLIN)) {
                int accepted = 0;
                
                while (accepted < max_accept_per_cycle) {
                    int client_fd = accept4(worker->server_fd, 
                                           (struct sockaddr*)&client_addr, 
                                           &addr_len, 
                                           SOCK_NONBLOCK);
                    
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else if (errno == EMFILE || errno == ENFILE) {
                            LOG_WARN("Too many open files (%s), implementing emergency measures", strerror(errno));
                            
                            int closed = 0;
                            time_t now = time(NULL);
                            
                            for (int j = 0; j < worker->client_count && closed < 10; j++) {
                                if (now - worker->clients[j].last_activity > 5) {
                                    worker_remove_client(worker, worker->clients[j].fd);
                                    closed++;
                                }
                            }
                            
                            if (closed > 0) {
                                LOG_INFO("Emergency closed %d idle connections", closed);
                                continue;  
                            }
                            
                            usleep(20000); 
                            break;
                        } else {
                            LOG_ERROR("Accept error: %s", strerror(errno));
                            break;
                        }
                    }
                    
                    optimize_tcp_socket(client_fd);
                    
                    worker_handle_connection(worker, client_fd);
                    accepted++;
                    connection_count++;
                }
                
                if (accepted > 0) {
                    LOG_DEBUG("Accepted %d new connections in batch", accepted);
                }
            }
            else if (event_flags & EPOLLIN) {
                worker_handle_client_data(worker, fd);
                request_count++;
            }
            else if (event_flags & EPOLLOUT) {
                worker_handle_client_write(worker, fd);
            }
            else if (event_flags & EPOLLRDHUP) {
                worker_remove_client(worker, fd);
            }
        }
        
        time_t now = time(NULL);
        if (now - last_stats_time >= 10) {
            unsigned long requests_per_sec = request_count / (now - last_stats_time);
            LOG_INFO("Worker %d stats: %lu req/s, %lu total connections, %d current clients",
                     worker->cpu_id, requests_per_sec, connection_count, worker->client_count);
            request_count = 0;
            last_stats_time = now;
        }
    }
    
    free(events);
    LOG_INFO("Worker %d exiting event loop", worker->cpu_id);
}

void worker_cleanup(worker_t *worker) {
    if (!worker) {
        return;
    }
    
    for (int i = 0; i < worker->client_count; i++) {
        if (worker->clients[i].buffer) {
            mempool_free(&worker->buffer_pool, worker->clients[i].buffer);
        }
        close(worker->clients[i].fd);
        close(worker->clients[i].timer_fd);
    }
    
    free(worker->clients);
    free(worker->events);
    close(worker->epoll_fd);
    mempool_cleanup(&worker->buffer_pool);
} 