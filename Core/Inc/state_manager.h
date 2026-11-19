#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <stdbool.h>
#include "state.h"
#include "can.h"


// overall timing constants 

#define STATE_REFRESH_PERIOD_MS   10U    
#define HVC_TIMEOUT_MS            500U   
#define RTD_SOUND_MS              2000U   
#define LAMP_CHECK_DURATION_MS    3000U   


// checking each switch

#ifndef CHECK_APPS
#define CHECK_APPS 1
#endif

#ifndef CHECK_BSE
#define CHECK_BSE 1
#endif

#ifndef CHECK_SDC
#define CHECK_SDC 1
#endif

#ifndef CHECK_RED_CAR
#define CHECK_RED_CAR 1
#endif

#ifndef CHECK_HVC_TIMEOUT
#define CHECK_HVC_TIMEOUT 1
#endif


// debug setting 

#define DEBUG_BIT_IGNORE_RTD_BRAKES      (1U << 0)   
#define DEBUG_BIT_IGNORE_BSPD            (1U << 1)

// high voltage can id
#ifndef CAN_ID_HVC_IO_SUMMARY
#define CAN_ID_HVC_IO_SUMMARY  
#endif

// what data needs to go betweeen files

typedef struct {
    bool lamp_check_done;
    bool rtd_active;                   
    bool brakes_engaged;               
    bool bse_hard_braking;          
    bool apps_above_bap;               
    bool apps_below_bap_reestablish;   
    bool apps_valid;                
    bool bse_valid;                    
    bool sdc_ok;                      
    bool red_car;                      
    bool hvc_timeout;                  
    bool buzzer_done;                 
    bool ignore_rtd_brakes;            
    bool ignore_bspd;    
} State_Inputs_t;

HAL_StatusTypeDef State_Manager_Init(void);

void StateManager_Task(void *argument);
void State_Manager_Step(const State_Inputs_t *in);
bool State_RxHandler(const CAN_Message_t *msg);
bool State_Manager_ClearFaultsTxTrigger(void);
bool State_Manager_DeadCarTxTrigger(void);

#endif 
