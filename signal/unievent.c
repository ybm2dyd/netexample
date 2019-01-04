#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/select.h>

#include "socket_poll.h"

#define MAX_EVENT 64
#define SOCKET_TYPE_LISTEN 1
#define SOCKET_TYPE_CONNECT 2
#define SOCKET_TYPE_INVALID 3

static int pipe_fd[2];

struct event ev[MAX_EVENT];
// static int checkctrl = 1;
static fd_set rfds;
static int event_index = 0;
static int event_number = 0;

struct socket
{
	int fd;
	uint16_t type;
};

static void sig_handler(int sig)
{
	fprintf(stderr, "sig:%d\n", sig);

	int save_errno = errno;
	int msg = sig;
	send(pipe_fd[1], (char*)&msg, 1, 0);
	errno = save_errno;

}

void addsig(int sig)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);

}

static int do_listen(const char * host, int port, int backlog)
{
	int fd;
	int status;
	int reuse = 1;
	struct addrinfo ai_hints;
	struct addrinfo* ai_list = NULL;
	char portstr[16];
	if (host == NULL || host[0] == 0)
	{
		host = "0.0.0.0";	// INADDR_ANY
	}
	sprintf(portstr, "%d", port);
	memset(&ai_hints, 0, sizeof(ai_hints));
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_protocol = IPPROTO_TCP;
	ai_hints.ai_socktype = SOCK_STREAM;
	status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
	if (status != 0)
	{
		return -1;
	}
	fd = socket(ai_list->ai_family, ai_list->ai_socktype, 0);
	if (fd < 0)
	{
		goto _failed_fd;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int)) == -1) {
		goto _failed;
	}
	status = bind(fd, (struct sockaddr *)ai_list->ai_addr, ai_list->ai_addrlen);
	if (status != 0)
	{
		goto _failed;
	}
	if (listen(fd, 32) == -1)
	{
		goto _failed;
	}
	freeaddrinfo(ai_list);
	return fd;
_failed:
	close(fd);
_failed_fd:
	freeaddrinfo(ai_list);
	return -1;
}

static void
socket_keepalive(int fd)
{
	int keepalive = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive));
}

static int has_sig()
{
	struct timeval tv = { 0,0 };
	int retval;
	FD_SET(pipe_fd[0], &rfds);

	retval = select(pipe_fd[0], &rfds, NULL, NULL, &tv);
	if (retval  == 1)
	{
		return 1;
	}
	return 0;
}

static void
block_readpipe(int pipefd, void *buffer, int sz)
{
	for (;;) {
		int n = read(pipefd, buffer, sz);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "socket-server : read pipe error %s.\n", strerror(errno));
			return;
		}
		// must atomic read from a pipe
		assert(n == sz);
		return;
	}
}

int main(int argc, char*argv[])
{
	if (argc <= 2)
	{
		return 1;
	}
	const char* host = argv[1];
	int port = atoi(argv[2]);

	poll_fd efd = sp_create();
	if (sp_invalid(efd)) 
	{
		fprintf(stderr, "socket-server: create event pool failed.\n");
		goto _failed;
		return 0;
	}
	if (pipe(pipe_fd)) {
		goto _failed_pipe;
		fprintf(stderr, "socket-server: create socket pair failed.\n");
		return 0;
	}
	if (sp_add(efd, pipe_fd[0], NULL))
	{
		goto _failed_add;
		fprintf(stderr, "socket-server: can't add server fd to event pool.\n");
		return 0;
	}
	sp_nonblocking(pipe_fd[0]);
	sp_nonblocking(pipe_fd[1]);
	FD_ZERO(&rfds);

	int listen_fd = do_listen(host, port, 32);
	if (listen_fd < 0)
	{
		goto _failed_listen;
		fprintf(stderr, "socket-server: failed listen fd\n");
		return 0;
	}
	struct socket s;
	s.fd = listen_fd;
	s.type = SOCKET_TYPE_LISTEN;
	sp_add(efd, listen_fd, &s);
	
	addsig(SIGHUP);
	addsig(SIGCHLD);
	addsig(SIGTERM);
	addsig(SIGINT);

	for (;;)
	{
		event_number = sp_wait(efd, ev, MAX_EVENT);
		if (event_number < 0 && errno != EINTR)
		{
			fprintf(stderr, "epoll error\n");
			goto _exit_server;
		}
		event_index = 0;
		for (; event_index < event_number; event_index++)
		{
			struct event* e = &ev[event_index];
			struct socket* s = e->s;
			if (s == NULL)
			{
				fprintf(stderr, "pipe signal\n");
				char signals[1024];
				int ret = recv(pipe_fd[0], signals, sizeof(signals), 0);
				if (ret <= 0)
				{
					continue;
				}
				else
				{
					int i;
					for (i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
						case SIGCHLD:
						case SIGHUP:
						{
							continue;
						}
						case SIGTERM:
						case SIGINT:
						{
							fprintf(stderr, "socket-server: INT exit\n");
							goto _exit_server;
						}
						}
					}
				}
				if (has_sig())
				{
				
				}
				continue;
			}
			switch (s->type)
			{
			case SOCKET_TYPE_LISTEN:
			{
				struct sockaddr_in client_address;
				socklen_t client_addrlength = sizeof(client_address);
				int client_fd = accept(s->fd, (struct sockaddr*)&client_address, &client_addrlength);
				socket_keepalive(client_fd);
				sp_nonblocking(client_fd);
				struct socket cs;
				cs.fd = client_fd;
				cs.type = SOCKET_TYPE_CONNECT;
				sp_add(efd, client_fd, &cs);
				break;
			}

			}
		} 
	}

	//// poll
	//for (;;)
	//{
	//	if (checkctrl)
	//	{
	//		if (has_sig())
	//		{
	//			char signal;
	//			block_readpipe(pipe_fd[0], &signal, 1);
	//			switch (signal)
	//			{
	//			case SIGCHLD:
	//			case SIGHUP:
	//			{
	//				continue;
	//			}
	//			case SIGTERM:
	//			case SIGINT:
	//			{
	//				fprintf(stderr, "socket-server: INT exit\n");
	//				goto _exit_server;
	//			}
	//			}
	//		}
	//		else
	//		{
	//			checkctrl = 0;
	//		}
	//	}
	//	if (event_index == event_number)
	//	{
	//		event_number = sp_wait(efd, ev, MAX_EVENT);
	//		if (event_number <= 0)
	//		{
	//			fprintf(stderr, "epoll failure\n");
	//			goto _exit_server;
	//		}
	//		checkctrl = 1;
	//		event_index = 0;
	//	}
	//	struct event* e = &ev[event_index++];
	//	struct socket* s = e->s;
	//	if (s == NULL)
	//	{
	//		// dispatch sig
	//		continue;
	//	}
	//	switch (s->type)
	//	{
	//	case SOCKET_TYPE_LISTEN:
	//	{
	//		struct sockaddr_in client_address;
	//		socklen_t client_addrlength = sizeof(client_address);
	//		int client_fd = accept(s->fd, (struct sockaddr*)&client_address, &client_addrlength);
	//		socket_keepalive(client_fd);
	//		sp_nonblocking(client_fd);
	//		struct socket cs;
	//		cs.fd = client_fd;
	//		cs.type = SOCKET_TYPE_CONNECT;
	//		sp_add(efd, client_fd, &cs);
	//		break;
	//	}
	//	case SOCKET_TYPE_CONNECT:
	//		if (e->read)
	//		{

	//		}
	//		if (e->write)
	//		{

	//		}
	//		break;
	//	}
	//}

_exit_server:;
_failed_listen:
	close(listen_fd);
_failed_add:
	close(pipe_fd[0]);
	close(pipe_fd[1]);
_failed_pipe:
	sp_release(efd);
_failed:
	return 0;
}