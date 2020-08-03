#include "server.h"

// ----------------------------------------------------------

static int is_finish = false;
static int is_error = false;

// -----------------------------------------------------------

/**
 * @fn static int server_set_fd_nonblock( int fd)
 * @brief file descriptor를 block되지 않게(비동기로) 설정하는 함수
 * @return 정상적으로 설정되면 NORMAL, 비정상적이면 FD_ERR 반환
 * @param fd 확인할 server file descriptor
 */
static int server_set_fd_nonblock( int fd){
	int rv = 0;

	if((rv = fcntl( fd, F_GETFL, 0)) < 0){
		printf("	| ! Server : fcntl error (F_GETFL)\n");
		return FD_ERR;
	}

	if((rv = fcntl( fd, F_SETFL, rv | O_NONBLOCK)) < 0){
		printf("	| ! Server : fcntl error (F_SETFL & O_NONblOCK)\n");
		return FD_ERR;
	}

	return NORMAL;
}

/**
 * @fn static int server_check_fd( int fd)
 * @brief src file descriptor가 연결되어 있는지 확인
 * @return 정상 연결되어 있으면 NORMAL, 비정상 연결이면 FD_ERR 반환
 * @param fd 연결을 확인할 file descriptor
 */
static int server_check_fd( int fd){
	int error = 0;
	socklen_t err_len = sizeof( error);
	if ( getsockopt( fd, SOL_SOCKET, SO_ERROR, &error, &err_len) < 0){
		return FD_ERR;
	}
	return NORMAL;
}

/**
 * @fn static int server_process_data( server_t *server, int fd)
 * @brief Server가 Client로 데이터를 보낼 때, Server에서 메시지 송수신을 처리하기 위한 함수 
 * @return 열거형 참고 
 * @param server 서버의 정보를 담고 있는 server_t 구조체 객체
 * @param fd 연결된 client file descriptor
 */
static int server_process_data( server_t* server, int fd){
	int i, event_count = 0;
	char read_buf[ BUF_MAX_LEN];
	char send_buf[ BUF_MAX_LEN];
	ssize_t recv_bytes, send_bytes;
	char msg[ BUF_MAX_LEN];

	memset(read_buf, 0, BUF_MAX_LEN);
	memset(send_buf, 0, BUF_MAX_LEN);
	strcpy(send_buf, "TCP communication success!");

	printf("	| @ Server : waiting...\n");
	while( 1){
		if( ( server_check_fd( fd) == FD_ERR)){
			printf("	| @ Server : connection is ended (client:%d <-> server)\n", fd);
			break;
		}

		event_count = epoll_wait( server->epoll_handle_fd, server->events, BUF_MAX_LEN, TIMEOUT);
		if( event_count < 0){
			printf("	| ! Server : epoll_wait error\n");
			break;
		}
		else if( event_count == 0){
			printf("	| ! Server : epoll_wait timeout\n");
			continue;
		}

		for( i = 0; i < event_count; i++){
			if( server->events[ i].data.fd == server->fd){
				// 서버 소켓에서 이벤트가 발생했을 때
			}
			else if( server->events[ i].data.fd == fd ){
				// 클라이언트 소켓에서 이벤트가 발생했을 때
				if( server->events[ i].events & EPOLLIN){
					if( ( recv_bytes = read( fd, read_buf, sizeof(read_buf))) <= 0){
						printf("	| ! Server : Failed to recv msg (bytes:%d) (errno:%d)\n\n", recv_bytes, errno);
						break;
					}
					else{
						read_buf[ recv_bytes] = '\0';
						printf("	| ! @ Server : < %s ( %lu bytes)\n", read_buf, recv_bytes);

						if( ( send_bytes = write( fd, send_buf, sizeof(send_buf))) <= 0){
							printf("	| ! Server : Failed to send msg (bytes:%d) (errno:%d)\n", send_bytes, errno);
						}
						else{
							printf("	| @ Server : Success to send msg to Client (bytes:%d)\n", send_bytes);
						}
					}
				}
			}
			printf("\n");
		}
	}
}

// -----------------------------------------------------------------------------------

/**
 * @fn server_t* server_init( char **argv)
 * @brief server 객체를 생성하고 초기화하는 함수
 * @return 생성된 server 객체
 * @param **argv IP와 PORT 정보
 */
server_t* server_init( char **argv){
	server_t *server = ( server_t*)malloc( sizeof( server_t));

	if( server == NULL){
		printf("	| ! Server : Failed to allocate memory\n");
		return NULL;
	}

	memset( &server->addr, 0, sizeof( struct sockaddr));
	server->addr.sin_family = AF_INET;
	server->addr.sin_addr.s_addr = inet_addr( argv[1]);
	// inet_aton이 inet_addr보다 명확한 에러 리턴을 갖고 있어서 리눅스 매뉴얼 페이지에서는 inet_addr 대체 함수로 권장하고 있다. 다만 inet_addr과 달리 inet_aton은 POSIX.1-2001에 포함되어있지 않다.
	server->addr.sin_port = htons( atoi( argv[2]));

	if( ( server->fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
		printf("	| ! Server : Failed to open socket\n");
		free( server);
		return NULL;
	}

	int reuse = 1;
	// 소켓 세부 설정
	// 이미 사용중인 주소나 포트에 대해서도 바인드 허용 
	if( setsockopt( server->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse))){
		printf("	| ! Server : Failed to set the socket's option\n");
		free( server);
		return NULL;
	}

	// 소켓 bind
	if( bind( server->fd, ( struct sockaddr*)( &server->addr), sizeof( server->addr)) < 0){
		printf("	| ! Server : Failed to bind socket\n");
		if( ( close( server->fd) < 0)){
			printf("	| ! Server : close error\n");
			return UNKNOWN;
		}
		free( server);
		return NULL;
	}

	// 소켓 listen
	if( listen( server->fd, MSG_QUEUE_NUM) < 0){
		printf("	| ! Server : listen error\n");
		if( ( close( server->fd) < 0)){
			printf("	| ! Server : close error\n");
			return UNKNOWN;
		}
		free( server);
		return NULL;
	}

	// epoll_create 설정. epoll 인스턴스 생성. 
	if( ( server->epoll_handle_fd = epoll_create( BUF_MAX_LEN)) < 0){
		printf("	| ! Server : Failed to create epoll handle fd\n");
		if( ( close( server->fd) < 0)){
			printf("	| ! Server : close error\n");
			return UNKNOWN;
		}
		free(server);
		return NULL;
	}

	// epll_ctl 설정. epoll 인스턴스에 관찰 대상 등록 
	struct epoll_event server_event;
	server_event.events = EPOLLIN;
	server_event.data.fd = server->fd;
	if( ( epoll_ctl( server->epoll_handle_fd, EPOLL_CTL_ADD, server->fd, &server_event)) < 0){
		printf("	| ! Server : Failed to add epoll server event\n");
		if( ( close( server->fd) < 0)){
			printf("	| ! Server : close error\n");
			return UNKNOWN;
		}
		free( server);
		return NULL;
	}

	printf("	| @ Server : Success to create a object\n");
	printf("	| @ Server : Welcome\n\n");
	return server;
}	

/**
 * @fn void server_destroy( server_t *server)
 * @brief server 객체를 삭제하기 위한 함수
 * @return void
 * @param server 삭제하려는 server 객체
 */
void server_destroy( server_t* server){
	if( server_check_fd( server->fd) == FD_ERR){
		return SOC_ERR;
	}
	int rv = 0;
	if( ( close( server->fd) < 0)){
		printf("	| ! Server : close error\n");
		return UNKNOWN;
	}
	free( server);

	printf("	| @ Server : Success to destroy the object\n");
	printf("	| @ Server : BYE\n\n");
}

/**
 * @fn int server_conn( server_t *server)
 * @brief client와 연결되었을 때 데이터를 수신하고 데이터를 처리하기 위한 함수
 * @return client와 정상 연결 여부
 * @param server 데이터 처리를 위한 server 객체
 */
int server_conn( server_t *server){
	// 서버 file descriptor 체크 
	if( server_check_fd( server->fd) == FD_ERR){
		return SOC_ERR;
	}

	int rv = 0;
	struct sockaddr_in client_addr;
	int client_addr_len = sizeof( client_addr);
	memset( &client_addr, 0, client_addr_len);

	int client_fd = accept( server->fd, ( struct sockaddr*)( &client_addr), ( socklen_t*)( &client_addr_len));
	if( client_fd < 0){
		perror("accept");
		printf("	| ! Server : accept error\n");
		return FD_ERR;
	}

	rv = server_set_fd_nonblock( client_fd);
	if( rv < NORMAL){
		return FD_ERR;
	}

	if(( server->epoll_handle_fd = epoll_create( BUF_MAX_LEN)) < 0){
		printf("	| ! Proxy : Failed to create epoll handle fd\n");
		return OBJECT_ERR;
	}

	struct epoll_event client_event;
	client_event.events = EPOLLIN;
	client_event.data.fd = client_fd;
	if(( epoll_ctl( server->epoll_handle_fd, EPOLL_CTL_ADD, client_fd, &client_event)) < 0){
		printf("	| ! Server : Failed to add epoll client event\n");
		return OBJECT_ERR;
	}

	rv = server_process_data( server, client_fd);
	if( rv < NORMAL){
		printf("	| ! Server : process end\n");
		return OBJECT_ERR;
	}

	return NORMAL;
}


// ------------------------------------------------------------------

/**
 * @fn int main(int argc, char **argv)
 * @brief server 구동을 위한 main 함수
 * @return int 
 * @param argc 매개변수 개수
 * @param argv ip / 포트번호
 */
int main( int argc, char **argv){
	if ( argc != 3){
		printf("	| ! need param : ip port\n");
		return UNKNOWN; // 왜 unknown을 return할까?
	}

	int rv;
	server_t* server = server_init( argv); // 메인에서 받은 ip와 포트주소를 이용해 서버 구조체 초기화 
	if( server == NULL){
		printf("	| ! Serer : Failed to initialize\n");
		return UNKNOWN;
	}
	rv = server_conn( server); 
	if( rv <= FD_ERR){
		if( rv == SOC_ERR){
			printf("	| ! Server : server fd closed\n");
		}
	}
	if ( is_finish == false){
		printf("	| @ Server : Please press <ctrl+c> to exit\n");
		is_error = true;
		while( 1){
			if( is_finish == true){
				break;
			}
		}
		server_destroy( server);
	}
	else{
		while( 1){
			if( is_error == true){
				break;
			}
		}
	}

	return NORMAL;
}

