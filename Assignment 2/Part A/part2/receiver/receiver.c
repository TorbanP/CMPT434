/* 
 * File:   receiver.c
 * Author: TJP873
 * CMPT 434 - Assignment 2, Part A
 * 
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
#include <arpa/inet.h>
#include <netdb.h>

#define DATASIZE 261
#define INITSEQID 0

/* represents if a slot in out_of_order is in use */
enum state_enum{
    ACTIVE,
    INACTIVE
};

/* stores out of order frames*/
typedef struct {
    int id;
    char data[DATASIZE];
    enum state_enum state;
} out_of_order;



/* get sockaddr, IPv4 or IPv6: */
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

/*DEPRECIATED AS PER ASSIGNMENT
 * Asks if user wants to drop packet
 */
int dropper(int placeholder) {
    fprintf(stdout, "send ACK y/n?\n");

    int nbytes = 100;
    char *my_string;
    /* These 2 lines are the heart of the program. */
    my_string = (char *) malloc(nbytes + 1);
    getline(&my_string, &nbytes, stdin);

    if (my_string[0] == 'Y' || my_string[0] == 'y') {
        return 1;
    } else {
        return 0;
    }
}
/* Probability dropper
 * 
 * 100 represents no loss
 * 50 = 50% loss
 * 0 = 100% loss
 */
int dropper2(int probability){
    //sleep(1);
    //srand(time(NULL));
    if(rand()%100 < probability){
        return 1;
    } else {
        fprintf(stderr, "Oops I did it again!\n");
        return 0;
    }
    

}


int main(int argc, char *argv[])
{
    srand(time(NULL));
    int expected_id = INITSEQID;
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[DATASIZE];
    socklen_t addr_len;

    if (argc != 4) {
        fprintf(stderr, "usage: PORT PROBABILITY RMAX\n");
        exit(1);
    }
    
    int probability = atoi(argv[2]);
    
    /* storage for out_of_order frames */
    out_of_order buffer[atoi(argv[3])];
    int i;
    for (i=0;i<atoi(argv[3]);i++){
        buffer[i].state = INACTIVE;
    }
    
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("Receiver: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("Receiver: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Receiver: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);
    printf("Receiver: waiting to recvfrom...\n");
    while (1) {
        /* listen for message */
        addr_len = sizeof their_addr;
        if ((numbytes = recvfrom(sockfd, buf, DATASIZE - 1, 0, (struct sockaddr *) &their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        buf[numbytes] = '\0';

        /* get seq id out*/
        int seq_id = 0;
        sscanf(buf, "%d", &seq_id);

        /* do i drop this packet? */
        if (dropper2(probability)) {
            /* good message */
            if (seq_id == expected_id) {
                printf("receiver: good id, message: %s", buf);
                expected_id++;
                /* convert seq_id back to str */
                char ACK[DATASIZE];
                snprintf(ACK, 10, "%d", seq_id);


                int numbytes;
                if ((numbytes = sendto(sockfd, ACK, sizeof (ACK), 0, (struct sockaddr *) &their_addr, sizeof (their_addr))) == -1) {
                    perror("receiver: sendto");
                    exit(1);
                }
                /* check if we have the next-in-order message in buffer */
                int target_id = seq_id + 1;  
                for (i = 0; i < atoi(argv[3]); i++) {
                    if (buffer[i].state == ACTIVE && buffer[i].id == target_id) {
                        buffer[i].state = INACTIVE;
                        target_id++;
                        expected_id++;
                        printf("receiver: stor id, message: %s", buffer[i].data);
                        i = -1; /* reset for loop incase next message is also here */
                    }
                }
            } else if (seq_id > expected_id) {
                /* we need to store this out of order message if there is room */
                /* we also need to make sure we dont already have it*/
                int duplicate = 0;
                for (i = 0; i < atoi(argv[3]); i++) {
                    if (buffer[i].state == ACTIVE && buffer[i].id == seq_id){
                        duplicate = 1;
                    }
                }
                    
                for (i = 0; i < atoi(argv[3]); i++) {
                    if (buffer[i].state == INACTIVE && duplicate == 0) {
                        buffer[i].state = ACTIVE;
                        buffer[i].id = seq_id;
                        strncpy(buffer[i].data, buf, DATASIZE);
                        //printf("receiver: Stored Out-of-order message: %s", buffer[i].data);
                        /* send ACK */
                        /* convert seq_id back to str */
                        char ACK[DATASIZE];
                        snprintf(ACK, 10, "%d", seq_id);
                        if ((numbytes = sendto(sockfd, ACK, sizeof (ACK), 0, (struct sockaddr *) &their_addr, sizeof (their_addr))) == -1) {
                            perror("receiver: sendto");
                            exit(1);
                        }

                        break;
                    }
                }
            }
        }
    }
    close(sockfd);

    return 0;
}