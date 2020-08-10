#include "server.h"

// ----------------------------------------------------------

static int is_finish = false;
static int is_error = false;

// -----------------------------------------------------------

/**
 * @fn static void server_transc_clear( transc_t *transc)
 * @brief transc 구조체 객체를 초기화하는 함수
 * @return void
 * @param transc 초기화하기 위한 transc_t 구조체 변수
 */
static void server_transc_clear( transc_t *transc){
    transc->is_recv_header = 0;
    transc->is_recv_body = 0;
    transc->is_send_header = 0;
    transc->is_send_body = 0;
    transc->length = 0;
    transc->recv_bytes = 0;
    transc->send_bytes = 0;
    memset( transc->read_hdr_buf, '\0', MSG_HEADER_LEN);
    memset( transc->read_body_buf, '\0', BUF_MAX_LEN);
    memset( transc->write_hdr_buf, '\0', MSG_HEADER_LEN);
    memset( transc->write_body_buf, '\0', BUF_MAX_LEN);
    transc->data = NULL;
}

/**
 * @fn static uint32_t server_transc_get_msg_length( transc_t *transc)
 * @brief 전달받은 메시지를 일부만 decode해서 메시지의 총 길이(Header + Body)를 구하는 함수
 * @return 메시지 길이
 * @param transc 메시지의 길이를 구하기 위한 transc_t 구조체 변수
 */
static uint32_t server_transc_get_msg_length( transc_t *transc){
    // if header is NULL
    if( ( strlen( transc->read_hdr_buf) == 0)){
        return -1;
    }

    // 1 byte = 8 bits
    // len size = 3 bytes
    // char형의 uint8_t 포인터로의 형변환
    // uint8_t -> 8bit 크기의 int형 자료형. char형과 크기가 같다. 
    //
    uint8_t *data = ( uint8_t*)( transc->read_hdr_buf);

    // Big endian
    //    uint32_t msg_len_b = ( ( ( int)( data[ 1])) << 16) + ( ( ( int)( data[ 2])) << 8) + data[ 3];

    // Little endian
    uint32_t msg_len_l = ( ( ( int)( data[ 3])) << 16) + ( ( ( int)( data[ 2])) << 8) + data[ 1]; 

    //    printf("msg_len_b : %d\n", msg_len_b);
    printf("msg_len_l : %d\n", msg_len_l);

    int i;
    for( i = 0; i < 20; i++){
        printf("%d = %d\n", i, ( uint8_t)data[i]);
    }

    return msg_len_l;
}

/**
 * @fn static int server_recv_data( transc_t *transc, int fd)
 * @brief client 가 server로 데이터를 보낼 때, server에서 user copy로 수신하기 위한 함수
 * @return 열거형 참고
 * @param transc user copy 처리를 위한 transc_t 객체
 * @param fd 연결된 client file descriptor
 */
static int server_recv_data( transc_t *transc, int fd){
    if( fd < 0){
        printf("    | ! Server : Failed to get client_fd in server_recv_data (fd:%d)\n", fd);
        return FD_ERR;
    }
    char temp_read_hdr_buf[ MSG_HEADER_LEN];
    char temp_read_body_buf[ BUF_MAX_LEN];
    int recv_bytes = 0;
    int body_len = 0;
    int body_index = 0;

    // 메시지 받을 때 인덱스 관리해서 받아야 한다.
    // 처음에 헤더 길이만큼 메시지 20바이트 먼저 받고, 메시지 길이 얻어내서 바디 길이(msglen-20)만큼 더 받는다.
    // 1. read로 헤더 크기만큼 먼저 수신하기 
    // 헤더와 바디 모두 수신받지 않았을 때 read()를 진행한다. 
    if( ( transc->is_recv_header == 0) && ( transc->is_recv_body == 0)){ // recv header
        memset( transc->read_hdr_buf, '\0', MSG_HEADER_LEN);
        recv_bytes = read( fd, temp_read_hdr_buf, MSG_HEADER_LEN - ( transc->recv_bytes));
        // 에러 처리 
        if( recv_bytes < 0){
            if( ( errno == EAGAIN) || ( errno == EWOULDBLOCK)){
                printf("    | @ Server : EAGAIN\n");
                return ERRNO_EAGAIN;
            }
            else if( errno == EINTR){
                printf("    | ! Server : Interrupted (in recv msg header) (fd:%d)\n", fd);
                return INTERRUPT;
            }
            else{
                printf("    | ! Server : read error (errno:%d) (in recv msg header) (fd:%d)\n", errno, fd);
                return NEGATIVE_BYTE;
            }
        }
        // 파일 없음 
        else if( recv_bytes == 0){
            perror(" read error ");
            printf("    | ! Server : read 0 byte (in recv msg header) (fd:%d)\n", fd);
            return ZERO_BYTE;
        }
        // read 성공 시 trnasc->read_hdr_buf에 헤더 저장 
        else{
            printf(" recv_bytes : %d", recv_bytes);
            memcpy( &transc->read_hdr_buf[ transc->recv_bytes], temp_read_hdr_buf, recv_bytes);
            transc->recv_bytes += recv_bytes;

            if( transc->recv_bytes == MSG_HEADER_LEN){
                // 서버가 헤더를 모두 수신하면, 헤더를 해독해서 메시지 길이를 구한다
                transc->length = server_transc_get_msg_length( transc);
                body_len = transc->length - MSG_HEADER_LEN;
                printf("    | @ Server : msg body len : %d\n", body_len);
                if( body_len <= 0){
                    printf("    | ! Server : msg body length is 0 (in recv msg header) (fd:%d)\n", fd);
                    return BUF_ERR;
                }
            }
            else{
                printf("    | ! Server : recv overflow error (in recv msg header) (fd:%d)\n", fd);
                return BUF_ERR;
            }
        }
    }
    else if( ( transc->is_recv_header == 0) && ( transc->is_recv_body == 1)){
        printf("    | ! Server : recv unknown error, not recv hdr but recv body? (in recv msg header) (fd:%d)\n", fd);
        return UNKNOWN;
    }

    // 2. Recv body with read() function
    if( ( transc->is_recv_header == 1) && ( transc->is_recv_body == 0)){ // If success to recv header, then recv body
        // The transc->recv_bytes is MSG_HEADER_LEN (20)
        // 즉 메시지 바디 길이만큼 read()한다. 
        recv_bytes = read( fd, temp_read_body_buf, transc->length - ( transc->recv_bytes));
        if( recv_bytes < 0){
            if( ( errno == EAGAIN) || ( errno == EWOULDBLOCK)){
                printf("    | @ Server : EAGAIN\n");
                return (ERRNO_EAGAIN);
            }
            else if( errno == EINTR){
                printf("    | ! Server : Interrupted (in read msg body) (fd:%d)\n", fd);
                return INTERRUPT;
            }
            else{
                printf("    | ! Server : read error(errno:%d) (in read msg body) (fd:%d)\n", errno, fd);
                return NEGATIVE_BYTE;
            }
        }
        else if( recv_bytes == 0){
            printf("    | ! Server : read 0 byte (in read msg body) (fd:%d)\n", fd);
            return ZERO_BYTE;
        }
        else{
            body_index = transc->recv_bytes - MSG_HEADER_LEN;
            memcpy( &transc->read_body_buf[ body_index], temp_read_body_buf, recv_bytes);
            transc->recv_bytes += recv_bytes;

            if( transc->recv_bytes == transc->length){
                transc->is_recv_body = 1;
            }
        }
    }


    if( ( ( transc->is_recv_header == 1) && ( transc->is_recv_body == 1))){
        printf("    | @ Server : Recv the msg (bytes : %d) (fd : %d)\n", transc->recv_bytes, fd);
    }

    return NORMAL;
}

/**
 * @fn static int server_send_data( transc_t *transc, int fd)
 * @brief Server가 Client로 데이터를 보낼 때, Server 에서 zero copy로 송신하기 위한 함수
 * @return 열거형 참고
 * @param transc user copy 처리를 위한 transc_t 객체
 * @param fd 연결된 client file descriptor
 */
static int server_send_data( transc_t *transc, int fd){
    if( fd < 0){
        printf("    | ! Server : Failed to get client_fd in server_send_data (fd:%d)\n", fd);
        return FD_ERR;
    }

    int write_bytes = 0;
    int body_len = 0;
    int body_index = 0;

    // 1. Send header with write() function
    // 보낸 헤더가 없을 시 받은 헤더 그대로 보낸다. 
    if( ( transc->is_send_header == 0) && ( transc->is_send_body == 0)){
        if( transc->send_bytes < MSG_HEADER_LEN){
            memset( transc->write_hdr_buf, '\0', MSG_HEADER_LEN);

            if( transc->send_bytes == 0){
                memcpy( transc->write_hdr_buf, transc->read_hdr_buf, MSG_HEADER_LEN);
            }
            else if( transc->send_bytes > 0){
                memcpy( &transc->write_hdr_buf[ transc->send_bytes], &transc->read_hdr_buf[ transc->send_bytes], MSG_HEADER_LEN - transc->send_bytes);
            }
            else{
                printf("    | ! Server : unknown error, transc->send_bytes is negative in server_send_data (fd:%d)\n", fd);
                return UNKNOWN;
            }
        }
        else{
            printf("    | ! Server : transc->send_bytes error, send_bytes is over than MSG_HEADER_LEN but header not sended in server_send_data (fd:%d)\n", fd);
            return UNKNOWN;
        }

        if( ( write_bytes = write( fd, transc->write_hdr_buf, MSG_HEADER_LEN - transc->send_bytes)) <= 0){
            printf("    | ! Server : Failed to write msg (fd:%d)\n", fd);
            if( errno == EAGAIN || errno == EWOULDBLOCK){
                return ERRNO_EAGAIN;
            }
            return NEGATIVE_BYTE;
        }

        transc->send_bytes += write_bytes;
        if( transc->send_bytes == MSG_HEADER_LEN){
            transc->is_send_header = 1;
            transc->recv_bytes -= write_bytes;
        }
    }
    else if( ( transc->is_send_header == 0) && ( transc->is_send_body == 1)){
        printf("    | ! Server : send unknown error, header not sended but body is? (in recv msg header) (fd:%d)\n", fd);
        return UNKNOWN;
    }

    // 2. Send body with write() function
    if( ( transc->is_send_header == 1) && ( transc->is_send_body == 0)){
        body_len = transc->length - MSG_HEADER_LEN;
        // 메시지 검사
        if( transc->send_bytes < transc->length){
            memset( transc->write_body_buf, '\0', body_len);

            if( transc->send_bytes == MSG_HEADER_LEN){
                memcpy( transc->write_body_buf, transc->read_body_buf, body_len);
            }
            else if( transc->send_bytes > MSG_HEADER_LEN){
                body_index = transc->send_bytes - MSG_HEADER_LEN;
                memcpy( &transc->write_body_buf[ body_index], &transc->read_body_buf[ body_index], body_len - transc->send_bytes);
            }
            else{
                printf("    | ! Server : unknown error, transc->send_bytes is negative in server_send_data (fd:%d)\n", fd);
                return UNKNOWN;
            }
        }
        else{
            printf("    | ! Server : transc->send_bytes error, send_bytes is over MSG_HEADER_LEN but header not sent in server_send_data (fd:%d)\n", fd);
            return UNKNOWN;
        }

        if( ( write_bytes = write( fd, transc->write_body_buf, transc->recv_bytes)) <= 0){
            if( errno == EAGAIN || errno == EWOULDBLOCK){
                return ERRNO_EAGAIN;
            }

            printf("    | ! Server : Failed to write msg\n");
            return NEGATIVE_BYTE;
        }
        else{
            transc->recv_bytes -= write_bytes;
            transc->send_bytes += write_bytes;

            if( transc->recv_bytes == 0){
                transc->is_send_body = 1;
            }
        }

        if( ( transc->is_send_header == 1) && ( transc->is_send_body == 1)){
            printf("    | @ Server : Send the msg (bytes : %d) (fd : %d)\n", transc->send_bytes, fd);
        }
    }

    return NORMAL;
}

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
    int read_rv = 0;
    int send_rv = 0;
    transc_t transc[ 1];

    printf("    | @ Server : waiting...\n");
    while( 1){
        if( ( server_check_fd( fd) == FD_ERR)){
            printf("    | @ Server : connection is ended (client:%d <-> server)\n", fd);
            break;
        }

        if( ( transc->is_send_header == 1) && ( transc->is_send_body == 1)){
            server_transc_clear( transc);
        }

        event_count = epoll_wait( server->epoll_handle_fd, server->events, BUF_MAX_LEN, TIMEOUT);
        if( event_count < 0){
            printf("    | ! Server : epoll_wait error\n");
            break;
        }
        else if( event_count == 0){
            printf("    | ! Server : epoll_wait timeout\n");
            continue;
        }

        for( i = 0; i < event_count; i++){
            if( server->events[ i].data.fd == server->fd){
            }
            else if ( server->events[ i].data.fd == fd){
                if( transc->is_recv_header == 0 && transc->is_recv_body == 0){
                    read_rv = server_recv_data( transc, fd);
                    if( read_rv <= ZERO_BYTE){
                        printf("    | ! Server : disconnected\n");
                        printf("    | ! Server : socket closed\n");
                        close( fd);
                    }
                }

                if( transc->is_recv_header = 1 && transc->is_recv_body == 0){
                    read_rv = server_recv_data( transc, fd);
                    if( read_rv <= ZERO_BYTE){
                        printf("    | ! Server : disconnected\n");
                        printf("    | ! Server : socket closed\n");
                        close( fd);
                    }
                }

                if( ( transc->is_recv_header == 1) && ( transc->is_recv_body == 1)){
                    if( ( send_rv = server_send_data( transc, fd)) <= ZERO_BYTE){
                        printf("    | ! Server : Failed to send msg (fd:%d)\n", fd);
                    }
                }

                if( ( transc->is_send_header == 1) && (transc->is_send_body == 0)){
                    if( ( send_rv = server_send_data( transc, fd)) <= ZERO_BYTE){
                        printf("   | ! Server : Failed to send msg (fd:%d)\n", fd);
                    }
                }
            }
        }
    }

    return NORMAL;
}

/**
 * @fn static void* server_detect_finish( void *data)
 * @brief Server에서 사용하는 메모리를 해제하기 위한 함수
 * @return None
 * @param data Thread 매개변수, 현재 사용되는(할당된) Server 객체
 */
static void* server_detect_finish( void *data){
    server_t *server = ( server_t*)( data);

    while( 1){
        if( is_error == true){
            break;
        }

        if( is_finish == true){
            server_destroy( server);
            is_error = true;
            break;
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
    int rv;
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

    rv = server_set_fd_nonblock( server->fd);
    if( rv < 0){
        printf("    | ! Server : set nonblock error! (fd :%d)\n", server->fd);
        close( server->fd);
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

    int i, rv, event_count = 0;
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof( client_addr);
    memset( &client_addr, 0, client_addr_len);

    while( 1){
        event_count = epoll_wait( server->epoll_handle_fd, server->events, BUF_MAX_LEN, TIMEOUT);
        if( event_count < 0){
            printf("    | ! Server : epoll_wait error in server_conn (fd:%d)\n", server->fd);
            break;
        }
        else if ( event_count == 0){
            printf("    ! @ Server : epoll_wait timeout in server_conn (fd:%d)\n", server->fd);
            continue;
        }

        for( i = 0; i < event_count; i++){
            if( server->events[ i].data.fd == server->fd){
                int client_fd = accept( server->fd, ( struct sockaddr*)( &client_addr), ( socklen_t*)( &client_addr_len));
                if( client_fd < 0){
                    printf("	| @ Server : accept error!\n");
                    return FD_ERR;
                }
                
                printf("    | @ Server : accept success!\n");

                rv = server_set_fd_nonblock( client_fd);
                if( rv < NORMAL){
                    return FD_ERR;
                }

                printf("    | @ Serer : set client nonblock success!\n");

                struct epoll_event client_event;
                client_event.events = EPOLLIN | EPOLLOUT;
                client_event.data.fd = client_fd;
                if( ( epoll_ctl( server->epoll_handle_fd, EPOLL_CTL_ADD, client_fd, &client_event)) < 0){
                    printf("	| ! Server : Failed to add epoll client event\n");
                    return OBJECT_ERR;
                }

                rv = server_process_data( server, client_fd);
                if( rv < NORMAL){
                    printf("	| ! Server : process end\n");
                    return OBJECT_ERR;
                }
                else{
                    continue;
                }
            }
        }
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
    server_t* server = server_init( argv); // 메인에서 받은 ip와 포트주소를 이용해 서버 구조체 초기 
    if( server == NULL){
        printf("	| ! Serer : Failed to initialize\n");
        return UNKNOWN;
    }

    while(1){
        rv = server_conn( server); 
        if( rv <= FD_ERR){
            if( rv == SOC_ERR){
                printf("	| ! Server : server fd closed\n");
            }
            continue;
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

