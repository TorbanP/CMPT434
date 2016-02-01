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
enum operations parse_message(char buf[], char* reply[], struct parsed_msg* current_msg);
void send_reply(int new_fd, char* reply[], struct sockaddr_storage client_addr);
void add_function(struct parsed_msg* current_msg, struct sockaddr_storage client_addr);
void getvalue_function(struct parsed_msg* current_msg, struct sockaddr_storage client_addr);
void getall_function(struct parsed_msg* current_msg, struct sockaddr_storage client_addr);
void remove_function(struct parsed_msg* current_msg, struct sockaddr_storage client_addr);
in_port_t get_in_port(struct sockaddr *sa);

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

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes = 0;
    struct sockaddr_storage server_addr, client_addr;
    char* buf = malloc(sizeof (char) * MAXDATASIZE);
    socklen_t addr_len;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    /* error message stuff */
    char error_msg[] = "Bad Query or oversized\n";
    char* err1 = error_msg;
    struct parsed_msg current_message;
    current_message.fd = sockfd;
    while (1) {

        addr_len = sizeof server_addr;
        if ((numbytes = recvfrom(sockfd, buf, MAXDATASIZE - 1, 0, (struct sockaddr *) &client_addr, &addr_len)) == -1) {
            perror("recvfrom");
            continue;
        }

        buf[numbytes] = '\0';

        /* validate and parse message*/
        enum operations ret = parse_message(buf, NULL, &current_message);

        /* perform operations requested by message */
        switch (ret) {
            case add_op: add_function(&current_message, client_addr);
                break;
            case getvalue_op: getvalue_function(&current_message, client_addr);
                break;
            case getall_op: getall_function(&current_message, client_addr);
                break;
            case remove_op: remove_function(&current_message, client_addr);
                break;
            default: send_reply(sockfd, &err1, client_addr);
        }
    }
    close(sockfd);
    return 0;
}

/* get port, IPv4 or IPv6: */

in_port_t get_in_port(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return (((struct sockaddr_in*) sa)->sin_port);
    }

    return (((struct sockaddr_in6*) sa)->sin6_port);
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
enum operations parse_message(char buf[], char* reply[], struct parsed_msg* current_msg) {

    char* command;
    char* key;
    char* value;
    char* save;

    /* parse message into three components*/
    command = strtok_r(buf, " \n\0", &save);
    key = strtok_r(NULL, " \n\0", &save);
    value = strtok_r(NULL, "\n\0", &save);

    /* map pointers to struct for operation function use */
    current_msg->key = key;
    current_msg->value = value;

    /* check if message has a possibly valid command */
    if (command != NULL) {
        if (strlen(command) > COMMANDSIZE) {
            return undefined_op;
        } else {
            if (command[strlen(command)] == '\n') {
                key[strlen(key)] = '\0';
            }
        }
    } else{
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
        if (strlen(value) > VALUESIZE) {
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
void add_function(struct parsed_msg* new, struct sockaddr_storage client_addr) {

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
        send_reply(new->fd, &enter1, client_addr);
        return;
    }
    /*Case 2: non-empty list*/
    node = GL_head;
    /* Add to list (non-recursive) */
    while (1) {
        /* check duplicate key*/
        if (strcmp(node->key, new->key) == 0) {
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &dup1, client_addr);
            return;
        } else if (node->next == NULL) {
            node->next = malloc(sizeof (data_container));
            node = node->next;
            strcpy(node->key, new->key);
            strcpy(node->value, new->value);
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &enter1, client_addr);
            return;
        } else {
            node = node->next;
        }
    }
}

/*
 * getvalue_function - return value from matching (key, value) pair, if any
 */
void getvalue_function(struct parsed_msg* new, struct sockaddr_storage client_addr) {

    char err[] = "Error: key does not exist\n";
    char* err_p = err;

    data_container* node;
    sem_wait(&GL_head_mutex);
    node = GL_head;

    /* list is empty, so no match */
    if (GL_head == NULL) {
        sem_post(&GL_head_mutex);
        send_reply(new->fd, &err_p, client_addr);
    }
    /* iterate list for match */
    while (1) {
        if (NULL == node) {
            sem_post(&GL_head_mutex);
            send_reply(new->fd, &err_p, client_addr);
            return;
        } else if (strcmp(node->key, new->key) == 0) {
            char* value = node->value;

            send_reply(new->fd, &value, client_addr);

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
void getall_function(struct parsed_msg* new, struct sockaddr_storage client_addr) {

    char err[] = "Error: no data exists\n";
    char* err_p = err;
    data_container* node;

    sem_wait(&GL_head_mutex);
    node = GL_head;

    /* empty list */
    if (NULL == node) {
        sem_post(&GL_head_mutex);
        send_reply(new->fd, &err_p, client_addr);
        return;
    }

    /* iterate list, printing */
    while (1) {
        if (NULL == node) {
            sem_post(&GL_head_mutex);
            return;
        } else {
            char* key = node->key;
            char* value = node->value;
            char* buf = malloc(sizeof (char) * MAXDATASIZE);
            strcat(buf, "(");
            strcat(buf, key);
            strcat(buf, ", ");
            strcat(buf, value);
            strcat(buf, ")\n");
            usleep(1000);
            send_reply(new->fd, &buf, client_addr);
            node = node->next;
        }
    }
}

/*
 * remove_function - remove matching (key, value) pair, if any
 */
void remove_function(struct parsed_msg* new, struct sockaddr_storage client_addr) {

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
            send_reply(new->fd, &err_p, client_addr);
            return;
        } else if (strcmp(node->key, new->key) == 0) {

            /* only element */
            if (prev_node == NULL) {
                if (NULL == node->next) {
                    GL_head = NULL;
                    free(node);
                    sem_post(&GL_head_mutex);
                    send_reply(new->fd, &success_p, client_addr);
                    return;

                    /* delete head, more data exists */
                } else {
                    GL_head = node->next;
                    free(node);
                    sem_post(&GL_head_mutex);
                    send_reply(new->fd, &success_p, client_addr);
                    return;
                }
                /* not head */
            } else {
                prev_node->next = node->next;
                free(node);
                sem_post(&GL_head_mutex);
                send_reply(new->fd, &success_p, client_addr);
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
void send_reply(int new_fd, char* reply[], struct sockaddr_storage client_addr) {

    if (sendto(new_fd, *reply, strlen(*reply), 0, (struct sockaddr *) &client_addr, sizeof (struct sockaddr)) == -1)
        perror("send");
}
