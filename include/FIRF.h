#ifndef FIRF_INC
#define FIRF_INC

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"
#include "DLL.h"

typedef struct _FIRF_t_ FIRF_t;
typedef struct _FIRF_DataSource_t_ FIRF_DataSource_t;
typedef struct _FIRF_DataSink_t_ FIRF_DataSink_t;
typedef int16_t AudioSample_t;

#define FIRF_SPEED_NORMAL 0x10000
#define FIRF_UNITYGAIN 0x10000


typedef uint32_t (* FIRF_sampleRequestCallback)(void * usrData, AudioSample_t * dst, uint32_t dstSize);
typedef void (* FIRF_sampleReturnCallback)(void * usrData, AudioSample_t * src, uint32_t dstSize);

struct _FIRF_DataSource_t_{
    FIRF_sampleRequestCallback sampleRequestCallback;
    void * callbackData;
};

struct _FIRF_DataSink_t_{
    FIRF_sampleReturnCallback sampleReturnCallback;
    void * callbackData;
};

struct _FIRF_t_{
    //WARNING: keep in mind that if the datasource is removed while a FIRF is still pointing to it this pointer will become invalid
    FIRF_DataSource_t * dataSource;
    
    DLLObject * dataSinks;
    
    int32_t * coefficients;
    int32_t * samples;
    
    uint32_t currFilterStartIndex;
    uint32_t filterLength;
    
    uint32_t active;
};

void FIRF_init();
void FIRF_setTransferFunction(FIRF_t * filter);
FIRF_t * FIRF_create(uint32_t filterLength);
void FIRF_destroy(FIRF_t * filter);
void FIRF_setSignalSource(FIRF_t * filter, FIRF_DataSource_t * source);
void FIRF_removeSignalSource(FIRF_DataSource_t * source);
FIRF_DataSource_t * FIRF_registerSignalSource(FIRF_sampleRequestCallback callback, void* callbackData);
void FIRF_removeSignalSink(FIRF_t * filter, FIRF_sampleReturnCallback * sink);
FIRF_DataSink_t * FIRF_addDataSink(FIRF_t * filter, FIRF_sampleReturnCallback callback, void* callbackData);

#endif