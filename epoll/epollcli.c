#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>


#define BUF_SIZE 1024
#define handle_error(msg)  do { \
    perror(msg);    \
    exit(EXIT_FAILURE); \
} while (0)


static int create_and_connect(char* host, char* port){
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; //AF_INET6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int ret, sfd = -1;
    ret = getaddrinfo(host, port, &hints, &result);
    if(ret != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if(sfd == -1){
        handle_error("failed create socke");
    }

    ret = connect(sfd, result->ai_addr, result->ai_addrlen);
    if(ret == -1){
        handle_error("failed connect");
    }
    freeaddrinfo(result);
    return sfd;
}

static int set_socket_non_block(int fd){
    int flags, res;
    flags = fcntl(fd, F_GETFL);
    if (flags == -1){
        handle_error("cannot get socket flags");
    }
    flags |= O_NONBLOCK;
    res = fcntl(fd, F_SETFL, flags);
    if (res == -1){
        handle_error("cannot set socket flags");
    }
    return res;
}

static int write_socket(int fd, char* buf, int len){
    int write_len = 0;
    for(int left = len, ret; left > 0;){
        ret = write(fd, buf, len);
        if(ret == -1){
           handle_error("write error");
        }
        left -= ret;
        write_len += ret;
    }
    return write_len;
}

static int read_socket(int fd, char* buf, int* len){
    int ret;
    ret = read(fd, buf, len);
    if(ret == -1){
       handle_error("read error");
    }
    len = &ret;
    return ret;
}

void* handle_input(void* fd){
    char buf[1024];
    while(1){
        scanf("%s", buf);
        if(write_socket((int)fd, buf, strlen(buf)) == -1){
            handle_error("write error");
        }
    }
    return (void*)NULL;
}

void* handle_connect(void* sfd){
    int fd = (int)sfd;
    int epfd = epoll_create(1);
    if(epfd == -1){
        handle_error("failed create epoll");
    }
    int ret;
    char buf[BUF_SIZE] = {0};
    struct epoll_event event, events[64];
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = fd;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    if(ret == -1){
        handle_error("epoll wait error");
    }
    while(1){
        printf("%s\n","waiting...");
        ret = epoll_wait(epfd, events, 64, -1);
        if(ret == -1){
            handle_error("epoll wait error");
        }
        for(int i=0; i< ret; i++){
           if((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)){
                perror("socket error");
                close(events[i].data.fd);
                close(epfd);
                return -1;
           }else if(events[i].data.fd == fd && events[i].events & EPOLLIN){
                ret = read_socket(events[i].data.fd, buf, BUF_SIZE);
                if(ret == 0){
                    close(events[i].data.fd);
                    close(epfd);
                    return -1;
                }
                printf("from server:%s %d\n", buf, ret);
                continue;
           }
        }
    }
    close(fd);
    close(epfd);
    return (void*)NULL;
}

int main(int argc, char* argv[])
{

    if(argc != 3){
	printf("%s %s\n","ip", "port");
	return 0;
    }	

    int sfd = create_and_connect(argv[1], argv[2]);
    if(sfd == -1){
        handle_error("error");
    }
    pthread_t input;
    pthread_create(&input, NULL, handle_input, sfd);


    pthread_t con;
    pthread_create(&con, NULL, handle_connect, sfd);


    void* ret;

    pthread_join(input, &ret);
    pthread_join(con, &ret);


    return 0;
}
