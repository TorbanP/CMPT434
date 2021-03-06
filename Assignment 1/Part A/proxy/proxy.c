/* 
 * File:   proxy.c
 * Author: TJP873
 * CMPT 434 - Assignment 1, Part A
 * Created on January 20, 2016, 6:21 PM
 * code is heavily based on http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10
#define MAXDATASIZE 222 /* max number of char we can get at once [10] + [10] + [200] + '\n' + '\0' */


void sigchld_handler(int s);
void *get_in_addr(struct sockaddr *sa);
int connect_to_server(char* argv[]);
int send_reply(int new_fd, char* reply[]);

/*
 * 
 */
int main(int argc, char* argv[]) {

    if (argc != 4) {
        fprintf(stderr, "usage: [proxy listen port] [proxy destination IP] [proxy destination port]\n");
        exit(1);
    }

    int sockfd;
    int client_fd; /* listen on sock_fd, new connection on new_fd */
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; /* connector's address information */
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; /* use my IP */

    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    /* loop through all the results and bind to the first we can */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("proxy: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("proxy: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); /* all done with this structure */

    if (p == NULL) {
        fprintf(stderr, "proxy: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; /* reap all dead processes */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("proxy: waiting for connections...\n");

    while (1) { /* main accept() loop */
        sin_size = sizeof their_addr;
        client_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), s, sizeof s);
        printf("proxy: got connection from %s\n", s);

        if (!fork()) {
            char* buf = malloc(sizeof (char) * MAXDATASIZE);
            int numbytes;
            char error_msg[] = "Proxy-Server connection fault\n";
            char* error_ptr = error_msg;

            /* connect to server, get server fd*/
            int server_fd = connect_to_server(argv);


            if (!fork()) {
                /* this fork handles server-> client communication */
                while (1) {
                    /* listen for reply from server*/
                    if ((numbytes = recv(server_fd, buf, MAXDATASIZE - 1, 0)) == -1) {
                        perror("recv");
                        exit(1);
                    }
                    /* if (!fork()) {  */
                    /* connection got closed, cleanup */
                    if (numbytes == 0) {
                        perror("closed connection");
                        close(client_fd);
                        close(server_fd);
                        free(buf);
                        exit(0);
                    }

                    /* hack to ensure a newline is at end of message */
                    char* save;
                    buf = strtok_r(buf, "\n\0", &save);
                    strcat(buf, "\n");

                    /* Send message to client */
                    if (send_reply(client_fd, &buf)) {
                        printf("proxy: sent message to client, %s \n", buf);
                        /* connection got closed, cleanup */
                    } else {
                        perror("closed connection");
                        close(client_fd);
                        close(server_fd);
                        free(buf);
                        exit(0);
                    }
                    /*}*/
                }
            }

            /* this child-parent handles client -> server communication */
            while (1) {
                /* wait for message from client */
                if ((numbytes = recv(client_fd, buf, MAXDATASIZE - 1, 0)) == -1) {
                    perror("recv");
                    free(buf);
                    exit(1);
                }

                /* connection got closed, cleanup */
                if (numbytes == 0) {
                    perror("closed connection");
                    close(client_fd);
                    close(server_fd);
                    free(buf);
                    exit(0);
                }
                buf[numbytes] = '\0';
                
                /* Send message to server */
                if (send_reply(server_fd, &buf) == 1) {
                    printf("proxy: sent message to server, %s \n", buf);
                } else {
                    exit(0);
                }
            }
        }
    }
    close(client_fd); /* parent doesn't need this */

    return (EXIT_SUCCESS);
}

/*
 * reap all dead processes
 */
void sigchld_handler(int s) {
    /* waitpid() might overwrite errno, so we save and restore it: */
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

/*
 * get sockaddr, IPv4 or IPv6:
 */
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

int connect_to_server(char* argv[]) {  
    
    int sockfd ;
    int numbytes;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(argv[2], argv[3], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    /* loop through all the results and connect to the first we can */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("proxy: socket");
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("proxy: connect");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "proxy: failed to connect\n");
        return -1;
    }
    
    freeaddrinfo(servinfo); /* all done with this structure */
   
    return sockfd;
}

/*
 * send_reply - send reply message back to connection
 */
int send_reply(int new_fd, char* reply[]) {

    int numbytes = send(new_fd, *reply, strlen(*reply) + 1, 0);
    if (numbytes == -1 || numbytes == 0) {
        perror("send");
        return -1;
    } else {
        return 1;
    }
}