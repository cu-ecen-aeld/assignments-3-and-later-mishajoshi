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

#define PORT 9000
#define BACKLOG 5
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;
char *filename = "/var/tmp/aesdsocketdata";

// Signal handler to cleanup on SIGINT or SIGTERM
void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);
    unlink(filename);
    closelog();
    exit(0);
}

int main(int argc, char *argv[]) {
	
	int daemon_flag = 0;

	if (argc == 2 && strcmp(argv[1], "-d") == 0) {
    		daemon_flag = 1;
	}

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Open syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Create TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        return -1;
    }

    // Bind socket
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

	if (daemon_flag) {
	    pid_t pid = fork();

	    if (pid < 0) {
	        perror("fork failed");
	        exit(EXIT_FAILURE);
	    }

	    if (pid > 0) {
	        // Parent exits
	        exit(EXIT_SUCCESS);
	    }

	    // Child continues
	    if (setsid() < 0) { // Start new session
	        perror("setsid failed");
	        exit(EXIT_FAILURE);
	    }

	    // Optional: change working directory and file permissions
	    chdir("/");
	    umask(0);

	    // Close standard file descriptors
	    close(STDIN_FILENO);
	    close(STDOUT_FILENO);
	    close(STDERR_FILENO);
	}



    // Listen
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }

    syslog(LOG_INFO, "Server listening on port %d", PORT);

    // Accept loop
    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen)) < 0) {
            perror("accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Open file for appending data
        int fd = open(filename, O_CREAT | O_APPEND | O_RDWR, 0644);
        if (fd < 0) {
            perror("open file failed");
            close(client_fd);
            client_fd = -1;
            continue;
        }

        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
            for (ssize_t i = 0; i < bytes_read; i++) {
                if (write(fd, &buffer[i], 1) != 1) {
                    perror("write to file failed");
                }
                if (buffer[i] == '\n') {
                    // Send entire file content back to client
                    lseek(fd, 0, SEEK_SET);
                    char file_buf[BUFFER_SIZE];
                    ssize_t file_bytes;
                    while ((file_bytes = read(fd, file_buf, sizeof(file_buf))) > 0) {
                        if (write(client_fd, file_buf, file_bytes) != file_bytes) {
                            perror("write to socket failed");
                            break;
                        }
                    }
                }
            }
        }

        close(fd);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        client_fd = -1;
    }

    // Cleanup (never reached due to infinite loop)
    close(server_fd);
    closelog();
    return 0;
}
