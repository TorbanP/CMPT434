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


enum state_id{
    READY,
    ACTIVE,
    COMPLETE
};


/* the message package & state details */
typedef struct {
    char data[SEQSIZE + DATASIZE];
    
    struct timeval sent;
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
    int *shared_state = mmap(NULL, (sizeof(int) * lines), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    

    /* get messages from file to save time , hardcoded but meh, store in frame_array*/

    size_t buffsize = DATASIZE;
    FILE *stream;
    stream = fopen("data", "r");
    if (stream == NULL)
        exit(EXIT_FAILURE);
    int i = 0;
    int seq_id = INITSEQID;
    char temp_msg[DATASIZE];
    while (fgets(temp_msg, buffsize, stream)) {
        seq_id = i + INITSEQID;
        snprintf(frame_array[i].data, 10, "%d", seq_id);
        strcat(frame_array[i].data, " ");
        strcat(frame_array[i].data, temp_msg);
        shared_state[i] = READY;
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
            
            shared_state[strtol(buf, NULL, 10)] = COMPLETE;
        }
    } else {
        /* parent - sends frames */
        
        while (1) {
            
            /* while there are frames NOT COMPLETE, we are NOT done*/
            int j;
            int complete_count;
            for (j=0;j < lines; j++){
                if(shared_state[j] == COMPLETE){
                    complete_count++;
                }
            }
            if (complete_count == lines) {
                return 0;
            }

            /* see if the window is full, if not, send oldest message */
            j = 0;
            int active_frames = 0;
            while (active_frames < strtol(argv[3], NULL, 10)) {
                if (shared_state[j] == 0) {
                    /* send message */
                    if ((numbytes = sendto(sockfd, frame_array[j].data, DATASIZE, 0, p->ai_addr, p->ai_addrlen)) == -1) {
                        perror("talker: sendto");
                        exit(1);
                    }
                    /*set time of sent*/
                    gettimeofday(&(frame_array[j].sent), NULL);
                    shared_state[j] == 1;
                    active_frames++;
                    printf("sender: sent %d bytes to %s = %s", numbytes, argv[1], frame_array[j].data);
                } else if (shared_state[j] == ACTIVE){
                    active_frames++;
                } else {
                    if (j >= lines){
                        break;
                    }
                }
                j++;
            }

            shared_state[4] = READY;
            printf("sender: state = %d\n", shared_state[4]);
            shared_state[4] = ACTIVE;
            printf("sender: state = %d\n", shared_state[4]);
            shared_state[4] = COMPLETE;
            printf("sender: state = %d\n", shared_state[4]);


            /* check for any timed out packets*/
            j = 0;
            for (j = 0; j < lines; j++) {
                printf("sender: timeout state = %d\n", shared_state[j]);
                if (shared_state[j] == ACTIVE) {
                    gettimeofday(&stop, NULL);
                    if ((stop.tv_sec - frame_array[j].sent.tv_sec) > timeout) {
                        shared_state[j] == READY;
                    }
                }
            }



        }
        close(sockfd);

        return 0;
    }
}