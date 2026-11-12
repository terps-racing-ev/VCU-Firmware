#ifndef IO_H
#define IO_H
#include "cmsis_os.h"
#include "stm32f4xx_hal_conf.h"
//#include "moving_average.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>


/**
 * @struct DigitalIO
 * @brief Structure for digital I/O with thread-safe access
 */
typedef struct {
    osMutexId_t mutex;          /**< Mutex for thread-safe access */
    bool value;                 /**< Current digital value */
    uint32_t last_updated;      /**< Timestamp of last update (ms) */
} DigitalIO;

/**
 * @struct AnalogIO
 * @brief Structure for analog I/O with thread-safe access
 */
typedef struct {
    osMutexId_t mutex;          /**< Mutex for thread-safe access */
    uint16_t value;             /**< Current analog value */
    uint32_t last_updated;      /**< Timestamp of last update (ms) */
} AnalogIO;

typedef struct {
    osMutexId_t mutex;          /**< Mutex for thread-safe access */
    //MovingAverage_Data_t ma;    /**< Moving average for filtering */
    int32_t value;              /**< Filtered current in mA */
    uint32_t last_updated;
} Current;

typedef struct {
    osMutexId_t mutex;          /**< Mutex for thread-safe access */
    //MovingAverage_Data_t ma;    /**< Moving average for filtering */
    uint32_t value;             /**< Filtered voltage in mV */
    uint32_t last_updated;
} VSense_t;

// Flag for comparator event
#define IO_COMP_EVENT 1

// Initializers, Setters and getters for IO/temps
HAL_StatusTypeDef IO_InitDigitalIO(DigitalIO* dio, const char* mutex_name);
HAL_StatusTypeDef IO_InitAnalogIO(AnalogIO* aio, const char* mutex_name);
HAL_StatusTypeDef IO_InitCurrent(Current* current, const char* mutex_name);
HAL_StatusTypeDef IO_InitVSense(VSense_t* v, const char* mutex_name);
bool IO_GetDigitalIO(DigitalIO *dio);
uint16_t IO_GetAnalogIO(AnalogIO *aio);
int32_t IO_GetCurrent(Current *c);
uint32_t IO_GetVSense(VSense_t *v);
void IO_SetDigitalIO(DigitalIO *dio, bool value);
void IO_SetAnalogIO(AnalogIO *aio, uint16_t value);
void IO_SetCurrent(Current *c, int32_t value);
void IO_SetVSense(VSense_t *v, uint32_t value);

/*
Example use
extern DigitalIO sdc;
extern DigitalIO imd;
extern DigitalIO bms_fault;

extern AnalogIO cs_low_raw;
extern AnalogIO cs_high_raw;
extern AnalogIO therm;
extern AnalogIO batt_raw;
extern AnalogIO inv_raw;

extern Temp ref_temp;
extern Current cs_low;
extern Current cs_high;
extern Current pack_curr;
extern VSense_t batt;
extern VSense_t inv;
*/
#endif /* IO_H */