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
#define MAXDATASIZE 222 /* max number of char we can get at once [10] + [10] + [200] + '\n' + '\0' */
#define COMMANDSIZE 10
#define KEYSIZE 10
#define VALUESIZE 200 
#define THREADLIMIT 100

/* STRUCTS & ENUMS */
typedef struct data_container data_container;
typedef struct parsed_msg parsed_msg;

enum operations {
    add_op,
    getvalue_op,
    getall_op,
    remove_op,
    undefined_op,
};

struct data_container {
    char key[KEYSIZE + 1];
    char value[VALUESIZE + 1];
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
data_container* GL_head;

/*
 * main - listens via tcp, parses, and performs basic data storage operations
 */
int main(int argc, char** argv) {

    if (sem_init(&GL_head_mutex, 0, 1) == -1) {
        perror("sem init failed");
    }

    if (argc != 2) {
        fprintf(stderr, "usage: [server listen port]\n");
        exit(1);
    }

    int sockfd;
    GL_head = NULL;
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

    /* prepare pthreads */
    pthread_t threads[THREADLIMIT];
    int thread_fd[THREADLIMIT];
    int thread_counter = 0;

    /* main accept loop, listen and spawn thread on incoming connections */
    while (1) {
        sin_size = sizeof their_addr;
        thread_fd[thread_counter] = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (thread_fd[thread_counter] == -1) {
            perror("accept");

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

    char* command;
    char* key;
    char* value;
    char* save;

    /* parse message into three components*/
    command = strtok_r(*buf, " \n", &save);
    key = strtok_r(NULL, " \n", &save);
    value = strtok_r(NULL, " \n", &save);

    /* map pointers to struct for operation function use */
    current_msg->key = key;
    current_msg->value = value;

    /* check if message has a possibly valid command */
    if (command == NULL && strlen(command) > COMMANDSIZE) {
        return undefined_op;
    }
    int param_count = 0;

    /* check that components are correct size */

    if (key != NULL) {
        if (strlen(key) > KEYSIZE) {
            return undefined_op;
        } else {
            if (key[strlen(key)] == '\n') {
                key[strlen(key)] = '\0';
            }
            param_count++;
        }
    }
    
    if (value != NULL) {
        if (strlen(value) > KEYSIZE) {
            return undefined_op;
        } else {
            if (value[strlen(value)] == '\n') {
                value[strlen(value)] = '\0';
            }
            param_count++;
        }
    }

    /* check if command is valid */
    if (!strcmp("add", command) && param_count == 2) {
        return add_op;
    } else if (!strcmp("getvalue", command) && param_count == 1) {
        return getvalue_op;
    } else if (!strcmp("getall", command) && param_count == 0) {
        return getall_op;
    } else if (!strcmp("remove", command) && param_count == 1) {
        return remove_op;
    } else {
        return undefined_op;
    }
}

/*
 * add_function - add (key, value) pair, if no existing pair with same key value
 */
void add_function(struct parsed_msg* new) {

    char enter_success[] = "Success: recorded message\n";
    char* enter1 = enter_success;
    char duplicate[] = "Error: Duplicate key exists\n";
    char* dup1 = duplicate;
    data_container* node;

    /* Case 1: GL_head is NULL*/
    sem_wait(&GL_head_mutex);
    if (GL_head == NULL) {
        GL_head = malloc(sizeof (data_container));
        strcpy(GL_head->key, new->key);
        strcpy(GL_head->value, new->value);
        GL_head->next = NULL;
        sem_post(&GL_head_mutex);
        send_reply(new->fd, &enter1);
        return;
    }
    /*Case 2: non-empty list*/
    node = GL_head;
    /* Add to list (non-recursive) */
    while (1) {
        /* check duplicate key*/

        if (strcmp(node->key, new->key) == 0) {
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &dup1);
            return;
        } else if (node->next == NULL) {
            node->next = malloc(sizeof (data_container));
            node = node->next;
            strcpy(node->key, new->key);
            strcpy(node->value, new->value);
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &enter1);
            return;
        } else {
            node = node->next;
        }
    }
}

/*
 * getvalue_function - return value from matching (key, value) pair, if any
 */
void getvalue_function(struct parsed_msg* new) {

    char err[] = "Error: key does not exist\n";
    char* err_p = err;

    data_container* node;
    sem_wait(&GL_head_mutex);
    node = GL_head;


    /* list is empty, so no match */
    if (GL_head == NULL) {
        sem_post(&GL_head_mutex);
        send_reply(new->fd, &err_p);
    }
    /* iterate list for match */
    while (1) {
        if (NULL == node) {
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &err_p);
            return;
        } else if (strcmp(node->key, new->key) == 0) {
            char* value = node->value;
            
            send_reply(new->fd, &value);

            sem_post(&GL_head_mutex);
            return;
        } else {
            node = node->next;
        }
    }
}

/*
 * getall_function - return all (key, value) pairs
 */
void getall_function(struct parsed_msg* new) {

    char err[] = "Error: no data exists\n";
    char* err_p = err;
    data_container* node;

    sem_wait(&GL_head_mutex);
    node = GL_head;
    
    /* empty list */
    if (NULL == node) {
        sem_post(&GL_head_mutex);
        send_reply(new->fd, &err_p);
        return;
    }
    
    /* iterate list, printing */
    while (1) {
        if (NULL == node) {
            sem_post(&GL_head_mutex);
            return;
        } else{
            char* value = node->value;
            send_reply(new->fd, &value);
            node = node->next;
        }
    }
}

/*
 * remove_function - remove matching (key, value) pair, if any
 */
void remove_function(struct parsed_msg* new) {

    char err[] = "Error: key doesn't exist\n";
    char* err_p = err;
    char success[] = "Success: deleted entry\n";
    char* success_p = success;

    data_container* node;
    data_container* prev_node;

    sem_wait(&GL_head_mutex);
    node = GL_head;
    prev_node = NULL;

    while (1) {
        /* empty list */
        if (NULL == node) {
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &err_p);
            return;
        } else if (strcmp(node->key, new->key) == 0) {
            
            /* only element */
            if (prev_node == NULL) {
                if (NULL == node->next) {
                    GL_head = NULL;
                    free(node);
                    sem_post(&GL_head_mutex);
                    send_reply(new->fd, &success_p);
                    return;

                    /* delete head, more data exists */
                } else {
                    GL_head = node->next;
                    free(node);
                    sem_post(&GL_head_mutex);
                    send_reply(new->fd, &success_p);
                    return;
                }
                /* not head */
            } else {
                prev_node->next = node->next;
                free(node);
                sem_post(&GL_head_mutex);
                send_reply(new->fd, &success_p);
                return;
            }
            /* miss */
        } else {
            prev_node = node;
            node = node->next;
        }
    }
}

/*
 * send_reply - send reply message back to connection
 */
void send_reply(int new_fd, char* reply[]) {

    printf("server: sending reply, %s\n", *reply);
    if (send(new_fd, *reply, strlen(*reply) + 1, 0) == -1)
        perror("send");
}

/*
 * worker_thread - 
 */
void *worker_thread(void *arg1) {

    /* fd to use with client */
    int* new_fd = (int*) arg1;
    /* memory for all types of receives */
    char* buf = malloc(sizeof (char) * MAXDATASIZE);
    /* memory for all types of replies */
    char* reply = malloc(sizeof (char) * MAXDATASIZE);
    /* error message stuff */
    char error_msg[] = "Bad Query or oversized\n";
    char* err1 = error_msg;
    /* recv size */
    int numbytes;

    /* worker loop */
    while (1) {
        /* listen for client message */
        if ((numbytes = recv(*new_fd, buf, MAXDATASIZE, 0)) == -1) {
            perror("recv");
            free(buf);
            free(reply);
            close(*new_fd);
            pthread_exit(NULL);
        }

        printf("server: received message, %s\n", buf);

        /* connection got closed, cleanup */
        if (numbytes == 0) {
            free(buf);
            free(reply);
            close(*new_fd);
            pthread_exit(NULL);

            /* received a message */
        } else {

            buf[numbytes - 1] = '\0';
            char* reply = malloc(sizeof (char) * MAXDATASIZE);
            struct parsed_msg current_message;
            current_message.fd = *new_fd;

            /* validate and parse message*/
            enum operations ret = parse_message(&buf, &reply, &current_message);

            /* perform operations requested by message */
            switch (ret) {
                case add_op: add_function(&current_message);
                    break;
                case getvalue_op: getvalue_function(&current_message);
                    break;
                case getall_op: getall_function(&current_message);
                    break;
                case remove_op: remove_function(&current_message);
                    break;
                default: send_reply(*new_fd, &err1);
            }
        }
    }
}