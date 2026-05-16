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

#define USE_AESD_CHAR_DEVICE 1

#if USE_AESD_CHAR_DEVICE
static const char *filename = "/dev/aesdchar";
#else
static const char *filename = "/var/tmp/aesdsocketdata";
#endif

int server_fd = -1;
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

struct thread_node {
    pthread_t tid;
    struct thread_node *next;
};

struct thread_node *thread_list = NULL;
pthread_mutex_t thread_list_lock = PTHREAD_MUTEX_INITIALIZER;

volatile sig_atomic_t exit_flag = 0;

/* ---------------- SIGNAL HANDLER ---------------- */
void signal_handler(int sig)
{
    (void)sig;
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;

    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

/* ---------------- THREAD LIST ---------------- */
void add_thread(pthread_t tid)
{
    struct thread_node *node = malloc(sizeof(struct thread_node));
    if (!node) return;

    node->tid = tid;
    node->next = NULL;

    pthread_mutex_lock(&thread_list_lock);

    if (!thread_list) {
        thread_list = node;
    } else {
        struct thread_node *tmp = thread_list;
        while (tmp->next)
            tmp = tmp->next;
        tmp->next = node;
    }

    pthread_mutex_unlock(&thread_list_lock);
}

/* ---------------- CLIENT HANDLER ---------------- */
void *client_handler(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg);

    int dev_fd = open(filename, O_RDWR);
    if (dev_fd < 0) {
        perror("open aesdchar failed");
        close(client_fd);
        return NULL;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0)
{
    pthread_mutex_lock(&file_lock);

    /* write full request */
    write(dev_fd, buffer, bytes_read);

    /* reset read position */
//    lseek(dev_fd, 0, SEEK_SET);

    /* read full snapshot */
    ssize_t file_bytes;
    char file_buf[BUFFER_SIZE];

    while ((file_bytes = read(dev_fd, file_buf, sizeof(file_buf))) > 0)
    {
        write(client_fd, file_buf, file_bytes);
    }

    pthread_mutex_unlock(&file_lock);
}

    close(dev_fd);
    close(client_fd);

    syslog(LOG_INFO, "Closed connection");
    return NULL;
}

/* ---------------- MAIN ---------------- */
int main(int argc, char *argv[])
{
    int daemon_flag = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0)
        daemon_flag = 1;

#if !USE_AESD_CHAR_DEVICE
    unlink(filename);
#endif

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        return -1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (daemon_flag) {
        pid_t pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);

        setsid();
        chdir("/");
        umask(0);

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }

    syslog(LOG_INFO, "Server listening on port %d", PORT);

    /* ---------------- ACCEPT LOOP ---------------- */
    while (!exit_flag)
    {
        int *new_client_fd = malloc(sizeof(int));
        if (!new_client_fd)
            continue;

        *new_client_fd = accept(server_fd,
                                (struct sockaddr *)&address,
                                &addrlen);

        if (*new_client_fd < 0) {
            free(new_client_fd);
            if (exit_flag) break;
            perror("accept failed");
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, new_client_fd) != 0) {
            perror("pthread_create failed");
            close(*new_client_fd);
            free(new_client_fd);
            continue;
        }

        add_thread(tid);
    }

    /* ---------------- CLEANUP ---------------- */
    pthread_mutex_lock(&thread_list_lock);
    struct thread_node *tmp = thread_list;

    while (tmp) {
        pthread_join(tmp->tid, NULL);
        struct thread_node *old = tmp;
        tmp = tmp->next;
        free(old);
    }

    pthread_mutex_unlock(&thread_list_lock);

    close(server_fd);
    closelog();

    return 0;
}
