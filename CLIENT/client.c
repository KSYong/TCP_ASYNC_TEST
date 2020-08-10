#include "client.h"
#include "kmp.h"

static int is_finish = false;
static int is_error = false;

// -------------------------------------------------------------------------

/**
 * @fn static int client_set_fd_nonblock( int fd)
 * @brief file descriptor를 block되지 않게(비동기로) 설정하는 함수
 * @return 정상적으로 설정되면 NORMAL, 비정상적이면 FD_ERR 반환
 * @param fd 확인할 client file descriptor
 */
static int client_set_fd_nonblock( int fd){
    int rv = 0;

    if( ( rv = fcntl( fd, F_GETFL, 0)) < 0){
        printf("	| ! Server : fcntl error (F_GETFL)\n");
        return FD_ERR;
    }

    if( ( rv = fcntl( fd, F_SETFL, rv | O_NONBLOCK)) < 0){
        printf("	| ! Server : fcntl error (F_SETFL & O_NONBLOCK)\n");
        return FD_ERR;
    }

    return NORMAL;
}

/**
 * @fn static int client_check_fd( int fd)
 * @brief src file descriptor가 연결되어 있는지 확인
 * @return 정상 연결되어 있으면 NORMAL, 비정상 연결이면 FD_ERR 반환
 * @param fd 연결을 확인할 file descriptor
 */
static int client_check_fd( int fd){
    int error = 0;
    socklen_t err_len = sizeof( error);
    if( getsockopt( fd, SOL_SOCKET, SO_ERROR, &error, &err_len) < 0){
        return FD_ERR;
    }
    return NORMAL;
}

// ------------------------------------------------------------------------

/**
 * @fn client_t* client_init( char **argv)
 * @brief client 객체를 생성하고 초기화하는 함수
 * @return 생성된 client 객체
 * @param argv 서버의 ip / port 정보
 */
client_t* client_init( char **argv){
    int rv;
    client_t *client = ( client_t*)( malloc( sizeof( client_t)));

    if( client == NULL){
        printf("	| ! Client : Failed to allocate memory\n");
        return NULL;
    }

    // 소켓 생성 
    if( ( client->fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1){
        printf("	| ! Client : Failed to open socket\n");
        free( client);
        return NULL;
    }


    int addr_len;
    int server_fd;
    struct hostent *server_host = gethostbyname( argv[1]);

    if( server_host == NULL){
        printf("	| ! Client : gethostbyname error ( struct hostent)\n");
        close( client->fd);
        free( client);
        return NULL;
    }

    addr_len = sizeof( client->server_addr);
    memset( &client->server_addr, 0, addr_len);
    client->server_addr.sin_family = AF_INET;
    memcpy( &client->server_addr.sin_addr, server_host->h_addr, server_host->h_length);
    client->server_addr.sin_port = htons( atoi( argv[2]));

    // 소켓 옵션 설정 
    int reuse = 1;
    if( setsockopt( client->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse))){
        printf("	| ! Client : Failed to set the socket's option\n");
        close( client->fd);
        free( client);
        return NULL;
    }

    // 소켓 넌블락 설정 
    rv = client_set_fd_nonblock( client->fd);
    if( rv < 0){
        close( client->fd);
        free( client);
        return NULL;
    }

    /*
       if( connect( client->fd, ( struct sockaddr*)( &server_addr), sizeof( struct sockaddr))){
       printf("	| ! Client : Failed to connect with Server\n");
       close( client->fd);
       free( client);
       return NULL;
       }
       */

    // epoll 생성 
    if( ( client->epoll_handle_fd = epoll_create( BUF_MAX_LEN)) < 0){
        printf("	| ! Client : Failed to create epoll handle fd\n");
        close( client->fd);
        free( client);
        return;
    }

    // epoll에 client->fd를 관심있는 파일 디스크립터로 지정하기 
    struct epoll_event client_event;
    client_event.events = EPOLLOUT | EPOLLIN;
    client_event.data.fd = client->fd;
    if( ( epoll_ctl( client->epoll_handle_fd, EPOLL_CTL_ADD, client->fd, &client_event)) < 0){
        printf("	| ! Client : Failed to add epoll client event\n");
        close( client->fd);
        free( client);
        return NULL;
    }

    printf("	| @ Client : Success to create an object\n");
    printf("	| @ Client : Welcome\n\n");
    return client;
}

/**
 * @fn void client_destroy( client_t *client)
 * @brief client 객체를 삭제하는 함수 
 * @param client 삭제할 client 객체
 */
void client_destroy( client_t *client){
    close( client->fd);
    free( client);

    printf("	| @ Client : Success to destroy the object\n");
    printf("	| @ Client : BYE\n\n");
}

/**
 * @fn int client_process_data( client_t *client)
 * @brief server로 요청을 보내서 응답을 받는 함수
 * @return int
 * @param client 요청을 하기 위한 client 객체 
 */
int client_process_data( client_t *client){
    // 나중에 int 형 return들로 에러 처리 할것임 그래서 int형 반환 함수,, 
    int i, event_count = 0;
    char read_buf[ BUF_MAX_LEN];
    char send_buf[ BUF_MAX_LEN];
    ssize_t recv_bytes, send_bytes;
    char msg[ BUF_MAX_LEN];

    memset(read_buf, 0, BUF_MAX_LEN);
    memset(send_buf, 0, BUF_MAX_LEN);

    printf("	| @ Client : connecting...\n");
    while( 1){
        memset( msg, '\0', BUF_MAX_LEN);
        // epoll_wait 시작 
        event_count = epoll_wait( client->epoll_handle_fd, client->events, BUF_MAX_LEN, TIMEOUT);
        if( event_count < 0){
            printf("	| ! Client : epoll_wait error\n");
            break;
        }
        else if( event_count == 0){
            printf("	| ! Client : epoll_wait timeout\n");
            continue;
        }

        for( i = 0; i < event_count; i++){
            if( client->events[ i].events & EPOLLOUT){ // socket is ready for writing
                if( client->events[ i].data.fd == client->fd){
                    // 클라이언트 file descriptor에서 변화가 감지되었을 때
                    printf("\n	| @ Client : >");
                    memset( msg, '\0', BUF_MAX_LEN);
                    if( fgets( msg, BUF_MAX_LEN, stdin) == NULL){
                        // EINTR : interrupted system call
                        if( errno == EINTR){
                            is_finish = true;
                            break;
                        }
                    }

                    msg[ strlen(msg) -1] = '\0';
                    snprintf( send_buf, BUF_MAX_LEN, "%s", msg);

                    kmp_t send_msg[ 1];
                    kmp_set_msg( send_msg, 1, send_buf, 1);
                    kmp_print_msg( send_msg);
                    if( ( send_bytes = write( client->fd, send_msg, sizeof(send_msg))) <= 0){
                        printf("	| ! Client : Failed to send msg (bytes:%d) (errno:%d)\n", send_bytes, errno);
                       break;
                    }
                    else{
                        printf("	| @ Client : Success to send msg to Server (bytes:%d)\n", send_bytes);

                        if( !memcmp( send_buf, "q", 1)){
                            printf("    | @ Client : Finish\n");
                            break;
                        }

                        printf("	| @ Client : Wait to recv msg...\n");
                    }
                }
            }

            if ( client->events[ i].events & EPOLLIN){ // socket is ready for reading
                kmp_t recv_msg[ 1];
                if( ( recv_bytes = read(client->fd, recv_msg, BUF_MAX_LEN)) <= 0){
                    printf("	| ! Client : Failed to recv msg (bytes:%d) (errno:%d)\n", recv_bytes, errno);
                    break;
                }
                else{
                    kmp_print_msg( recv_msg);
                    snprintf( read_buf, BUF_MAX_LEN, "%s", kmp_get_data( recv_msg));
                    read_buf[ recv_bytes] = '\0';
                    printf("	| @ Client : < %s ( %lu bytes)\n", read_buf, recv_bytes);
                }
            }
            printf("\n");
        }
    }
}


int client_conn( client_t *client){
    if( client_check_fd( client->fd) == FD_ERR){
        return SOC_ERR;
    }

    int i, rv, event_count = 0;
    while( 1){
        event_count = epoll_wait( client->epoll_handle_fd, client->events, BUF_MAX_LEN, TIMEOUT);
        if( event_count < 0){
            printf("    | ! Client : epoll_wait error\n");
            return -1;
        }
        else if( event_count == 0){
            printf("    | ! Client : epoll_wait timeout\n");
        }

        for( i = 0; i < event_count; i++){
            if( client->events[ i].data.fd == client->fd){
                rv = connect( client->fd, ( struct sockaddr*)( &client->server_addr), sizeof( struct sockaddr));
                if( rv < 0 ){
                    if( ( errno == EAGAIN) || ( errno == EWOULDBLOCK) || ( errno == EINPROGRESS)){
                        printf("    | @ Client : EAGAIN\n");
                        continue;
                    }
                    else if( errno == EINTR){
                        printf("    | ! Client : Interrupted in connect (fd:%d)\n", client->fd);
                        break;
                    }
                    else{
                        perror("error");
                        printf("	| ! Client : Failed to connect with Server\n");
                        close( client->fd);
                        free( client);
                        return NULL;
                    }
                }
                else if( rv == 0){
                    printf("    | @ Client : Connection to Server success!\n");
                }

                rv = client_process_data( client);
                if( rv < NORMAL){
                    printf("    | Client : process end\n");
                    return OBJECT_ERR;
                }
            }
        }
        return NORMAL;
    }
}

// ---------------------------------------------------------------================

/**
 * @fn int main( int argc, char **argv)
 * @brief client 구동을 위한 main 함수
 * @return int 
 * @param argc 매개변수 개수
 * @param argv 서버의 ip와 포트 정보
 */
int main( int argc, char **argv){
    if( argc != 3){
        printf("	| ! need param : server_ip server_port\n");
        return -1;
    }

    int rv;
    client_t* client = client_init( argv);
    if( client == NULL){
        printf("	| ! Client : Failed to initialize\n");
        return -1;
    }

    while( 1){
        rv = client_conn( client);
        if( rv <= FD_ERR){
            if( rv == SOC_ERR){
                printf("    | ! Client : client fd closed\n");
                break;
            }
            continue;
        }
    }

    if( is_finish == false){
        printf("	| @ Client : Please press <ctrl + c> to exit\n");
        is_error = true;
        while( 1){
            if( is_finish == true){
                break;
            }
        }
        client_destroy( client);
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

