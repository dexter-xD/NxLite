#include "server.h"

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

static int optimize_server_socket(int fd) {
    int on = 1;
    
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        LOG_ERROR("Failed to set SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
    
    int qlen = 256;  
    if (setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) == -1) {
        LOG_WARN("Failed to set TCP_FASTOPEN (continuing anyway): %s", strerror(errno));
    }
    
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == -1) {
        LOG_ERROR("Failed to set TCP_NODELAY: %s", strerror(errno));
        return -1;
    }
    
    int secs = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &secs, sizeof(secs)) == -1) {
        LOG_WARN("Failed to set TCP_DEFER_ACCEPT (continuing anyway): %s", strerror(errno));
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) == -1) {
        LOG_WARN("Failed to set SO_REUSEPORT (continuing anyway): %s", strerror(errno));
    }
    
    int bufsize = 2 * 1024 * 1024;  
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) == -1) {
        LOG_WARN("Failed to set SO_RCVBUF (continuing anyway): %s", strerror(errno));
    }
    
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) == -1) {
        LOG_WARN("Failed to set SO_SNDBUF (continuing anyway): %s", strerror(errno));
    }
    
    if (setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &on, sizeof(on)) == -1) {
        LOG_WARN("Failed to set TCP_QUICKACK (continuing anyway): %s", strerror(errno));
    }
    
    if (setsockopt(fd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on)) == -1) {
        LOG_WARN("Failed to set TCP_CORK (continuing anyway): %s", strerror(errno));
    }
    
    #ifdef TCP_CONGESTION
    const char *algo = "bbr";
    if (setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, algo, strlen(algo)) == -1) {
        algo = "cubic";
        if (setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, algo, strlen(algo)) == -1) {
            LOG_WARN("Failed to set TCP congestion algorithm (continuing anyway): %s", strerror(errno));
        }
    }
    #endif
    
    return 0;
}

int server_init(server_t *server, int port) {
    if (!server) {
        LOG_ERROR("NULL server pointer in server_init");
        return -1;
    }
    
    if (port <= 0 || port > 65535) {
        LOG_ERROR("Invalid port number: %d (must be 1-65535)", port);
        return -1;
    }
    
    if (port < 1024) {
        LOG_WARN("Using privileged port %d (requires root privileges)", port);
    }
    
    memset(server, 0, sizeof(server_t));
    
    server->server_fd = -1;
    server->epoll_fd = -1;
    
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd == -1) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    if (server->server_fd >= FD_SETSIZE) {
        LOG_WARN("Server socket FD %d is larger than FD_SETSIZE %d", server->server_fd, FD_SETSIZE);
    }
    
    if (optimize_server_socket(server->server_fd) == -1) {
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }
    
    memset(&server->server_addr, 0, sizeof(server->server_addr));
    server->server_addr.sin_family = AF_INET;
    server->server_addr.sin_addr.s_addr = INADDR_ANY;
    server->server_addr.sin_port = htons((uint16_t)port);
    
    if (bind(server->server_fd, (struct sockaddr *)&server->server_addr, 
             sizeof(server->server_addr)) == -1) {
        LOG_ERROR("Failed to bind socket to port %d: %s", port, strerror(errno));
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }
    
    if (set_nonblocking(server->server_fd) == -1) {
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }
    
    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd == -1) {
        LOG_ERROR("Failed to create epoll instance: %s", strerror(errno));
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }
    
    server->is_running = 1;
    server->is_worker = 0;
    
    LOG_INFO("Server initialized successfully on port %d", port);
    return 0;
}

int server_init_worker(server_t *server, int server_fd) {
    if (!server) {
        LOG_ERROR("NULL server pointer in server_init_worker");
        return -1;
    }
    
    if (server_fd < 0) {
        LOG_ERROR("Invalid server file descriptor: %d", server_fd);
        return -1;
    }
    
    memset(server, 0, sizeof(server_t));
    server->is_worker = 1;
    server->server_fd = server_fd;

    if (set_nonblocking(server->server_fd) == -1) {
        LOG_ERROR("Failed to set non-blocking mode for worker");
        return -1;
    }

    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd == -1) {
        LOG_ERROR("Failed to create epoll instance for worker: %s", strerror(errno));
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server->server_fd;
    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->server_fd, &ev) == -1) {
        LOG_ERROR("Failed to add server socket to epoll: %s", strerror(errno));
        close(server->epoll_fd);
        server->epoll_fd = -1;
        return -1;
    }

    LOG_DEBUG("Worker server initialized with fd=%d", server_fd);
    return 0;
}

void handle_new_connection(server_t *server) {
    if (!server || server->server_fd < 0) {
        LOG_ERROR("Invalid server state in handle_new_connection");
        return;
    }
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    int connections_accepted = 0;
    const int max_accepts_per_call = 100;  

    while (connections_accepted < max_accepts_per_call && 
           (client_fd = accept(server->server_fd, (struct sockaddr*)&client_addr, &client_len)) != -1) {
        
        if (client_fd < 0) {
            LOG_ERROR("Invalid client file descriptor: %d", client_fd);
            continue;
        }
        
        if (client_fd >= FD_SETSIZE) {
            LOG_WARN("Client FD %d exceeds FD_SETSIZE, closing connection", client_fd);
            close(client_fd);
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip))) {
            LOG_DEBUG("Accepted connection from %s:%d (fd=%d)", 
                     client_ip, ntohs(client_addr.sin_port), client_fd);
        }
        
        if (set_nonblocking(client_fd) == -1) {
            LOG_ERROR("Failed to set non-blocking mode for client %d", client_fd);
            close(client_fd);
            continue;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            LOG_ERROR("Failed to add client %d to epoll: %s", client_fd, strerror(errno));
            close(client_fd);
            continue;
        }
        
        connections_accepted++;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("Accept error: %s", strerror(errno));
    }
    
    if (connections_accepted > 0) {
        LOG_DEBUG("Accepted %d new connections", connections_accepted);
    }
}

void handle_client_data(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        
        const char *response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Content-Length: 12\r\n"
                             "\r\n"
                             "Hello World!";
        
        write(client_fd, response, strlen(response));
        close(client_fd);
        return;
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("read");
    }
}

int server_run(server_t *server) {
    printf("Worker running on port %d...\n", ntohs(server->server_addr.sin_port));

    while (1) {
        int nfds = epoll_wait(server->epoll_fd, server->events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            return -1;
        }

        for (int i = 0; i < nfds; ++i) {
            if (server->events[i].data.fd == server->server_fd) {
                handle_new_connection(server);
            } else {
                handle_client_data(server->events[i].data.fd);
            }
        }
    }

    return 0;
}

void server_cleanup(server_t *server) {
    if (!server) {
        return;
    }
    
    if (server->epoll_fd != -1) {
        close(server->epoll_fd);
        server->epoll_fd = -1;
    }
    
    if (server->server_fd != -1) {
        close(server->server_fd);
        server->server_fd = -1;
    }
    
    memset(server, 0, sizeof(server_t));
}

int server_start(server_t *server) {
    int backlog = SOMAXCONN;
    
    char backlog_str[16] = {0};
    FILE *fp = fopen("/proc/sys/net/core/somaxconn", "r");
    if (fp) {
        if (fgets(backlog_str, sizeof(backlog_str), fp) != NULL) {
            int max_backlog = atoi(backlog_str);
            if (max_backlog > 0) {
                backlog = max_backlog;
                if (max_backlog < backlog) {
                    backlog = max_backlog;
                }
            }
        }
        fclose(fp);
        fp = NULL;
    }
    
    LOG_INFO("Using listen backlog size: %d", backlog);
    
    if (listen(server->server_fd, backlog) == -1) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        return -1;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  
    ev.data.fd = server->server_fd;
    
    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->server_fd, &ev) == -1) {
        LOG_ERROR("Failed to add server socket to epoll: %s", strerror(errno));
        return -1;
    }
    
    LOG_INFO("Server listening on port %d", 
             ntohs(server->server_addr.sin_port));
    
    return 0;
} 