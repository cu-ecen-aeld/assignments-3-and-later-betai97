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
#include <pthread.h>
#include <sys/queue.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include "../aesd-char-driver/aesd_ioctl.h"

// Build switch for whether to use our char driver
#define USE_AESD_CHAR_DEVICE 1

// Macros
#define SOCK_SERV_PORT "9000"
#define SOCK_ERR (-1)
#define MAX_CONNECTIONS (10)
#define SOCK_DATA_FILE ((USE_AESD_CHAR_DEVICE) ? "/dev/aesdchar" : "/var/tmp/aesdsocketdata")
#define RECV_BUF_LEN (2048)

// Globals for freeing resources in signal handler
int g_sockfd;
struct addrinfo *g_servinfo;
FILE *g_data_file;
pthread_t g_timer_thread;
pthread_mutex_t g_sockdata_mutex;

SLIST_HEAD(tdata_head, tdata) g_head;


// LL struct for each elem
struct tdata {
	bool complete;
	int accepted_sock_fd;
	char ip_addr[20];
	char *dyn_recv_buf;
	pthread_t thread;
	SLIST_ENTRY(tdata) ptrs;
};

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

struct tdata *create_tdata(int accepted_sock_fd) {
	struct tdata *tdata_ptr = (struct tdata *) malloc(sizeof(struct tdata));
	if(tdata_ptr == NULL) {
		return NULL;
	}

	tdata_ptr->complete = 0;
	tdata_ptr->accepted_sock_fd = accepted_sock_fd;
	tdata_ptr->dyn_recv_buf = NULL;

	return tdata_ptr;
}

void destroy_tdata(struct tdata *tdata_ptr) {
	if(tdata_ptr != NULL) {
		free(tdata_ptr);
	}
}

void *connection_thread_func(void* thread_param) {
	struct tdata *tdata_ptr = (struct tdata *) thread_param;

	// Log IP of accepted connection
	syslog(LOG_DEBUG, "Accepted connection from %s\n", tdata_ptr->ip_addr);

	// receive data
	char *dyn_recv_buf = NULL;
	int recv_size = 0;
	int recv_size_tot = 0;
	int contains_newlin = 0;
	char recv_buf[RECV_BUF_LEN] = {0};
	struct aesd_seekto seekto;
	int ioc_ret = 0, ioc_fd = 0;;
	while(1) {
		recv_size = (int) recv(tdata_ptr->accepted_sock_fd, recv_buf, RECV_BUF_LEN, 0);
		recv_size_tot += recv_size;
		dyn_recv_buf = realloc(dyn_recv_buf, recv_size_tot);
		if(dyn_recv_buf == NULL) {
			fprintf(stderr, "Couldn't allocate more memeory\n");
			continue;
		}
		memset(dyn_recv_buf+(recv_size_tot-recv_size), 0, recv_size);
		memcpy(dyn_recv_buf+(recv_size_tot-recv_size), recv_buf, recv_size);
		
		contains_newlin = contains_chr(recv_buf, '\n', recv_size);
		if(contains_newlin) {
			break;
		}
		memset(recv_buf, 0, RECV_BUF_LEN);
	}

#ifdef USE_AESD_CHAR_DEVICE
	if(strncmp(dyn_recv_buf, "AESDCHAR_IOCSEEKTO", 18) == 0) {
		seekto.write_cmd = dyn_recv_buf[19] - '0';
		seekto.write_cmd_offset = dyn_recv_buf[21] - '0';

		g_data_file = fopen(SOCK_DATA_FILE, "r");
		ioc_fd = fileno(g_data_file);

		ioc_ret = ioctl(ioc_fd, AESDCHAR_IOCSEEKTO, &seekto);

		if(ioc_ret != 0) {
			fprintf(stderr, "ioctl err: %d\n", ioc_ret);
		}

		goto after_write;
	}
#endif

	// Write packet to file
#ifndef USE_AESD_CHAR_DEVICE
	pthread_mutex_lock(&g_sockdata_mutex);
#endif
	g_data_file = fopen(SOCK_DATA_FILE, "a+");
	if(g_data_file == NULL) {
		fprintf(stderr, "Couldn't open file\n");
		return thread_param;
	}
	fwrite(dyn_recv_buf, 1, recv_size_tot, g_data_file);

after_write:

	free(dyn_recv_buf);
	dyn_recv_buf = NULL;
	fclose(g_data_file);
	g_data_file = NULL;
#ifndef USE_AESD_CHAR_DEVICE
	pthread_mutex_unlock(&g_sockdata_mutex);
#endif

	// Send back data from file
	// Note: line-by-line read code learned from https://stackoverflow.com/questions/3501338/c-read-file-line-by-line
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
#ifndef USE_AESD_CHAR_DEVICE
	pthread_mutex_lock(&g_sockdata_mutex);
#endif
	g_data_file = fopen(SOCK_DATA_FILE, "r");
	if(g_data_file == NULL) {
		fprintf(stderr, "Couldn't open file\n");
		return thread_param;
	}
	while((read = getline(&line, &len, g_data_file)) != -1) {
		if(send(tdata_ptr->accepted_sock_fd, line, read, 0) == SOCK_ERR) {
			fprintf(stderr, "socket send err\n");
		}
	}

	fclose(g_data_file);
#ifndef USE_AESD_CHAR_DEVICE
	pthread_mutex_unlock(&g_sockdata_mutex);
#endif
	g_data_file = NULL;
	free(line);


	tdata_ptr->complete = true;
	close(tdata_ptr->accepted_sock_fd);

	// Log connection close
	syslog(LOG_DEBUG, "Closed connection from %s\n", tdata_ptr->ip_addr);

	return thread_param;
}

// Signal handler for SIGINT and SIGTERM
// Free all dynamic memory, close all files & sockets
void handle_kill(int sig) {
	struct tdata *tdata_cur = NULL;
	void *t_ret = NULL;

	syslog(LOG_DEBUG, "Caught signal, exiting\n");

	while (!SLIST_EMPTY(&g_head)) {
        tdata_cur = SLIST_FIRST(&g_head);
        if(pthread_join(tdata_cur->thread, &t_ret) != 0) {
			fprintf(stderr, "thread join fail. cant free memory without thread. bad!\n");
		} else {
	        SLIST_REMOVE(&g_head, tdata_cur, tdata, ptrs);
	        destroy_tdata(tdata_cur);
    	}
    }

	if(g_sockfd)
		close(g_sockfd);
	if(g_data_file != NULL)
		fclose(g_data_file);
	if(g_servinfo != NULL)
		freeaddrinfo(g_servinfo);
#ifndef USE_AESD_CHAR_DEVICE
	if(remove(SOCK_DATA_FILE) != 0) {
		fprintf(stderr, "Error deleting %s\n", SOCK_DATA_FILE);
		exit(1);
	}
#endif
	pthread_mutex_destroy(&g_sockdata_mutex);


	pthread_cancel(g_timer_thread);

	exit(0);
}

// Thread for timer
void *timer_thread(void *data) {
	struct tdata *tdata_cur = NULL;
	struct tdata **tdata_ptrs = NULL;
	void *t_ret = NULL;
	int num_join = 0;

#ifndef USE_AESD_CHAR_DEVICE
	time_t t;
	struct tm *tm_p;
	char time_str[30] = {0};
#endif

	while(1) {
		sleep(10);

#ifndef USE_AESD_CHAR_DEVICE

		time(&t);
		tm_p = localtime(&t);
		strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z", tm_p);

		pthread_mutex_lock(&g_sockdata_mutex);
		g_data_file = fopen(SOCK_DATA_FILE, "a+");
		if(g_data_file == NULL) {
			fprintf(stderr, "Couldn't open file\n");
			return NULL;
		}

		fputs("timestamp:", g_data_file);
		fputs(time_str, g_data_file);
		fputs("\n", g_data_file);
		fclose(g_data_file);
		pthread_mutex_unlock(&g_sockdata_mutex);

#endif

		// Mechanism for join of threads while server is still running
		num_join = 0;
		tdata_ptrs = NULL;
		tdata_cur = NULL;
		t_ret = NULL;
		SLIST_FOREACH(tdata_cur, &g_head, ptrs) {
			if(tdata_cur->complete == true) {
				if(pthread_join(tdata_cur->thread, &t_ret) != 0) {
					fprintf(stderr, "thread join fail\n");
				} else {
					num_join++;
					tdata_ptrs = (struct tdata**) realloc(tdata_ptrs, sizeof(struct tdata*)*num_join);
					tdata_ptrs[num_join-1] = tdata_cur;
					printf("Joined thread on sockfd %d\n", tdata_cur->accepted_sock_fd);
				}
			}
		}

		for(int i=0; i<num_join; i++) {
			printf("Deleting mem for thread on sockfd %d\n", tdata_ptrs[i]->accepted_sock_fd);

			SLIST_REMOVE(&g_head, tdata_ptrs[i], tdata, ptrs);
			destroy_tdata(tdata_ptrs[i]);
		}

		if(tdata_ptrs != NULL)
			free(tdata_ptrs);
	}

}

int main(int argc, char *argv[])
{
	// Step 1: Setup

	SLIST_INIT(&g_head);

	pthread_mutex_init(&g_sockdata_mutex, NULL);
	

	// Part A: Set up addrinfo

	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_addr = NULL;
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

	// Start timer thread
	pthread_create(&g_timer_thread, NULL, timer_thread, NULL);

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
	int accepted_sock_fd;

	while(1) {
		// Accept connection
		accepted_sock_fd = accept(g_sockfd, client_sockaddr, &sockaddr_len);

		if(accepted_sock_fd == SOCK_ERR) {
			fprintf(stderr, "socket accept err\n");
			continue;
		}

		struct tdata *tdata_ptr = create_tdata(accepted_sock_fd);
		if(tdata_ptr == NULL) {
			fprintf(stderr, "Failed to allocate thread obj space!\n");
			continue;
		}
		SLIST_INSERT_HEAD(&g_head, tdata_ptr, ptrs);
		sprintf(tdata_ptr->ip_addr, "%s", inet_ntoa(((struct sockaddr_in*)&client_sockaddr)->sin_addr) );

		if(pthread_create(&tdata_ptr->thread, NULL, connection_thread_func, (void*) tdata_ptr) != 0) {
			fprintf(stderr, "Failed to create thread!\n");
			SLIST_REMOVE(&g_head, tdata_ptr, tdata, ptrs);
			destroy_tdata(tdata_ptr);
			continue;
		}
		
	}

	return 0;
}