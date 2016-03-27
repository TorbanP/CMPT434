/* 
 * File:   main.c
 * Author: tpeterson
 *
 * Created on March 20, 2016, 8:35 PM
 */

// Borrowed Code
// http://stackoverflow.com/questions/2999075/generate-a-random-number-within-range/2999130#2999130


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>




#define NUM_THREADS     10
#define buffer_count    10
#define base_location_x 500
#define base_location_y 500
#define time_per_tick   1
#define max_coordinate  1000
#define starting_port   30000 // this will be a range (start -> (start+NUM_THREADS))
#define max_msg_length  20 // "location_x location_y 65535 NID \n" 4+1+4+1+5+1+3+1 
#define max_destination 254 // max domain name length = 253 + \n
// message passed to newly launched threads

struct thread_data {
    int thread_id, location_x, location_y, distance_per_tick, transmit_range, max_sent_packets, num_ticks;
    bool debug_print;
    char *node_name; //N01 - N10
};

// message data

struct message_data {
    char buffer[max_msg_length], destination[max_destination]; // these are not pointers to handle concurrency problem
    int port;
    bool debug_print;

};

struct thread_data thread_data_array[NUM_THREADS];
char *node_name[NUM_THREADS + 1];

int rand_lim(int limit) {
    // return a random number between 0 and limit inclusive.


    int divisor = RAND_MAX / (limit + 1);
    int retval;

    do {
        retval = rand() / divisor;
    } while (retval > limit);

    return retval;
}





// send message

void *send_udp_message(void *threadarg) {
    struct message_data *my_data;
    my_data = (struct message_data *) threadarg;

    if(my_data->debug_print) 
        printf("send_udp_message %s to %s:%d\n", my_data->buffer, my_data->destination, my_data->port);
    pthread_exit(NULL);
}

// sensor node

void *sensor_node(void *threadarg) {
    struct thread_data *my_data;
    my_data = (struct thread_data *) threadarg;

    //for sending messages
    pthread_t send_threads[NUM_THREADS + 1];
    struct message_data my_message[NUM_THREADS+1];

    // on startup
    if (my_data->debug_print) printf("Sensor Node #%s started!(%d,%d)\n", my_data->node_name, my_data->location_x, my_data->location_y);

    // test print
    if (my_data->debug_print) printf("Debug enabled for #%s\n", my_data->node_name);

    // run loop (basically)
    int tick_no;
    int j;
    for (tick_no = 0; tick_no < my_data->num_ticks; tick_no++) {
        // pick a direction and travel, N, E, S, W == 0, 1, 2, 3
        //sleep(1);
        j = rand_lim(3);

        switch (j) {
            case 0:
                if (my_data->debug_print) printf("Sensor Node #%s Going North, ", my_data->node_name);

                my_data->location_x = my_data->location_x - my_data->distance_per_tick;
                if (my_data->location_x < 0) {
                    if (my_data->debug_print) printf("BOUNCE (%d,%d), ", my_data->location_x, my_data->location_y);
                    my_data->location_x = my_data->location_x * -1;
                }
                break;

            case 1:
                if (my_data->debug_print) printf("Sensor Node #%s Going East, ", my_data->node_name);

                my_data->location_y = my_data->location_y + my_data->distance_per_tick;

                if (my_data->location_y > max_coordinate) {
                    if (my_data->debug_print) printf("BOUNCE (%d,%d), ", my_data->location_x, my_data->location_y);
                    my_data->location_y = 2 * max_coordinate - my_data->location_y;
                }
                break;

            case 2:
                if (my_data->debug_print) printf("Sensor Node #%s Going South, ", my_data->node_name);

                my_data->location_x = my_data->location_x + my_data->distance_per_tick;

                if (my_data->location_x > max_coordinate) {
                    if (my_data->debug_print) printf("BOUNCE (%d,%d), ", my_data->location_x, my_data->location_y);
                    my_data->location_x = 2 * max_coordinate - my_data->location_x;
                }
                break;

            case 3:
                if (my_data->debug_print) printf("Sensor Node #%s Going West, ", my_data->node_name);

                my_data->location_y = my_data->location_y - my_data->distance_per_tick;
                if (my_data->location_y < 0) {
                    if (my_data->debug_print) printf("BOUNCE (%d,%d), ", my_data->location_x, my_data->location_y);
                    my_data->location_y = my_data->location_y * -1;
                }
                break;
            default: printf("Error - Broken Direction\n");
                break;

        }

        if (my_data->debug_print) printf("New location = (%d,%d)\n", my_data->location_x, my_data->location_y);

        // create my message
        char buffer[max_msg_length];
        int n = sprintf(buffer, "%d %d %d %s", my_data->location_x, my_data->location_y, tick_no, my_data->node_name);
        char destination[] = "127.0.0.1";

        // Send Message to all threads + base (except for myself)
        int return_code;
        for (j = 0; j < NUM_THREADS + 1; j++) {
            // avoid sending message to myself
            if (j != my_data->thread_id) {

                strcpy(my_message[j].buffer, buffer);
                strcpy(my_message[j].destination, destination);
                my_message[j].debug_print = my_data->debug_print;
                my_message[j].port = starting_port + j;
                
                if (my_data->debug_print) {
                    //printf("Sensor Node #%s sending UDP Message to port %d, %s\n", my_data->node_name, my_message[j].port, buffer);
                }

                return_code = pthread_create(&send_threads[j], NULL, send_udp_message, (void *) &my_message[j]);
                if (return_code) {
                    printf("ERROR; return code from pthread_create() is %d\n", return_code);
                    exit(-1);

                }

            }
        }
    }
    //sleep(5);
    //pthread_exit(NULL);
}

// main:
// arg[1] - K - Number of ticks (simulate steps )
// arg[2] - D - Distance per tick
// arg[3] - R - Transmission range
// arg[4] - P - max number of data packets to send
// arg[5] - n - debug print sensor node num

int main(int argc, char *argv[]) {
    srand(time(NULL));
    pthread_t threads[NUM_THREADS];

    int return_code, i;

    // hard coded names as thread count is fixed
    node_name[0] = "N00";
    node_name[1] = "N01";
    node_name[2] = "N02";
    node_name[3] = "N03";
    node_name[4] = "N04";
    node_name[5] = "N05";
    node_name[6] = "N06";
    node_name[7] = "N07";
    node_name[8] = "N08";
    node_name[9] = "N09";
    node_name[10] = "N10";

    // parse command line arguments
    if (argc != 6) {
        printf("Incorrect Usage! ./main 'D' 'K' 'R' 'P' 'n'");
        exit(-1);
    }

    for (i = 1; i < 6; i++) {
        if (argv[i] <= 0) {
            printf("Incorrect Usage! ./main 'D' 'K' 'R' 'P' 'n'");
            exit(-1);
        }
    }

    // Launch the sensor node threads
    for (i = 0; i < NUM_THREADS; i++) {

        thread_data_array[i].thread_id = i + 1;
        thread_data_array[i].distance_per_tick = atoi(argv[2]);
        thread_data_array[i].max_sent_packets = atoi(argv[4]);
        thread_data_array[i].transmit_range = atoi(argv[3]);
        thread_data_array[i].num_ticks = atoi(argv[1]);
        thread_data_array[i].node_name = node_name[i + 1];
        thread_data_array[i].location_x = rand_lim(1000);
        thread_data_array[i].location_y = rand_lim(1000);

        // debug enable
        if (atoi(argv[5] - 1) == i) {
            thread_data_array[i].debug_print = true;
        } else {
            thread_data_array[i].debug_print = false;
        }




        return_code = pthread_create(&threads[i], NULL, sensor_node, (void *) &thread_data_array[i]);
        if (return_code) {
            printf("ERROR; return code from pthread_create() is %d\n", return_code);
            exit(-1);
        }
    }

    // I becometh the Base Station

    int location_x = base_location_x, location_y = base_location_y;



    printf("Base Station #%s started!(%d,%d)\n", node_name[0], location_x, location_y);

    pthread_exit(NULL);
}