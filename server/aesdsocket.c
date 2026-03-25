
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <syslog.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define PORT 9000
#define BACKLOG 5
#define BUFFER_SIZE 1024

int server_fd = -1;
int global_fd = -1; // single file descriptor for /var/tmp/aesdsocketdata
char *filename = "/var/tmp/aesdsocketdata";
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

struct thread_node {
    pthread_t tid;
    struct thread_node *next;
};
struct thread_node *thread_list = NULL;
pthread_mutex_t thread_list_lock = PTHREAD_MUTEX_INITIALIZER;

volatile sig_atomic_t exit_flag = 0;

// Signal handler
void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
    if (server_fd != -1) close(server_fd); // unblock accept
}

// Add thread to linked list
void add_thread(pthread_t tid) {
    struct thread_node *node = malloc(sizeof(struct thread_node));
    if (!node) return;
    node->tid = tid;
    node->next = NULL;

    pthread_mutex_lock(&thread_list_lock);
    if (!thread_list) thread_list = node;
    else {
        struct thread_node *tmp = thread_list;
        while (tmp->next) tmp = tmp->next;
        tmp->next = node;
    }
    pthread_mutex_unlock(&thread_list_lock);
}

// Client thread
void *client_handler(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in peer;
    socklen_t len = sizeof(peer);
    if (getpeername(client_fd, (struct sockaddr *)&peer, &len) == 0) {
        inet_ntop(AF_INET, &peer.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    } else strcpy(client_ip, "unknown");

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        pthread_mutex_lock(&file_lock);
        if (write(global_fd, buffer, bytes_read) != bytes_read) {
            perror("write to file failed");
        }
        fsync(global_fd); // flush immediately
        pthread_mutex_unlock(&file_lock);

        // Echo file content back to client
        pthread_mutex_lock(&file_lock);
        lseek(global_fd, 0, SEEK_SET);
        ssize_t file_bytes;
        char file_buf[BUFFER_SIZE];
        while ((file_bytes = read(global_fd, file_buf, sizeof(file_buf))) > 0) {
            if (write(client_fd, file_buf, file_bytes) != file_bytes) {
                perror("write to socket failed");
                break;
            }
        }
        pthread_mutex_unlock(&file_lock);
    }

    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    return NULL;
}

// Timestamp thread
void *timestamp_thread_func(void *arg) {
    (void)arg;
    char timestamp[128];
    while (!exit_flag) {
        sleep(10);
        time_t now = time(NULL);
        struct tm *tm_info = gmtime(&now);

        strftime(timestamp, sizeof(timestamp),
                 "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

        pthread_mutex_lock(&file_lock);
        if (write(global_fd, timestamp, strlen(timestamp)) != (ssize_t)strlen(timestamp)) {
            perror("timestamp write failed");
        }
        fsync(global_fd);
        pthread_mutex_unlock(&file_lock);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int daemon_flag = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) daemon_flag = 1;

    unlink(filename); // remove old file

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Open global file once
    global_fd = open(filename, O_CREAT | O_APPEND | O_RDWR | O_SYNC, 0644);
    if (global_fd < 0) { perror("open file failed"); return -1; }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed"); return -1;
    }
	
	int reuse = 1;
if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt failed");
    close(server_fd);
    return -1;
}

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed"); close(server_fd); return -1;
    }

    // Daemonize
    if (daemon_flag) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork failed"); exit(EXIT_FAILURE); }
        if (pid > 0) exit(EXIT_SUCCESS);

        if (setsid() < 0) { perror("setsid failed"); exit(EXIT_FAILURE); }

       // chdir("/"); umask(0);
	if (chdir("/") != 0) {
    	perror("chdir");
    	return -1;
	}
	umask(0);
        close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed"); close(server_fd); return -1;
    }
    syslog(LOG_INFO, "Server listening on port %d", PORT);

    // Start timestamp thread
    pthread_t ts_thread;
    if (pthread_create(&ts_thread, NULL, timestamp_thread_func, NULL) != 0) {
        perror("Failed to create timestamp thread"); close(server_fd); return -1;
    }

    // Accept loop
    while (!exit_flag) {
        int *new_client_fd = malloc(sizeof(int));
        if (!new_client_fd) continue;

        *new_client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (*new_client_fd < 0) {
            free(new_client_fd);
            if (exit_flag) break;
            perror("accept failed");
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, new_client_fd) != 0) {
            perror("pthread_create failed"); close(*new_client_fd); free(new_client_fd); continue;
        }
        add_thread(tid);
    }

    // Join client threads
    pthread_mutex_lock(&thread_list_lock);
    struct thread_node *tmp = thread_list;
    while (tmp) {
        pthread_join(tmp->tid, NULL);
        struct thread_node *del = tmp;
        tmp = tmp->next;
        free(del);
    }
    pthread_mutex_unlock(&thread_list_lock);

    // Join timestamp thread
    pthread_join(ts_thread, NULL);

    close(global_fd);
    close(server_fd);
    closelog();
    return 0;
}
