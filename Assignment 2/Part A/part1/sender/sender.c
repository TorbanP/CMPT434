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
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/mman.h>


#define DATASIZE 257
#define SEQSIZE 6
#define INITSEQID 0

/* contains a list of messages to send. originally had more items inside hence the struct */
typedef struct {
    char data[SEQSIZE + DATASIZE];

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
int main(int argc, char *argv[]) {
    int *LAR = mmap(NULL, sizeof (int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int GL_LFS;
    int timeout = atoi(argv[4]);
    struct timeval start, stop;
    *LAR = INITSEQID; /* last ack received */
    GL_LFS = INITSEQID; /* last frame sent  */
    int sockfd;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    if (argc != 5) {
        fprintf(stderr, "usage: IP PORT WINDOWSIZE TIMEOUT\n");
        exit(1);
    }

    /* the frame data storage */

    /* find num lines in the file */
    FILE *linecount;
    int temp = 0;
    int lines = 0;
    linecount = fopen("data", "r");
    if (linecount == NULL)
        exit(EXIT_FAILURE);

    while ((temp = fgetc(linecount)) != EOF) {
        if (temp == '\n')
            lines++;
    }
    fclose(linecount);

    frame frame_array[lines];

    /* get messages from file to save time , hardcoded but meh, store in frame_array*/

    size_t buffsize = DATASIZE;
    FILE *stream;
    stream = fopen("data", "r");
    if (stream == NULL)
        exit(EXIT_FAILURE);
    int i = 0;
    int seq_id = INITSEQID;
    char* temp_msg[DATASIZE];
    while (fgets(temp_msg, buffsize, stream)) {
        seq_id = i + INITSEQID;
        snprintf(frame_array[i].data, 10, "%d", seq_id);
        strcat(frame_array[i].data, " ");
        strcat(frame_array[i].data, temp_msg);
        printf("%s", frame_array[i].data);
        i++;
    }
    fclose(stream);

    /* UDP setup stuff */

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    /* loop through all the results and make a socket */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    pid_t pid = fork();
    if (pid == 0) {
        /* child - listens for ACK, increments GL_LAR*/
        char buf[DATASIZE];
        addr_len = sizeof their_addr;

        while (1) {
            if ((numbytes = recvfrom(sockfd, buf, DATASIZE - 1, 0, (struct sockaddr *) &their_addr, &addr_len)) == -1) {
                perror("recvfrom");
                exit(1);
            }
            printf("Sender: ACK =  %s", buf);
            if (strtol(buf, NULL, 10) == *LAR) {
                *LAR = *LAR + 1;
                printf(" = ok, LAR = %d\n", *LAR);
            }
        }
    } else {
        /* parent - sends frames */
        while (1) {

            /* check if i can send data to receiver */
            int LARtemp = *LAR;
            int j = 0;
            while (GL_LFS - LARtemp + 1 < strtol(argv[3], NULL, 10)) {
                /* check that there are still messages to run*/
                if (j + LARtemp >= lines) {
                    break;
                } else if (LARtemp > lines) {
                    return 0;
                }
                /* oldest packet for timeout check */
                if (j == 0) {
                    gettimeofday(&start, NULL);
                }

                if ((numbytes = sendto(sockfd, frame_array[LARtemp + j].data, DATASIZE, 0, p->ai_addr, p->ai_addrlen)) == -1) {
                    perror("talker: sendto");
                    exit(1);
                }
                GL_LFS = LARtemp + j;
                j++;
                printf("sender: sent %d bytes to %s = %s", numbytes, argv[1], frame_array[GL_LFS].data);
            }
            /* check if oldest sent has timed out, reset LFS if so to trigger retry*/
            gettimeofday(&stop, NULL);
            if ((stop.tv_sec - start.tv_sec) > timeout) {
                GL_LFS = *LAR;
            }
        }
        close(sockfd);

        return 0;
    }
}