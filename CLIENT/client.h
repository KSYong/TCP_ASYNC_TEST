#pragma once
#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include "../COMMON/common.h"

#define BUF_MAX_LEN 1024
#define TIMEOUT 10000

/// @struct client_t
/// @brief server로 요청을 보내서 응답을 받기 위한 구조체 
typedef struct client_s client_t;
struct client_s{
	/// client tcp socket file descriptor
	int fd;
	/// client socket address
	struct sockaddr_in addr;
	/// client epoll handle file descriptor
	int epoll_handle_fd;
	/// client epoll event management structure
	struct epoll_event events[ BUF_MAX_LEN];
};

client_t* client_init();
void client_destroy( client_t* client);
int client_process( client_t* client);

#endif
