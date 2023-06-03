#include <stdint.h>

#include "FreeRTOS.h"
#include "message_buffer.h"
#include "semphr.h"
#include "task.h"
#include "FIRF.h"
#include "DLL.h"

static DLLObject * sampleSources;
static DLLObject * FIRFs;
static SemaphoreHandle_t firfListsSemaphore;

#define FIRF_batchSize 48

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

//tasks for all FIRF calculations
static void FIRF_task(void * taskData){
    uint32_t speedScaler = FIRF_SPEED_NORMAL;
    
    AudioSample_t * sampleBuffer = pvPortMalloc(sizeof(AudioSample_t) * FIRF_batchSize);
    
    while(1){
        //TODO evaluate of this ever returns to other code... priorities and stuff
        //if(xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)){
            //go through the list of sample sources, get a sample and check if anybody needs to calculate it. This does mean samples with no data sink are discarded
            DLL_FOREACH(currSrc, sampleSources){
                                //LATBSET = _LATB_LATB2_MASK;
                //get the sample data, as much as possible up to the maximum batch size
                FIRF_DataSource_t * src = (FIRF_DataSource_t *) (currSrc->data);
                
                uint32_t samplesReceived = (*(src->sampleRequestCallback))(src->callbackData, sampleBuffer, FIRF_batchSize);


                //did we actually read any data?
                if(samplesReceived > 0){
                    //check if a FIRF needs this data
                    DLL_FOREACH(currFRIF, FIRFs){
                        FIRF_t * firf = (FIRF_t *) (currFRIF->data);
                        if(firf->active && firf->dataSource == src){
                            
                            //yes, run the calculation for each of the samples with the amount of items to calculate scaled by the overall speed scale
                            for(uint32_t currSample = 0; currSample < samplesReceived; currSample++){
                                sampleBuffer[currSample] = FIRF_filter(firf, sampleBuffer[currSample], FIRF_SCALE_ITEMS(firf, speedScaler));
                            }
                            
                            //and finally write the data to all sinks
                            DLL_FOREACH(currSink, firf->dataSinks){
                                FIRF_DataSink_t * sink = (FIRF_DataSink_t *) currSink->data;
                                (*(sink->sampleReturnCallback))(sink->callbackData, sampleBuffer, samplesReceived);
                            }
                        }
                    }
                }
                                //LATBCLR = _LATB_LATB2_MASK;
            }
        //}
        //xSemaphoreGive(firfListsSemaphore);
            vTaskDelay(1);
    }
}
#pragma GCC pop_options

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

FIRF_DataSink_t * FIRF_addDataSink(FIRF_t * filter, FIRF_sampleReturnCallback callback, void* callbackData){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return NULL;
            
    FIRF_DataSink_t * sink = pvPortMalloc(sizeof(FIRF_DataSink_t));
    
    sink->sampleReturnCallback = callback;
    sink->callbackData = callbackData;
    
    DLL_add(sink, filter->dataSinks);
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
}

void FIRF_removeSignalSink(FIRF_t * filter, FIRF_sampleReturnCallback * sink){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return NULL;
    
    //remove the sink from the DLL
    DLL_removeData(sink, filter->dataSinks);
    
    vPortFree(sink);
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
}

FIRF_DataSource_t * FIRF_registerSignalSource(FIRF_sampleRequestCallback callback, void* callbackData){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return NULL;
    
    //initialise source struct
    FIRF_DataSource_t * ret = pvPortMalloc(sizeof(FIRF_DataSource_t));
    
    ret->sampleRequestCallback = callback;
    ret->callbackData = callbackData;
    
    //add the new source to the DLL
    DLL_add(ret, sampleSources);
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
    
    return ret;
}

void FIRF_removeSignalSource(FIRF_DataSource_t * source){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return;
    
    //remove all pointers to this source from the firf list (set them to NULL)
    DLL_FOREACH(currFIRF, FIRFs){
        FIRF_t * firf = (FIRF_t *) currFIRF->data;
        if(firf->dataSource == source) firf->dataSource = NULL;
    }
    
    //now remove the source from the source DLL
    DLL_removeData(source, sampleSources);
    
    //and finally free the struct
    vPortFree(source);
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
}

void FIRF_setSignalSource(FIRF_t * filter, FIRF_DataSource_t * source){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return;
    
    uint32_t found = 0;
    //first check if the source is even registered
    DLL_FOREACH(currSrc, sampleSources){
        FIRF_DataSource_t * src = (FIRF_DataSource_t *) currSrc->data;
        
        if(src == source){ 
            found = 1;
            break;
        }
    }
    
    if(!found){
        //hmm source is not registered. Assigning it to a firf would cause weirdness, so just return (and remember to return the semaphore)
        xSemaphoreGive(firfListsSemaphore);
        return;
    }
    
    //ok signal source is registered, all we need to do is to assign the pointer in the firf struct
    filter->dataSource = source;
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
}

void FIRF_init(){
    //create lists
    sampleSources = DLL_create();
    FIRFs = DLL_create();
    
    //create semaphore and make sure its available
    firfListsSemaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(firfListsSemaphore);
    
    //create the task, lowest system wide priority
    xTaskCreate(FIRF_task, "FIRF Task", configMINIMAL_STACK_SIZE+128, NULL, tskIDLE_PRIORITY + 1, NULL);
}

void FIRF_setTransferFunction(FIRF_t * filter){
    //TODO!
}

FIRF_t * FIRF_create(uint32_t filterLength){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return NULL;
    
    FIRF_t * ret = pvPortMalloc(sizeof(FIRF_t));
    
    ret->dataSinks = DLL_create();
    
    ret->samples = pvPortMalloc(sizeof(int32_t) * filterLength);
    ret->coefficients = pvPortMalloc(sizeof(int32_t) * filterLength);
    
    ret->active = 1;
    ret->currFilterStartIndex = 0;
    ret->filterLength = filterLength;
    
    //set a unity gain transfer function
    memset(ret->coefficients, 0, sizeof(int32_t) * filterLength);
    ret->coefficients[0] = FIRF_UNITYGAIN;
    
    //add the filter to the list
    DLL_add(ret, FIRFs);
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
    
    return ret;
}

void FIRF_destroy(FIRF_t * filter){
    //get the list semaphore, so we can be sure the FOREACH won't do stupid stuff
    if(!xSemaphoreTake(firfListsSemaphore, portMAX_DELAY)) return NULL;
    
    //remove the filter from the list if its in it
    DLL_removeData(filter, FIRFs);
    
    //remove all sinks and free the DLL
    DLL_FOREACH(currSink, filter->dataSinks){
        FIRF_DataSink_t * sink = (FIRF_DataSink_t *) currSink->data;
        DLL_remove(currSink);
        vPortFree(sink);
    }
    DLL_free(filter->dataSinks);
    
    //free the FIRF
    vPortFree(filter->samples);
    vPortFree(filter->coefficients);
    
    vPortFree(filter);
    
    //return the semaphore
    xSemaphoreGive(firfListsSemaphore);
}