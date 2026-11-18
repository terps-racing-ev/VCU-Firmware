#ifndef IO_H
#define IO_H
#include "cmsis_os.h"
#include "stm32f4xx_hal.h"
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

//Bascially useless until DEN algorithm is implemented, but potential errors that could be seen
typedef enum {
    EFUSE_STATUS_UNINITIALIZED = 0,
    EFUSE_STATUS_OFF,
    EFUSE_STATUS_ON,
    EFUSE_STATUS_OPEN_LOAD,
    EFUSE_STATUS_OVER_CURRENT,
    EFUSE_STATUS_SHORT_CIRCUIT,
    EFUSE_STATUS_DIAGNOSTIC_ERROR,
    EFUSE_STATUS_STALE
} EFuseStatus;

typedef struct {
    osMutexId_t mutex;

    /* eFuse command mapping: BTG IN or DRS EN */
    GPIO_TypeDef *in_port;
    uint16_t in_pin;

    /* Optional diagnostic enable. Set both fields to zero when unused. */
    GPIO_TypeDef *den_port;
    uint16_t den_pin;

    ADC_HandleTypeDef *adc;
    uint32_t is_channel;

    /* Requested and applied outputs */
    bool command;
    bool output_enabled;
    bool diagnostic_enabled;

    /* Current-sense feedback */
    uint16_t current_raw;
    int32_t current_ma;

    /* Interpreted state */
    EFuseStatus status;
    uint32_t last_updated;
} EFuse;

// Flag for comparator event
#define IO_COMP_EVENT 1

// Initializers, Setters and getters for IO/temps
HAL_StatusTypeDef IO_InitDigitalIO(DigitalIO* dio, const char* mutex_name);
HAL_StatusTypeDef IO_InitAnalogIO(AnalogIO* aio, const char* mutex_name);
HAL_StatusTypeDef IO_InitCurrent(Current* current, const char* mutex_name);
HAL_StatusTypeDef IO_InitVSense(VSense_t* v, const char* mutex_name);
HAL_StatusTypeDef IO_InitEFuse(EFuse* efuse, const char* mutex_name, GPIO_TypeDef *in_port, uint16_t in_pin, GPIO_TypeDef *den_port, uint16_t den_pin, ADC_HandleTypeDef *adc, uint32_t is_channel);
bool IO_GetDigitalIO(DigitalIO *dio);
uint16_t IO_GetAnalogIO(AnalogIO *aio);
int32_t IO_GetCurrent(Current *c);
uint32_t IO_GetVSense(VSense_t *v);
EFuseStatus IO_GetEFuseStatus(EFuse *efuse);
void IO_SetDigitalIO(DigitalIO *dio, bool value);
void IO_SetAnalogIO(AnalogIO *aio, uint16_t value);
void IO_SetCurrent(Current *c, int32_t value);
void IO_SetVSense(VSense_t *v, uint32_t value);
HAL_StatusTypeDef IO_SetEFuseCommand(EFuse *efuse, bool command);

//SDC Inputs
extern DigitalIO bspd_latched;
extern DigitalIO bspd_fault; //Is this SDC
extern DigitalIO imd_latched;
extern DigitalIO bms_latched;
extern DigitalIO sdc_post_raw;
extern DigitalIO sdc_post_ils;
extern DigitalIO sdc_post_buttons;
extern DigitalIO rtm_lamp_check;
//VCU
extern DigitalIO rtd;
//TPS
extern DigitalIO TSSI_green;
extern DigitalIO TSSI_red;
extern DigitalIO brake_light;
extern DigitalIO buzzer;
extern DigitalIO data_rail_enable;
extern DigitalIO data_rail_den;
extern AnalogIO apps1;
extern AnalogIO apps2;
extern AnalogIO front_brake_pressure;
extern AnalogIO rear_brake_pressure;
extern AnalogIO current_sense_12v_raw;
extern Current current_sense_12v;
extern AnalogIO output_voltage_5v_raw;
extern Current current_sense_5v;
extern AnalogIO data_rail_sns;

extern AnalogIO lv_current_raw;
extern Current lv_current;
extern AnalogIO hv_current_raw;
extern Current hv_current;

extern VSense_t output_v_sense_5;

extern EFuse batt_fans_L_efuse;
extern EFuse batt_fans_R_efuse;
extern EFuse rad_fans_efuse;
extern EFuse pump_efuse;
extern EFuse drs_efuse;
/* PB3 DRS_EN and ADC1 channel 14 (PC4 DRS_IS). PWM is separate. */

//BTG Outputs \/
//Acc fans already have defined algorithm, left and right cant be on at the same time
//batt_fans_L_in
//batt_fans_L_DEN
//batt_fans_R_in
//batt_fans_R_DEN
//rad_fans_in, coolant temp based (need historesis)
//BTG_DEN, diagnostic enable, whole additional project, not the most needed
//pump_in
//pump_DEN currently just rtd, can expand to power (need historesis)
//DRS EN and IS belong to drs_efuse; PB4 PWM is not an eFuse DEN signal
#endif /* IO_H */