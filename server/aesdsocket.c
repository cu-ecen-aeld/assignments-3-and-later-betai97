// File: aesdsocket.c
// Author: Ben Tait

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>

// Macros
#define SOCK_SERV_PORT "9000"
#define SOCK_ERR (-1)
#define MAX_CONNECTIONS (10)
#define SOCK_DATA_FILE "/var/tmp/aesdsocketdata"
#define RECV_BUF_LEN (2048)

// Globals for freeing resources in signal handler
int g_accepted_sock_fd = SOCK_ERR;
int g_sockfd;
struct addrinfo *g_servinfo;
FILE *g_data_file;
char *g_dyn_recv_buf;

// Signal handler for SIGINT and SIGTERM
// Free all dynamic memory, close all files & sockets
void handle_kill(int sig) {
	syslog(LOG_DEBUG, "Caught signal, exiting\n");
	if(g_accepted_sock_fd)
		close(g_accepted_sock_fd);
	if(g_sockfd)
		close(g_sockfd);
	if(g_data_file != NULL)
		fclose(g_data_file);
	if(g_servinfo != NULL)
		freeaddrinfo(g_servinfo);
	if(g_dyn_recv_buf != NULL)
		free(g_dyn_recv_buf);

	if(remove(SOCK_DATA_FILE) != 0) {
		fprintf(stderr, "Error deleting %s\n", SOCK_DATA_FILE);
		exit(1);
	}

	exit(0);
}

// Check if string contains char c
// return 1 if it does, otherwise 0
int contains_chr(char *str, char c, int len) {
	for(int i=0; i<len; i++) {
		if(str[i] == c) {
			return 1;
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	// Step 1: Setup

	// Part A: Set up addrinfo

	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	int status;

	if((status = getaddrinfo(NULL, SOCK_SERV_PORT, &hints, &g_servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo err: %s\n", gai_strerror(status));
		return 1;
	}

	// Part B: Set up Syslog
	openlog(NULL, LOG_PID | LOG_CONS | LOG_PERROR, LOG_USER);

	// Part C: Register signal handlers
	signal(SIGINT, handle_kill);
	signal(SIGTERM, handle_kill);

	// Step 2: Create socket fd

	g_sockfd = socket(PF_INET, SOCK_STREAM, 0);

	if(g_sockfd == SOCK_ERR) {
		freeaddrinfo(g_servinfo);
		fprintf(stderr, "socket err\n");
		closelog();
		return 1;
	}

	// note: line taken from https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
	if (setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
		freeaddrinfo(g_servinfo);
		fprintf(stderr, "setsockopt err\n");
		closelog();
		return 1;
	}

	// Step 3: Bind the socket

	int bind_res = bind(g_sockfd, g_servinfo->ai_addr, sizeof(struct sockaddr));

	if(bind_res == SOCK_ERR) {
		freeaddrinfo(g_servinfo);
		fprintf(stderr, "bind err\n");
		closelog();
		return 1;
	}

	// Step 3a: Run as daemon if requested
	if(argc > 1) {
		if(strcmp(argv[1], "-d") == 0) {
			daemon(0, 0);
		}
	}

	// Step 4: Listen for socket connections

	int listen_res = listen(g_sockfd, MAX_CONNECTIONS);

	if(listen_res == SOCK_ERR) {
		freeaddrinfo(g_servinfo);
		fprintf(stderr, "listen err\n");
		closelog();
		return 1;
	}

	// Step 5: Accept and handle connections

	struct sockaddr *client_sockaddr = NULL;
	socklen_t sockaddr_len = 0;
	g_data_file = NULL;
	int recv_size = 0;
	int recv_size_tot = 0;
	int contains_newlin = 0;
	g_dyn_recv_buf = NULL;

	while(1) {
		// Accept connection
		g_accepted_sock_fd = accept(g_sockfd, client_sockaddr, &sockaddr_len);

		if(g_accepted_sock_fd == SOCK_ERR) {
			fprintf(stderr, "socket accept err\n");
			continue;
		}

		// Log IP of accepted connection
		syslog(LOG_DEBUG, "Accepted connection from %s\n", inet_ntoa(((struct sockaddr_in*)&client_sockaddr)->sin_addr));

		// receive data
		recv_size_tot = 0;
		char recv_buf[RECV_BUF_LEN] = {0};
		while(1) {
			recv_size = (int) recv(g_accepted_sock_fd, recv_buf, RECV_BUF_LEN, 0);
			recv_size_tot += recv_size;
			g_dyn_recv_buf = realloc(g_dyn_recv_buf, recv_size_tot);
			if(g_dyn_recv_buf == NULL) {
				fprintf(stderr, "Couldn't allocate more memeory\n");
				continue;
			}
			memset(g_dyn_recv_buf+(recv_size_tot-recv_size), 0, recv_size);
			memcpy(g_dyn_recv_buf+(recv_size_tot-recv_size), recv_buf, recv_size);
			
			contains_newlin = contains_chr(recv_buf, '\n', recv_size);
			if(contains_newlin) {
				break;
			}
			memset(recv_buf, 0, RECV_BUF_LEN);
		}

		if(g_dyn_recv_buf == NULL)
			continue;

		// Write packet to file
		g_data_file = fopen(SOCK_DATA_FILE, "a+");
		if(g_data_file == NULL) {
			fprintf(stderr, "Couldn't open file\n");
			return 1;
		}
		fwrite(g_dyn_recv_buf, 1, recv_size_tot, g_data_file);
		free(g_dyn_recv_buf);
		g_dyn_recv_buf = NULL;
		fclose(g_data_file);
		g_data_file = NULL;

		// Send back data from file
		// Note: line-by-line read code learned from https://stackoverflow.com/questions/3501338/c-read-file-line-by-line
		char *line = NULL;
		size_t len = 0;
		ssize_t read;
		g_data_file = fopen(SOCK_DATA_FILE, "r");
		if(g_data_file == NULL) {
			fprintf(stderr, "Couldn't open file\n");
			return 1;
		}
		while((read = getline(&line, &len, g_data_file)) != -1) {
			if(send(g_accepted_sock_fd, line, read, 0) == SOCK_ERR) {
				fprintf(stderr, "socket send err\n");
			}
		}

		fclose(g_data_file);
		g_data_file = NULL;
		free(line);

		// Log connection close
		syslog(LOG_DEBUG, "Closed connection from %s\n", inet_ntoa(((struct sockaddr_in*)&client_sockaddr)->sin_addr));
		shutdown(g_accepted_sock_fd, 2);
		close(g_accepted_sock_fd);

	}

	close(g_sockfd);
	freeaddrinfo(g_servinfo);
	closelog();

	return 0;
}