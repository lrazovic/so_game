#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "image.h"
#include "surface.h"
#include "vehicle.h"
#include "world.h"
#include "world_viewer.h"

typedef struct handler_args_s {
    /*
     * Specify fields for the arguments that will be populated in the
     * main thread and then accessed in connection_handler(void* arg).
     **/
    int socket_desc;
    struct sockaddr_in *client_addr;
} handler_args_t;

void *connection_handler(void *arg) {
    handler_args_t *args = (handler_args_t *)arg;

    /* We make local copies of the fields from the handler's arguments
     * data structure only to share as much code as possible with the
     * other two versions of the server. In general this is not a good
     * coding practice: using simple indirection is better! */
    int socket_desc = args->socket_desc;
    struct sockaddr_in *client_addr = args->client_addr;

    int ret, recv_bytes;

    char buf[1024];
    size_t buf_len = sizeof(buf);
    size_t msg_len;

    char *quit_command = SERVER_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    // parse client IP address and port
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    uint16_t client_port =
        ntohs(client_addr->sin_port); // port number is an unsigned short

    // send welcome message
    sprintf(buf,
            "It's Work! You are %s talking on port %hu.\n "
            "I will stop if you send me %s :-)\n",
            client_ip, client_port, quit_command);
    msg_len = strlen(buf);
    while ((ret = send(socket_desc, buf, msg_len, 0)) < 0) {
        if (errno == EINTR)
            continue;
        ERROR_HELPER(-1, "Cannot write to the socket");
    }
    while (1) {
    }
    // close socket
    ret = close(socket_desc);
    ERROR_HELPER(ret, "Cannot close socket for incoming connection");

    /** SOLUTION
     *
     * Suggestions
     * - free memory allocated for this thread inside the main thread
     * - print a debug message to inform the user that the thread has
     *   completed its work
     */

    if (DEBUG)
        fprintf(stderr,
                "Thread created to handle the request has completed.\n");

    free(args->client_addr); // do not forget to free this buffer!
    free(args);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: %s <elevation_image> <texture_image> <port>\n", argv[1]);
        exit(-1);
    }
    char *elevation_filename = argv[1];
    char *texture_filename = argv[2];
    int server_port = strtol(argv[3], NULL, 0);
    char *vehicle_texture_filename = "./images/arrow-right.ppm";
    printf("loading elevation image from %s ... ", elevation_filename);

    // load the images
    Image *surface_elevation = Image_load(elevation_filename);
    if (surface_elevation) {
        printf("Done! \n");
    } else {
        printf("Fail! \n");
    }

    printf("loading texture image from %s ... ", texture_filename);
    Image *surface_texture = Image_load(texture_filename);
    if (surface_texture) {
        printf("Done! \n");
    } else {
        printf("Fail! \n");
    }

    printf("loading vehicle texture (default) from %s ... ",
           vehicle_texture_filename);
    Image *vehicle_texture = Image_load(vehicle_texture_filename);
    if (vehicle_texture) {
        printf("Done! \n");
    } else {
        printf("Fail! \n");
    }

    int ret;

    int socket_desc, client_desc;
    struct sockaddr_in server_addr = {0};
    int sockaddr_len = sizeof(struct sockaddr_in);

    // Initialize socket for listening
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_HELPER(socket_desc, "Could not create socket");
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // Enable SO_REUSEADDR to quickly restart our server after a crash
    int reuseaddr_opt = 1;
    ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt,
                     sizeof(reuseaddr_opt));
    ERROR_HELPER(ret, "Cannot set SO_REUSEADDR option");

    // bind address to socket
    ret = bind(socket_desc, (struct sockaddr *)&server_addr, sockaddr_len);
    ERROR_HELPER(ret, "Cannot bind address to socket");

    // start listening
    ret = listen(socket_desc, MAX_CONN_QUEUE);
    ERROR_HELPER(ret, "Cannot listen on socket");

    // we allocate client_addr dynamically and initialize it to zero
    struct sockaddr_in *client_addr = calloc(1, sizeof(struct sockaddr_in));

    while (1) {
        if (DEBUG)
            fprintf(stderr, "Waiting on accept...\n");
        client_desc = accept(socket_desc, (struct sockaddr *)client_addr,
                             (socklen_t *)&sockaddr_len);
        if (client_desc == -1 && errno == EINTR)
            continue; // check for interruption by signals
        ERROR_HELPER(client_desc, "Cannot open socket for incoming connection");

        if (DEBUG)
            fprintf(stderr, "Incoming connection accepted...\n");

        pthread_t thread;

        // put arguments for the new thread into a buffer
        handler_args_t *thread_args = malloc(sizeof(handler_args_t));
        thread_args->socket_desc = client_desc;
        thread_args->client_addr = client_addr;

        ret = pthread_create(&thread, NULL, connection_handler,
                             (void *)thread_args);
        PTHREAD_ERROR_HELPER(ret, "Could not create a new thread");

        if (DEBUG)
            fprintf(stderr, "New thread created to handle the request!\n");

        ret = pthread_detach(thread); // I won't phtread_join() on this thread
        PTHREAD_ERROR_HELPER(ret, "Could not detach the thread");

        // we can't just reset fields: we need a new buffer for client_addr!
        client_addr = calloc(1, sizeof(struct sockaddr_in));
    }

    return 0;
}
