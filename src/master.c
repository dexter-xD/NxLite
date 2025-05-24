#include "master.h"


static master_t *master_instance = NULL;
static pid_t *worker_pids = NULL;

static void handle_child_signal(int signo __attribute__((unused))) {
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        LOG_INFO("Worker process %d exited with status %d", pid, WEXITSTATUS(status));
        
        if (master_instance && master_instance->is_running) {
            for (int i = 0; i < master_instance->worker_count; i++) {
                if (worker_pids[i] == pid) {
                    LOG_INFO("Restarting worker %d", i);
                    pid_t new_pid = fork();
                    if (new_pid == 0) {
                        worker_t worker;
                        if (worker_init(&worker, master_instance->server_fd, i) == 0) {
                            worker_run(&worker);
                            worker_cleanup(&worker);
                        }
                        exit(0);
                    } else if (new_pid > 0) {
                        worker_pids[i] = new_pid;
                        LOG_INFO("Worker %d restarted with PID %d", i, new_pid);
                    } else {
                        LOG_ERROR("Failed to fork worker process: %s", strerror(errno));
                    }
                    break;
                }
            }
        }
    }
}

static int configure_tcp_socket(int sockfd) {
    int opt = 1;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set SO_KEEPALIVE: %s", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set TCP_NODELAY: %s", strerror(errno));
        return -1;
    }
    
    int secs = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &secs, sizeof(secs)) < 0) {
        LOG_WARN("Failed to set TCP_DEFER_ACCEPT: %s (continuing anyway)", strerror(errno));
    }
    
    int snd_buf = 65536;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf)) < 0) {
        LOG_ERROR("Failed to set SO_SNDBUF: %s", strerror(errno));
        return -1;
    }
    
    int rcv_buf = 65536;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcv_buf, sizeof(rcv_buf)) < 0) {
        LOG_ERROR("Failed to set SO_RCVBUF: %s", strerror(errno));
        return -1;
    }
    
    int keepidle = 60;  
    int keepintvl = 10;
    int keepcnt = 6;    
    
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
        LOG_ERROR("Failed to set TCP_KEEPIDLE: %s", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
        LOG_ERROR("Failed to set TCP_KEEPINTVL: %s", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
        LOG_ERROR("Failed to set TCP_KEEPCNT: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

static int set_worker_cpu_affinity(int worker_id) {
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus <= 0) {
        LOG_WARN("Failed to get CPU count, not setting CPU affinity");
        return -1;
    }
    
    int cpu_id = worker_id % num_cpus;
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        LOG_WARN("Failed to set CPU affinity for worker %d: %s", 
                worker_id, strerror(errno));
        return -1;
    }
    
    LOG_INFO("Worker %d assigned to CPU %d", worker_id, cpu_id);
    return cpu_id;
}

static pid_t fork_worker(master_t *master, int worker_id) {
    pid_t pid = fork();
    
    if (pid == -1) {
        LOG_ERROR("Failed to fork worker process: %s", strerror(errno));
        return -1;
    } else if (pid == 0) {
        LOG_INFO("Worker %d started with PID %d", worker_id, getpid());
        
        int cpu_id = set_worker_cpu_affinity(worker_id);
        if (cpu_id < 0) {
            cpu_id = worker_id; 
        }
        
        worker_t worker;
        if (worker_init(&worker, master->server_fd, cpu_id) == 0) {
            worker_run(&worker);
            worker_cleanup(&worker);
        }
        
        LOG_INFO("Worker %d exiting", worker_id);
        exit(0);
    }
    
    return pid;
}

int master_init(master_t *master, int port, int worker_count) {
    if (!master || worker_count <= 0) {
        return -1;
    }

    memset(master, 0, sizeof(master_t));
    master->port = port;
    master->worker_count = worker_count;
    master->is_running = 1;
    master_instance = master;

    master->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (master->server_fd == -1) {
        LOG_ERROR("Failed to create server socket: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(master->server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        LOG_ERROR("Failed to set SO_REUSEPORT: %s", strerror(errno));
        close(master->server_fd);
        return -1;
    }

    if (configure_tcp_socket(master->server_fd) != 0) {
        LOG_ERROR("Failed to configure TCP socket options");
        close(master->server_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(master->server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        LOG_ERROR("Failed to bind to port %d: %s", port, strerror(errno));
        close(master->server_fd);
        return -1;
    }

    if (listen(master->server_fd, SOMAXCONN) == -1) {
        LOG_ERROR("Failed to listen: %s", strerror(errno));
        close(master->server_fd);
        return -1;
    }

    worker_pids = calloc(worker_count, sizeof(pid_t));
    if (!worker_pids) {
        LOG_ERROR("Failed to allocate worker PID array");
        close(master->server_fd);
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_child_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        LOG_ERROR("Failed to set up SIGCHLD handler: %s", strerror(errno));
        free(worker_pids);
        close(master->server_fd);
        return -1;
    }

    return 0;
}

void master_run(master_t *master) {
    if (!master) {
        return;
    }

    LOG_INFO("Starting master process with %d workers", master->worker_count);

    for (int i = 0; i < master->worker_count; i++) {
        pid_t pid = fork_worker(master, i);
        if (pid > 0) {
            worker_pids[i] = pid;
            LOG_INFO("Started worker %d with PID %d", i, pid);
        }
    }

    nice(5); 
    
    time_t last_stats_time = time(NULL);
    int stats_interval = 60; 
    
    while (master->is_running && !shutdown_requested) {
        sleep(1);
        
        for (int i = 0; i < master->worker_count; i++) {
            if (worker_pids[i] <= 0) {
                LOG_INFO("Restarting missing worker %d", i);
                pid_t pid = fork_worker(master, i);
                if (pid > 0) {
                    worker_pids[i] = pid;
                    LOG_INFO("Restarted worker %d with PID %d", i, pid);
                }
            }
        }
        
        time_t now = time(NULL);
        if (now - last_stats_time >= stats_interval) {
            LOG_INFO("Master process running with %d workers", master->worker_count);
            last_stats_time = now;
        }
    }

    LOG_INFO("Master shutting down, sending SIGTERM to workers");
    for (int i = 0; i < master->worker_count; i++) {
        if (worker_pids[i] > 0) {
            kill(worker_pids[i], SIGTERM);
        }
    }

    int timeout = 5; 
    time_t start_time = time(NULL);
    int all_exited = 0;
    
    while (!all_exited && time(NULL) - start_time < timeout) {
        all_exited = 1;
        for (int i = 0; i < master->worker_count; i++) {
            if (worker_pids[i] > 0) {
                int status;
                pid_t result = waitpid(worker_pids[i], &status, WNOHANG);
                if (result == 0) {
                    all_exited = 0;
                } else if (result > 0) {
                    worker_pids[i] = 0;
                    LOG_INFO("Worker %d exited with status %d", i, WEXITSTATUS(status));
                }
            }
        }
        
        if (!all_exited) {
            usleep(100000); 
        }
    }

    for (int i = 0; i < master->worker_count; i++) {
        if (worker_pids[i] > 0) {
            LOG_WARN("Worker %d (PID %d) did not exit gracefully, sending SIGKILL", 
                    i, worker_pids[i]);
            kill(worker_pids[i], SIGKILL);
            waitpid(worker_pids[i], NULL, 0);
        }
    }

    LOG_INFO("Master process exiting");
}

void master_cleanup(master_t *master) {
    if (!master) {
        return;
    }

    if (master->server_fd != -1) {
        close(master->server_fd);
        master->server_fd = -1;
    }

    if (worker_pids) {
        free(worker_pids);
        worker_pids = NULL;
    }

    master_instance = NULL;
}

void master_handle_signal(int signum) {
    if (!master_instance) {
        return;
    }

    switch (signum) {
        case SIGTERM:
        case SIGINT:
            LOG_INFO("Received termination signal %d", signum);
            master_instance->is_running = 0;
            break;
        case SIGHUP:
            LOG_INFO("Received reload signal");
            config_t *config = config_get_instance();
            if (config_load(config, NULL) == 0) {
                LOG_INFO("Configuration reloaded successfully");
                
                for (int i = 0; i < master_instance->worker_count; i++) {
                    if (worker_pids[i] > 0) {
                        kill(worker_pids[i], SIGHUP);
                    }
                }
            } else {
                LOG_ERROR("Failed to reload configuration");
            }
            break;
    }
}

master_t* master_get_instance(void) {
    return master_instance;
} 