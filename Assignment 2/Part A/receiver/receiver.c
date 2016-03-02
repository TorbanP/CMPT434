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

/* get sockaddr, IPv4 or IPv6: */
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    int expected_id = INITSEQID;
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[DATASIZE];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];

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

        printf("Receiver: got packet from %s, ", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *) &their_addr), s, sizeof s));
        printf("packet is %d bytes long, message:\n", numbytes);
        buf[numbytes] = '\0';

        /* get seq id out*/
        int seq_id = 0;
        sscanf(buf, "%d", &seq_id);
        printf("%s", buf);

        /* is seq id correct? */

        if (seq_id == expected_id) {
            expected_id++;
            printf("receiver: good id, sending ACK\n");
            /* convert seq_id back to str */
            char ACK[DATASIZE];
            snprintf(ACK, 10, "%d", seq_id);
            
            
            int numbytes;
            if ((numbytes = sendto(sockfd, ACK , sizeof(ACK), 0, (struct sockaddr *) &their_addr, sizeof(their_addr))) == -1) {
                perror("receiver: sendto");
                exit(1);
            }

        }


        /*Send ACK*/
    }
    close(sockfd);

    return 0;
}