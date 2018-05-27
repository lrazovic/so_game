#ifndef COMMON_H
#define COMMON_H
#include "errno.h"
#include <stdio.h>

// macro to simplify error handling
#define GENERIC_ERROR_HELPER(cond, errCode, msg)                 \
    do {                                                         \
        if (cond) {                                              \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode)); \
            exit(EXIT_FAILURE);                                  \
        }                                                        \
    } while (0)

#define ERROR_HELPER(ret, msg) GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg) GENERIC_ERROR_HELPER((ret != 0), ret, msg)

// Config
#define DEBUG 1 
#define BUFFER_SIZE 1000000
#define TCP_PORT 8888
#define UDP_PORT 8888 
#define SERVER_ADDRESS "127.0.0.1"
#define SENDER_SLEEP_C 200 * 1000
#define RECEIVER_SLEEP_C 50 * 1000
#define SENDER_SLEEP_S 300 * 1000
#define RECEIVER_SLEEP_S 20 * 1000
#define WORLDSIZE 256
#define WORLD_SIZE 256
#define TOUCHED 1
#define UNTOUCHED 0

#endif
