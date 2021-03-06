#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/tcp.h>

#define MAX_EVENTS 64
#define BUF_SIZE 1024
#define handle_error(msg)  do { \
    perror(msg);    \
    exit(EXIT_FAILURE); \
} while (0)

struct clientinfo
{
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    int fd;
};

static int create_and_bind(const char* port){
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; //AF_INET6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int ret;
    ret = getaddrinfo(NULL, port, &hints, &result);
    if(ret != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    int sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if(-1 == ret){
        handle_error("failed create socket");
    }

    ret = bind(sfd, result->ai_addr, result->ai_addrlen);
    if(-1 == ret){
        handle_error("failed bind");
    }

    freeaddrinfo(result);           /* No longer needed */
    return sfd;
}

static int write_socket(struct clientinfo* client, char* buf, int len){
    int write_len = 0;
    for(int left = len, ret; left > 0;){
        ret = write(client->fd, buf, len);
        if(ret == -1){
            perror("write error");
            if(errno == EAGAIN){//buffer is full
                ret = 0;
            }else{
                return -1;
            }
        }
        left -= ret;
        write_len += ret;
    }
    return write_len;
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

int main(int argc, char* argv[]){
    int ret;
    int cfd;
    struct epoll_event event, events[MAX_EVENTS];
    int sfd = create_and_bind("1234");

    ret = set_socket_non_block(sfd);
    if(ret == 1){
        handle_error("cannot set socket flags");
    }

    ret = listen(sfd, SOMAXCONN);
    if(-1 == ret){
        handle_error("failed listen");
    }

    int epfd = epoll_create(1);
    if(epfd == -1){
        handle_error("failed create epoll");
    }

    event.events = EPOLLIN | EPOLLET;
    event.data.fd = sfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &event);

    while(1){
        printf("%s\n", "listing......");
        int ready = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if(ready == -1){
            handle_error("failed epoll wait");
        }
        for(int i=0; i<ready; i++){
            //EPOLLERR || EPOLLHUP || !EPOLLIN
            if((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || !(events[i].events & EPOLLIN)){
                perror("error socket:");
                close(events[i].data.fd);
                continue;
            }else if(events[i].data.fd == sfd){//new
                struct sockaddr_storage client_addr;
                int cl_addrlen = sizeof(client_addr);
                cfd = accept(sfd, &client_addr, &cl_addrlen);
                if(cfd == -1){
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    } else{
                        perror("error : cannot accept a new socket!\n");
                        continue;
                    }
                }

                ret = set_socket_non_block(cfd);
                if(ret == -1){
                    handle_error("failed set socket flag");
                }

                struct clientinfo* clientinfo = malloc(sizeof(struct clientinfo));
                memset(clientinfo, 0, sizeof(clientinfo));
                clientinfo->fd = cfd;
                ret = getnameinfo((struct sockaddr *) &client_addr, cl_addrlen,
                                              clientinfo->host, NI_MAXHOST,
                                              clientinfo->service, NI_MAXSERV, NI_NUMERICSERV);
                if(0 != ret){
                    fprintf(stderr, "getnameinfo: %s\n", gai_strerror(ret));
                    close(clientinfo->fd);
                    free(clientinfo);
                }else{
                    printf("join in %s:%s\n", clientinfo->host, clientinfo->service);
//                    pthread_t tid;
//                    pthread_create(&tid, NULL, handle_connect, clientinfo);
//                    pthread_detach(tid);

                    event.events = EPOLLIN | EPOLLET;
                    event.data.ptr = clientinfo;
                    ret = epoll_ctl(epfd, EPOLL_CTL_ADD,  cfd, &event);
                    if(ret == -1){
                        handle_error("failed add socket to epoll");
                    }
                }
            }else {//not sfd
               char buf[BUF_SIZE] ={0};
               int rbt;
               struct clientinfo* client = (struct clientinfo*)events[i].data.ptr;//union
               rbt = read(client->fd, buf, BUF_SIZE);
               if(rbt == -1){
                   if(errno == EAGAIN || errno == EWOULDBLOCK){
                       break;
                   }
                   perror("read error:");
               }else{
                   if(rbt == 0){  /*end client close*/
                       close(client->fd);
                       printf("leave %s:%s\n", client->host, client->service);
                       free(client);
                   }else{
                       printf("from %s:%s %s %d\n", client->host, client->service, buf, rbt);
                       int len = write_socket(client, buf, rbt);
                       printf("to %s:%s:%s %d\n", client->host, client->service, buf, len);
                   }
               }
            }
        }
    }
    close(sfd);
    return 0;
}

