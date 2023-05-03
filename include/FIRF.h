#ifndef FIRF_INC
#define FIRF_INC

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"

typedef int16_t AudioSample_t;

typedef struct{
    StreamBufferHandle_t inputBuffer;
    StreamBufferHandle_t outputBuffer;
    
    int32_t * coefficients;
    int32_t * samples;
    
    uint32_t currFilterStartIndex;
    uint32_t filterLength;
    
    uint32_t active;
} FIRF_t;

FIRF_t * FIRF_create(uint32_t filterLength);

#endif