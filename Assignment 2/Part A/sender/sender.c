/* 
 * File:   sender.c
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

#define FRAMESIZE 10
#define DATASIZE 256
#define MAXFRAME 10
#define INITSEQID 0

typedef struct {
    
    char data[DATASIZE];
    int seq_id;
    
} frame;

/* 
 * main
 * Args: 
 * IP address of the receiver, 
 * the port number that the receiver will be using to receive data, 
 * the maximum sending window size
 * timeout value (in seconds)
 * the number of lines to send
 */
int main(int argc, char *argv[])
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;


    if (argc != 6) {
        fprintf(stderr, "usage: IP PORT WINDOWSIZE TIMEOUT\n");
        exit(1);
    }


    frame frame_array[strtol(argv[5], NULL, 10)];

    char *arrptr;
    int ret;
    size_t buffsize = DATASIZE;
    char temp[DATASIZE];

    FILE *stream;
    stream = fopen("data", "r");
    if (stream == NULL)
        exit(EXIT_FAILURE);
    int i = 0;
    while(fgets(frame_array[i].data, buffsize, stream)){
        frame_array[i].seq_id = i;
        printf("%s",frame_array[i].data);
        i++;
    }
     

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }

    if ((numbytes = sendto(sockfd, argv[2], strlen(argv[2]), 0,
             p->ai_addr, p->ai_addrlen)) == -1) {
        perror("talker: sendto");
        exit(1);
    }

    freeaddrinfo(servinfo);

    printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
    close(sockfd);

    return 0;
}