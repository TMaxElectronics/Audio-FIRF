#ifndef FIRF_INC
#define FIRF_INC

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"

typedef struct _FIRF_t_ FIRF_t;
typedef struct _FIRF_DataSource_t_ FIRF_DataSource_t;
typedef int16_t AudioSample_t;

#define FIRF_SPEED_NORMAL 0x10000

typedef int32_t (* FIRF_sampleRequestCallback)(void * data);

struct _FIRF_DataSource_t_{
    FIRF_sampleRequestCallback sampleRequestCallback;
    void * callbackData;
    
    uint32_t ID;
};

struct _FIRF_t_{
    uint32_t dataSourceID;
    
    StreamBufferHandle_t outputBuffer;
    
    int32_t * coefficients;
    int32_t * samples;
    
    uint32_t currFilterStartIndex;
    uint32_t filterLength;
    
    uint32_t active;
};

FIRF_t * FIRF_create(uint32_t filterLength);

#endif