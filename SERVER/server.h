#pragma once 

#ifndef __SERVER_H__
#define __SERVER_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include "../COMMON/common.h"

#define MSG_QUEUE_NUM 10
#define BUF_MAX_LEN 1024
#define SERVER_PORT 8000
#define TIMEOUT 10000

/// @struct server_t
/// @brief client의 요청에 따른 응답을 처리하기 위한 구조체 
typedef struct server_s server_t;
struct server_s{
	/// server tcp socket file descriptor
	int fd;
	/// server socket address
	struct sockaddr_in addr;
	/// server epoll handle file descriptor
	int epoll_handle_fd;
	/// server epoll event management structure
	struct epoll_event events[ BUF_MAX_LEN];
};

server_t* server_init();
void server_destroy( server_t* server);
int server_conn( server_t* server);

#endif
