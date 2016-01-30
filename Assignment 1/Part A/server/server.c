/* 
 * File:   server.c
 * Author: TJP873
 * CMPT 434 - Assignment 1, Part A
 * Created on January 20, 2016, 6:22 PM
 * Some code based on http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
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
#define MAXDATASIZE 221 /* max number of char we can get at once [10] + [10] + [200] + '\0' */
#define COMMANDSIZE 10
#define KEYSIZE 10
#define VALUESIZE 200

typedef struct data_container data_container;

struct data_container {
    char* key; 
    char* value;
    data_container* next;
};

void sigchld_handler(int s);
void *get_in_addr(struct sockaddr *sa);
int start_tcp(char * argv[], struct sockaddr_storage *their_addr);
int parse_message(char* buf[], char* reply[]);

data_container GL_head;

/*
 * 
 */
int main(int argc, char** argv) {

    GL_head.key = NULL;
    GL_head.value = NULL;
    GL_head.next = NULL;
    
    if (argc != 2) {
        fprintf(stderr, "usage: [server listen port]\n");
        exit(1);
    }

    int sockfd;
    int new_fd; /* listen on sock_fd, new connection on new_fd */
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
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); /* all done with this structure */

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
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

    printf("server: waiting for connections...\n");

    while (1) { /* main accept() loop */
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) { /* this is the child process */

            char* buf = malloc(sizeof (char) * MAXDATASIZE);
            char* reply = malloc(sizeof (char) * MAXDATASIZE);
            int numbytes;
            
            while (1) {
                if ((numbytes = recv(new_fd, buf, MAXDATASIZE, 0)) == -1) {
                    perror("recv");
                    free(buf);
                    free(reply);
                    close(new_fd);
                    exit(1);
                }
                if (numbytes == 0) {
                    free(buf);
                    free(reply);
                    close(new_fd);
                    exit(0);
                } else {
                    buf[numbytes - 1] = '\0';
                    char* reply = malloc(sizeof (char) * MAXDATASIZE);
                    if (parse_message(&buf, &reply) == -1) {
                        perror("parse message");
                    }

                    if (send(new_fd, reply, MAXDATASIZE, 0) == -1)
                        perror("send");
                }
            }
        }
        close(new_fd); /* parent doesn't need this */
    }

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

int parse_message(char* buf[], char* reply[]) {
    printf("server: got message = %s \n", *buf);

    char errormsg[] = "Bad Query or oversized\n";
    char* command;
    char* key;
    char* value;
    char* save;




    command = strtok_r(*buf, " ", &save);
    key = strtok_r(NULL, " ", &save);
    value = strtok_r(NULL, "\0", &save);

    if (command == NULL || key == NULL || value == NULL) {
        strcpy(*reply, errormsg);
        printf("sending error\n");
        return 0;
    }


    printf("%s length %lu\n", command, strlen(command));
    printf("%s length %lu\n", key, strlen(key));
    printf("%s length %lu\n", value, strlen(value));



    if ((strlen(command) > COMMANDSIZE) || (strlen(key) > KEYSIZE) || (strlen(value) > VALUESIZE)) {
        strcpy(*reply, errormsg);
        printf("sending error\n");
        return 0;
    }


    return 0;
}

int add_function(){
    return 0;
}

int getvalue_function(){
    return 0;
}

int getall_function(){
    return 0;
}

int remove_function(){
    return 0;
}




