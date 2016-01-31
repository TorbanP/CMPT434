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
#include <semaphore.h>
#include <pthread.h>

#define BACKLOG 10
#define MAXDATASIZE 221 /* max number of char we can get at once [10] + [10] + [200] + '\0' */
#define COMMANDSIZE 10
#define KEYSIZE 10
#define VALUESIZE 200 
#define THREADLIMIT 100

typedef struct data_container data_container;

enum operations {
    add_op,
    getvalue_op,
    getall_op,
    remove_op,
    undefined_op,
};

struct data_container {
    int active;
    char* key;
    char* value;
    data_container* next;
};

struct parsed_msg {
    int fd;
    char* key;
    char* value;
};

/* METHODS*/
void sigchld_handler(int s);
void *get_in_addr(struct sockaddr *sa);
int start_tcp(char * argv[], struct sockaddr_storage *their_addr);
enum operations parse_message(char* buf[], char* reply[], struct parsed_msg* current_msg);
void send_reply(int new_fd, char* reply[]);
void add_function(struct parsed_msg* current_msg);
void getvalue_function(struct parsed_msg* current_msg);
void getall_function(struct parsed_msg* current_msg);
void remove_function(struct parsed_msg* current_msg);
void *worker_thread(void *arg1);

/* GLOBAL */
sem_t GL_head_mutex;
data_container GL_head;
int debug = 0;

/*
 * 
 */
int main(int argc, char** argv) {

    if (sem_init(&GL_head_mutex, 0, 1) == -1){
        perror("sem init failed");
    }
    GL_head.active = 0;
    GL_head.key = NULL;
    GL_head.value = NULL;
    GL_head.next = NULL;

    if (argc != 2) {
        fprintf(stderr, "usage: [server listen port]\n");
        exit(1);
    }

    int sockfd;

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
    
    pthread_t threads[THREADLIMIT];
    int thread_fd[THREADLIMIT];
    int thread_counter = 0;
    
    
    while (1) { /* main accept() loop */
        sin_size = sizeof their_addr;
        thread_fd[thread_counter] = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (thread_fd[thread_counter] == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        /* program will exit after threadlimit, could implement code to allow reuse of closed threads in threads but meh */
        if (thread_counter == THREADLIMIT) {
            printf("out of threads\n");
            return (EXIT_SUCCESS);
        }
        int* arg = &thread_fd[thread_counter];
        int rc = pthread_create(&threads[thread_counter], NULL, worker_thread, (void *) arg);
        if (rc) {
            printf("ERROR; return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
        rc = pthread_detach(threads[thread_counter]);
        thread_counter++;

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

/*
 * parse_message
 */
enum operations parse_message(char* buf[], char* reply[], struct parsed_msg* current_msg) {
    
    debug++;
    
    char* command;
    char* key;
    char* value;
    char* save;
    
    /* parse message into three components*/
    command = strtok_r(*buf, " ", &save);
    key = strtok_r(NULL, " ", &save);
    value = strtok_r(NULL, "\0", &save);
    
    /* map pointers to struct for operation function use */
    current_msg->key = key;
    current_msg->value = value;
    
    /* check if message has 3 non-null components */
    if (command == NULL || key == NULL || value == NULL) {
        return undefined_op;
        
    /* check that components are correct size */
    } else if ((strlen(command) > COMMANDSIZE) || (strlen(key) > KEYSIZE) || (strlen(value) > VALUESIZE)) {
        return undefined_op;

    /* figure out if command is valid, and return which if so*/
    } else {
        if (!strcmp("add", command)) {
            return add_op;
        } else if (!strcmp("getvalue", command)) {
            return getvalue_op;
        } else if (!strcmp("getall", command)) {
            return getall_op;
        } else if (!strcmp("remove", command)) {
            return remove_op;
        } else {
            return undefined_op;
        }
    }
}

/*
 * add_function - add (key, value) pair, if no existing pair with same key value
 */
void add_function(struct parsed_msg* current_msg) {
    sem_wait(&GL_head_mutex);
    char enter_success[] = "Success: recorded message\n\0";
    char* enter1 = enter_success;
    char duplicate[] = "Error: Duplicate key exists\n";
    char* dup1 = duplicate;
    /* Case 1: GL_head is NULL*/
    if (GL_head.active == 0) {
        printf("head null\n");
        GL_head.active = 1;
        GL_head.key = current_msg->key;
        GL_head.value = current_msg->value;
        sem_post(&GL_head_mutex);
        send_reply(current_msg->fd, &enter1);
        return;
    }
    
    sem_post(&GL_head_mutex);
    send_reply(current_msg->fd, &dup1);

}

/*
 * getvalue_function - return value from matching (key, value) pair, if any
 */
void getvalue_function(struct parsed_msg* current_msg) {
    sem_wait(&GL_head_mutex);






    sem_post(&GL_head_mutex);
}

/*
 * getall_function - return all (key, value) pairs
 */
void getall_function(struct parsed_msg* current_msg) {
    sem_wait(&GL_head_mutex);






    sem_post(&GL_head_mutex);
}

/*
 * remove_function - remove matching (key, value) pair, if any
 */
void remove_function(struct parsed_msg* current_msg) {
    sem_wait(&GL_head_mutex);






    sem_post(&GL_head_mutex);
}

/*
 * send_reply - send reply message back to connection
 */
void send_reply(int new_fd, char* reply[]) {
    if (send(new_fd, *reply, strlen(*reply), 0) == -1)
        perror("send");
}

/*
 * worker_thread - 
 */
void *worker_thread(void *arg1) {
    
    int* new_fd = (int*)arg1;
    
    printf("worker thread, new_fd = %i\n", *new_fd);
    
    char* buf = malloc(sizeof (char) * MAXDATASIZE);

    /* memory for all types of replies */
    char* reply = malloc(sizeof (char) * MAXDATASIZE);
    char error_msg[] = "Bad Query or oversized\n";
    char* err1 = error_msg;
    int numbytes;

    while (1) {
        if ((numbytes = recv(*new_fd, buf, MAXDATASIZE, 0)) == -1) {
            perror("recv");
            free(buf);
            free(reply);
            close(*new_fd);
            exit(1);
        }
        printf("recv msg = %s\n", buf);
        if (numbytes == 0) {
            continue;
            free(buf);
            free(reply);
            close(*new_fd);
            exit(0);
        } else {

            buf[numbytes - 1] = '\0';
            char* reply = malloc(sizeof (char) * MAXDATASIZE);
            struct parsed_msg current_message;
            current_message.fd = *new_fd;
            enum operations ret = parse_message(&buf, &reply, &current_message);

            switch (ret) {
                case add_op: add_function(&current_message); break;
                case getvalue_op: getvalue_function(&current_message); break;
                case getall_op: getall_function(&current_message); break;
                case remove_op: remove_function(&current_message); break;
                default: send_reply(*new_fd, &err1);
            }
        }
    }
}