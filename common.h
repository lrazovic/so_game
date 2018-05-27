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
#define TIME_TO_SLEEP 0.1
#define RECEIVER_SLEEP 0.05
#define WORLDSIZE 256
#define WORLD_SIZE 256
#define TOUCHED 1
#define UNTOUCHED 0

#endif
