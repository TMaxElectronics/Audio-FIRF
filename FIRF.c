#include <stdint.h>

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "task.h"
#include "FIRF.h"
#include "DLL.h"

static DLLObject * sampleSources;
static DLLObject * FIRFs;

#define FIRF_SCALE_ITEMS(FIRF, SCALE) (SCALE * FIRF->filterLength) >> 16

//force Os optimisation for this part of the code. Necessary to get decent performance
#pragma GCC push_options
#pragma GCC optimize ("Os")
static AudioSample_t FIRF_filter(FIRF_t * filter, AudioSample_t inputSample, uint32_t itemsToCalculate){
    //limit size variable
    if(itemsToCalculate > filter->filterLength) itemsToCalculate = filter->filterLength;
    
    int32_t accumulator = 0;
    filter->samples[filter->currFilterStartIndex] = inputSample;
    
    //calculate pointers
    int32_t * sample = &(filter->samples[filter->currFilterStartIndex]);
    int32_t * coefficient = filter->coefficients;
    int32_t * INVALID_firstInvalidPointer = &(filter->samples[filter->filterLength]);
    
    //end pointer is the first pointer that must not be calculated. If itemsToCalculate is filterLength then it must be the pointer to the sample that was just added
    uint32_t lastID = filter->currFilterStartIndex + itemsToCalculate;
    if(lastID >= filter->filterLength) lastID -= filter->filterLength;
    int32_t * INVALID_endPointer = &(filter->samples[lastID]); //end once we have reached the start again
    
    do{
        accumulator += (*(sample++) * *(coefficient++)) >> 16;
        if(sample == INVALID_firstInvalidPointer) sample = filter->samples;
    }while(sample != INVALID_endPointer);
    
    if(++filter->currFilterStartIndex >= filter->filterLength) filter->currFilterStartIndex = 0;
    
    return accumulator;
}
#pragma GCC pop_options

//tasks for all FIRF calculations
static void FIRF_task(void * taskData){
    uint32_t speedScaler = FIRF_SPEED_NORMAL;
    
    while(1){
        //go through the list of sample sources, get a sample and check if anybody needs to calculate it. This does mean samples with no data sink are discarded
        DLL_FOREACH(currSrc, sampleSources){
            //get the sample data
            FIRF_DataSource_t * src = (FIRF_DataSource_t *) (currSrc->data);
            AudioSample_t currSample = (*(src->sampleRequestCallback))(src->callbackData);
            
            //check if a FIRF needs this data
            DLL_FOREACH(currFRIF, FIRFs){
                FIRF_t * firf = (FIRF_t *) (currFRIF->data);
                if(firf->active && firf->dataSourceID == src.ID){
                    //yes, run the calculation with the amount of items to calculate scaled by the overall speed scale
                    FIRF_filter(firf, currSample, FIRF_SCALE_ITEMS(firf, speedScaler));
                }
            }
        }
    }
}

/*
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
}*/

FIRF_t * FIRF_create(uint32_t filterLength){
    FIRF_t * ret = pvPortMalloc(sizeof(FIRF_t));
    
    ret->inputBuffer = xStreamBufferCreate(sizeof(AudioSample_t) * filterLength, 0);
    ret->outputBuffer = xStreamBufferCreate(sizeof(AudioSample_t) * filterLength, 0);
    
    ret->samples = pvPortMalloc(sizeof(int32_t) * filterLength);
    ret->coefficients = pvPortMalloc(sizeof(int32_t) * filterLength);
    
    ret->active = 1;
    ret->currFilterStartIndex = 0;
    ret->filterLength = filterLength;
    
    ret->coefficients[0] = 0xffff;
    
    xTaskCreate(FIRF_task, "FIRF Task", configMINIMAL_STACK_SIZE, (void*) ret, tskIDLE_PRIORITY + 1, NULL);
    
    return ret;
}