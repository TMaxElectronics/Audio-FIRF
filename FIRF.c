#include <stdint.h>

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"
#include "FIRF.h"

static AudioSample_t FIRF_filter(FIRF_t * filter, AudioSample_t inputSample){
    uint32_t currSampleIndex = filter->currFilterStartIndex;
    
    int32_t accumulator = 0;
    filter->samples[filter->currFilterStartIndex] = inputSample;
    
    int32_t * sample = &filter->samples[filter->currFilterStartIndex];
    int32_t * coefficient = &filter->coefficients[filter->currFilterStartIndex];
    int32_t * INVALID_firstInvalidPointer = &filter->samples[filter->filterLength];
    int32_t * INVALID_endPointer = sample; //end once we have reached the start again
    
    do{
        accumulator += (*(sample++) * *(coefficient++)) >> 16;
        if(sample == INVALID_firstInvalidPointer) sample = filter->samples;
    }while(sample != INVALID_endPointer);
    
    return accumulator >> 16;
}

static void FIRF_task(void * taskData){
    FIRF_t * filter = (FIRF_t *) taskData;
    
    while(filter->active){
        AudioSample_t currSample = 0;
        
        if(xStreamBufferReceive(filter->inputBuffer, &currSample, sizeof(AudioSample_t), portMAX_DELAY)){
            currSample = FIRF_filter(filter, currSample);
            xStreamBufferSend(filter->outputBuffer, &currSample, sizeof(AudioSample_t), 0);
        }
    }
    
    //TODO free data
}

FIRF_t * FIRF_create(uint32_t filterLength){
    FIRF_t * ret = pvPortMalloc(sizeof(FIRF_t));
    
    ret->inputBuffer = xStreamBufferCreate(sizeof(AudioSample_t) * filterLength, 0);
    ret->outputBuffer = xStreamBufferCreate(sizeof(AudioSample_t) * filterLength, 0);
    
    ret->samples = pvPortMalloc(sizeof(int32_t) * filterLength);
    ret->coefficients = pvPortMalloc(sizeof(int32_t) * filterLength);
    
    ret->active = 1;
    ret->currFilterStartIndex = 0;
    ret->filterLength = filterLength;
    
    xTaskCreate(FIRF_task, "Audio Task", configMINIMAL_STACK_SIZE, (void*) ret, tskIDLE_PRIORITY + 1, NULL);
    
    return ret;
}