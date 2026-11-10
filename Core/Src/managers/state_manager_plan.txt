STATE MANAGER PLAN (VCU-Firmware)
=================================

Sources used for this plan:
  - Old logic:      TREV4-Controls/src/state_machine.c   (what the car did last year)
  - Code structure: HVC-Firmware/Core/Src/Managers/state_manager.c
                    HVC-Firmware/Core/Src/Data/state.c  (how our managers should look)
  - HVC CAN layout: HVC-Firmware/hvc.dbc + Core/Inc/Config/can_id.h

Legend:  [EXISTS]  you can call it today on this branch
         [BRANCH]  exists only on origin/io_manager/start (not merged yet)
         [MISSING] nothing in VCU-Firmware yet, has to be written
         [?]       needs an answer from the team before it can be written


###########################################################################
PART 1 - DATA THE TREV4 STATE MACHINE USES, AND WHERE TO GET IT IN VCU-FIRMWARE
###########################################################################

TREV4 StateMachine_Update() reads everything at the top of the function
(state_machine.c lines 31-42), then runs one switch statement. Below is every
input, what TREV4 called, and what to use in VCU-Firmware instead.

Note: the meaning of each TREV4 field is taken from how state_machine.c uses
it. I did not read apps.c / bse.c / lights.c / buzzer.c themselves.

---------------------------------------------------------------------------
1. APPS (accelerator pedal) data
---------------------------------------------------------------------------
   TREV4 call:   const APPS_Data_t* apps = APPS_GetData();     (sensors/apps.c)
   Fields used:
     apps->valid                            -> false = HARD fault
     apps->above_bap_threshold              -> part of BAP (brake/accel) fault
     apps->below_bap_reestablish_threshold  -> leave BAP_FAULT back to DRIVING

   VCU-Firmware: [MISSING] [?]
     - No pedal code exists. ADC1/ADC2 are set up by CubeMX (hadc1, hadc2,
       MX_ADC1_Init / MX_ADC2_Init in Core/Src/main.c) and pins PA0-PA7,
       PC1-PC5 are ADC inputs in the .ioc, but none are labelled as APPS.
     - Question: are the pedals wired to the VCU ADC, or read by another
       board and sent over CAN?
     - Either way, the plan is: a sensor/IO module fills a struct in data/
       (e.g. data/io.h) and the state manager only reads that struct.

---------------------------------------------------------------------------
2. BSE (brake pressure) data
---------------------------------------------------------------------------
   TREV4 call:   const BSE_Data_t* bse = BSE_GetData();         (sensors/bse.c)
   Fields used:
     bse->valid            -> false = HARD fault
     bse->hard_braking     -> part of BAP fault
     bse->brakes_engaged   -> needed (with RTD) to enter ready-to-drive

   VCU-Firmware: [MISSING] [?]   Same situation as APPS: no brake pin is labelled.

---------------------------------------------------------------------------
3. RTD (ready-to-drive) switch
---------------------------------------------------------------------------
   TREV4 call:   bool rtd_active = RTD_IsActive();               (io/rtd.c)

   VCU-Firmware: [BRANCH]
     - Pin: RTD_Pin / RTD_GPIO_Port (PB0, input with pull-up) in
       Core/Inc/main.h on origin/io_manager/start.
     - Raw read: HAL_GPIO_ReadPin(RTD_GPIO_Port, RTD_Pin)
       BUT that should live in the IO manager, which then publishes the
       value through data/io.h [MISSING]. The state manager reads data/io.h.
     - [?] Pull-up input: is "pressed/on" GPIO_PIN_RESET (low)? Check schematic.
     - TREV4 had util/debounce.c. Nothing like it exists here yet; the IO
       manager should debounce before publishing.

---------------------------------------------------------------------------
4. "Red car" (TSSI red / dead car)
---------------------------------------------------------------------------
   TREV4 call:   bool is_red_car = Lights_isRedCar();            (io/lights.c)
   Used as:      HARD fault, and a rising edge sends the "dead car" message.

   VCU-Firmware: [BRANCH] + [EXISTS over CAN, handler MISSING]
     [?] Confirm what "red car" meant in lights.c. Probably IMD or BMS fault
         (that is what drives the red TSSI). If so, sources are:
     - Local pins [BRANCH]: IMD_Latched_Pin (PB10), BMS_Latched_Pin (PB11),
       BSPD_Latched_Pin (PB1), BSPD_Fault_Pin (PB2). Read by IO manager.
     - From HVC over CAN: HVC_IO_Summary, ID 0x004001F0 (extended):
         byte 0 bit 1 = HVC_IMD_Fault
         byte 0 bit 2 = HVC_BMS_Fault
       Received through a State_RxHandler (see Part 2).

---------------------------------------------------------------------------
5. Buzzer finished
---------------------------------------------------------------------------
   TREV4 call:   Buzzer_GetState() == BUZZER_STATE_DONE          (io/buzzer.c)

   VCU-Firmware: [MISSING], but it does not need its own module.
     - Pin [BRANCH]: Buzzer_EN_Pin (PB5, output).
     - Suggested: the state manager remembers the tick it entered
       PLAYING_RTD_SOUND and says "done" when
           osKernelGetTickCount() - rtd_sound_start >= RTD_SOUND_MS
       The IO manager turns Buzzer_EN on whenever the published state is
       PLAYING_RTD_SOUND. So no buzzer state machine is needed.
     - osKernelGetTickCount() [EXISTS] (cmsis_os2, 1 tick = 1 ms here).

---------------------------------------------------------------------------
6. Shutdown circuit OK (HVC summary)
---------------------------------------------------------------------------
   TREV4 call:   CAN_RX_GetHVCSummaryData()->sdc_ok             (can/can_rx.c)

   VCU-Firmware: [EXISTS on the bus, handler MISSING]
     - HVC sends HVC_IO_Summary, ID 0x004001F0 (extended, 7 bytes):
         byte 0 bit 0 = HVC_SDC_Open   (1 = open = NOT ok, so sdc_ok = !bit)
     - HVC also sends HVC_BMS_State, ID 0x004001F1 (extended, 7 bytes):
         byte 0     = HVC state (0 PRE_INIT, 1 RUNNING, 2 CHARGING,
                                 3 BALANCING, 4 ERRORED)
         bytes 1-4  = HVC error mask
       HVC sends this every ~100 ms (STATE_REFRESH_FREQ_MS in HVC).
     - To receive: write State_RxHandler() and add it to the CAN_RxHandlers[]
       table in Core/Inc/managers/can_manager.h (line 17). [EXISTS: table]
     - The VCU CAN filters accept extended IDs only - HVC uses extended, OK.
     - Local alternative/backup [BRANCH]: SDC_Post_Buttons_Pin (PB14),
       SDC_Post_ILs_Pin (PB9), SDC_Post_Raw_Pin (PB8).
     - NEW vs TREV4: also record WHEN the last HVC message arrived, so that
       "HVC went silent" becomes a HARD fault. TREV4 did not check this.

---------------------------------------------------------------------------
7. Debug bits (ignore RTD brakes / ignore brake plausibility)
---------------------------------------------------------------------------
   TREV4 call:   RuntimeConfig_GetI32(RUNTIME_PARAM_DEBUG_DEFINES, &dbg_bits)
                 bit DEBUG_BIT_IGNORE_RTD_BRAKES
                 bit DEBUG_BIT_IGNORE_BRAKE_PLAUSIBILITY

   VCU-Firmware: [EXISTS: API]  [MISSING: the variable]
     - Call: Config_ReadVariable(CONFIG_STATE_DEBUG_BITS, &value)
       from Core/Inc/data/config.h. Values are uint16_t.
     - Add the variable in 3 places (addresses must never change later):
         Core/Inc/data/config.h   #define CONFIG_STATE_DEBUG_BITS  4
         Core/Src/data/config.c   {CONFIG_STATE_DEBUG_BITS, 0, 0, 3},
         config.json              (so tools/pcan_config.py can edit it)
     - Default MUST be 0 (no checks ignored).

---------------------------------------------------------------------------
8. "EEPROM loaded" (LOADING state)
---------------------------------------------------------------------------
   TREV4 call:   RuntimeConfig_IsEepromLoadComplete()

   VCU-Firmware: not needed as-is.
     - Config_Manager_Init() runs EEPROM_Init() in main.c BEFORE the
       scheduler starts, so config is already loaded when the task first runs.
     - Suggested replacement for LOADING: wait until the first HVC message
       has been received (like HVC's PRE_INIT waits for its managers), so
       the car never leaves LOADING with unknown SDC status.

---------------------------------------------------------------------------
9. Outputs TREV4 produced (other code read these)
---------------------------------------------------------------------------
   StateMachine_GetState()            -> torque controller etc.
       VCU: State_GetState() in new data/state.h        [MISSING - Part 2]
   StateMachine_ClearFaultsTxTrigger() -> CAN TX sent "clear faults"
   StateMachine_DeadCarTxTrigger()     -> CAN TX sent "dead car"
       VCU: send directly with CAN_SendCTRLMsg(&msg)    [EXISTS: data/can.h]
       [?] Which CAN IDs / who receives these (inverter? dash?). Check
           TREV4-Controls/VCU.dbc and can/can_tx.c.

---------------------------------------------------------------------------
SUMMARY TABLE
---------------------------------------------------------------------------
 TREV4 input                    VCU-Firmware source                     Status
 -----------------------------  --------------------------------------  ---------
 apps->valid / bap thresholds   APPS module -> data/io.h                MISSING ?
 bse->valid/hard_braking/engaged BSE module -> data/io.h                MISSING ?
 RTD_IsActive()                 RTD_Pin via IO manager -> data/io.h     BRANCH
 Lights_isRedCar()              IMD/BMS latched pins, or HVC 0x004001F0 BRANCH/CAN
 Buzzer done                    tick timer inside state manager         write it
 hvc_summary->sdc_ok            HVC 0x004001F0 byte0 bit0 (inverted)    CAN, handler MISSING
 debug bits                     Config_ReadVariable(...)                API EXISTS
 EEPROM loaded                  already done in main.c init             not needed
 current time                   osKernelGetTickCount()                  EXISTS
 send CAN                       CAN_SendCTRLMsg() / CAN_SendDAQMsg()    EXISTS


###########################################################################
PART 2 - BAREBONE STRUCTURE OF THE NEW STATE MANAGER
###########################################################################

Same layout as HVC: a data/ file that OWNS the state (so other managers can
read it without talking to the state manager), and a managers/ file that
RUNS the state machine. The switch logic is ported from TREV4.

Files:
   Core/Inc/data/state.h              NEW   state enum, error bits, getters
   Core/Src/data/state.c              NEW   mutex-protected get/set
   Core/Inc/managers/state_manager.h  NEW   init/task/handler prototypes, tunables
   Core/Src/managers/state_manager.c  (empty now) the actual state machine
   + small edits listed in "WIRING" at the end

Everything below is a skeleton: it shows the shape and what goes where.
It has not been compiled.

===========================================================================
2.1  Core/Inc/data/state.h
===========================================================================

#ifndef DATA_STATE_H
#define DATA_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os.h"
#include "main.h"                 /* HAL_StatusTypeDef */

/* (a) The states. Same as TREV4 so the logic ports 1:1.
 *     Explicit numbers because this byte goes out on CAN, and the DBC
 *     must match. Never reorder; only add at the end. */
typedef enum {
    VCU_STATE_LOADING           = 0,
    VCU_STATE_LAMP_CHECK        = 1,
    VCU_STATE_NOT_READY         = 2,
    VCU_STATE_PLAYING_RTD_SOUND = 3,
    VCU_STATE_DRIVING           = 4,
    VCU_STATE_BAP_FAULT         = 5,   /* Brake/Accel Plausibility */
    VCU_STATE_HARD_FAULT        = 6
} VCU_State_t;

/* (b) Error bits, HVC style. Instead of TREV4's two bools
 *     (hard_fault, bap_fault) every reason gets its own bit. This lets
 *     the CAN message say WHY the car faulted. */
typedef uint32_t VCU_ErrorMask_t;

typedef enum {
    VCU_ERR_APPS_INVALID = 0,
    VCU_ERR_BSE_INVALID  = 1,
    VCU_ERR_SDC_OPEN     = 2,
    VCU_ERR_RED_CAR      = 3,
    VCU_ERR_HVC_TIMEOUT  = 4,
    VCU_ERR_BAP          = 5        /* the only "soft" fault */
} VCU_ErrorBit_t;

#define SET_ERROR(mask, bit) ((mask) |= ((VCU_ErrorMask_t)1U << (uint32_t)(bit)))

/* Every bit except BAP is a hard fault. */
#define VCU_HARD_FAULT_MASK (~((VCU_ErrorMask_t)1U << VCU_ERR_BAP))

/* (c) Accessors. Other managers (torque, IO, CAN) call ONLY these. */
HAL_StatusTypeDef State_InitState(void);
void State_GetState(VCU_State_t *state);
void State_SetState(VCU_State_t new_state);
void State_GetErrorMask(VCU_ErrorMask_t *mask);
void State_SetErrorMask(VCU_ErrorMask_t mask);

#endif /* DATA_STATE_H */

Explanation:
 (a) The enum is the list of states. It lives in data/ (not in the
     manager) because the torque code and IO code also need it, and the
     README rule is that managers never include each other.
 (b) The error mask replaces TREV4's local hard_fault/bap_fault bools.
     "hard_fault" becomes (errors & VCU_HARD_FAULT_MASK) != 0 and
     "bap_fault" becomes the BAP bit. Same logic, more information.
 (c) Getters/setters with the same names as HVC's data/state.h, so both
     codebases read the same. Writer: only the state manager. Readers:
     anyone.

===========================================================================
2.2  Core/Src/data/state.c
===========================================================================

#include "data/state.h"

/* (a) Private: other files can't touch these directly, only through the
 *     functions. (HVC makes bms_state extern; static is safer.) */
static osMutexId_t     state_mutex = NULL;
static VCU_State_t     vcu_state   = VCU_STATE_LOADING;
static VCU_ErrorMask_t vcu_errors  = 0U;

/* (b) Called once from State_Manager_Init(), before the scheduler starts. */
HAL_StatusTypeDef State_InitState(void)
{
    const osMutexAttr_t attr = { .name = "State_Mutex" };
    state_mutex = osMutexNew(&attr);
    if (state_mutex == NULL) {
        return HAL_ERROR;
    }
    vcu_state  = VCU_STATE_LOADING;   /* always boot into LOADING */
    vcu_errors = 0U;
    return HAL_OK;
}

/* (c) Lock, copy, unlock. Same pattern as HVC State_GetState(). */
void State_GetState(VCU_State_t *state)
{
    if (state == NULL) return;
    if (state_mutex != NULL) osMutexAcquire(state_mutex, osWaitForever);
    *state = vcu_state;
    if (state_mutex != NULL) osMutexRelease(state_mutex);
}

void State_SetState(VCU_State_t new_state)          { /* same pattern */ }
void State_GetErrorMask(VCU_ErrorMask_t *mask)      { /* same pattern */ }
void State_SetErrorMask(VCU_ErrorMask_t mask)       { /* same pattern */ }

Explanation:
 (a) The data. Only this file can change it directly.
 (b) The mutex must be created before any task runs, which is why it is
     called from the manager's Init (main.c calls Init before
     osKernelStart()). Boot state is always LOADING = safe.
 (c) The mutex makes sure a reader never sees a half-written value. Never
     call these from an interrupt (mutexes can't be used in ISRs). The CAN
     RX handlers run in the CAN *task*, not the ISR, so they are fine.

===========================================================================
2.3  Core/Inc/managers/state_manager.h
===========================================================================

#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <stdbool.h>
#include "data/state.h"
#include "data/can.h"

/* (a) Timing */
#define STATE_REFRESH_PERIOD_MS   10U     /* [?] how fast TREV4 ran it */
#define HVC_TIMEOUT_MS            500U    /* HVC sends every ~100 ms */
#define RTD_SOUND_MS              2000U   /* [?] check FSAE rule (1-3 s) */

/* (b) Turn checks on/off while parts are missing (HVC does the same).
 *     A check set to 0 is IGNORED, so set to 1 before the car drives. */
#define CHECK_APPS          0   /* 0 until APPS code exists */
#define CHECK_BSE           0   /* 0 until BSE code exists  */
#define CHECK_SDC           1
#define CHECK_RED_CAR       1
#define CHECK_HVC_TIMEOUT   1

/* (c) Debug bits stored in config (Part 1, item 7) */
#define DEBUG_BIT_IGNORE_RTD_BRAKES          (1U << 0)
#define DEBUG_BIT_IGNORE_BRAKE_PLAUSIBILITY  (1U << 1)

/* (d) Public functions */
HAL_StatusTypeDef State_Manager_Init(void);
void StateManager_Task(void *argument);
bool State_RxHandler(const CAN_Message_t *msg);

#endif /* STATE_MANAGER_H */

Explanation:
 (a) All the numbers you might tune in one place.
 (b) Lets the code build and run before APPS/BSE exist. Danger: with
     CHECK_APPS 0 the car ignores a broken pedal. These should all be 1
     before driving.
 (c) Same meaning as TREV4's debug bits, read from Config_ReadVariable.
 (d) Task name MUST be StateManager_Task: that is the name CubeMX put in
     main.c (from the .ioc). HVC calls it State_ManagerTask; if you want
     that name, change it in CubeMX, not in main.c.

===========================================================================
2.4  Core/Src/managers/state_manager.c
===========================================================================

#include "managers/state_manager.h"
#include "data/config.h"
#include "main.h"

/* ---- (a) Everything the transition logic needs, gathered in one place.
 *          Field names match TREV4 so the switch ports directly. */
typedef struct {
    bool apps_valid;
    bool apps_above_bap;               /* apps->above_bap_threshold */
    bool apps_below_bap_reestablish;   /* apps->below_bap_reestablish_threshold */
    bool bse_valid;
    bool bse_hard_braking;
    bool brakes_engaged;
    bool rtd_active;
    bool red_car;
    bool sdc_ok;
    bool buzzer_done;
    bool hvc_seen;                     /* got at least one HVC message */
    bool ignore_rtd_brakes;
    bool ignore_brake_plausibility;
} State_Inputs_t;

/* ---- (b) Latest HVC data. Written by State_RxHandler (CAN task),
 *          read by StateManager_Task. Each is one aligned word/byte,
 *          so reads are atomic on the M4. If you add fields that must
 *          stay consistent together, protect them with a mutex. */
static volatile uint32_t hvc_last_rx_tick = 0U;
static volatile bool     hvc_seen         = false;
static volatile bool     hvc_sdc_open     = true;   /* assume worst */
static volatile bool     hvc_imd_fault    = false;
static volatile bool     hvc_bms_fault    = false;

/* ---- (c) Private bookkeeping, only touched by this task. */
static uint32_t rtd_sound_start = 0U;
static bool     was_red_car     = false;

/* ---- (d) Private function prototypes (HVC uses a leading underscore). */
static void            _State_ReadInputs(State_Inputs_t *in);
static VCU_ErrorMask_t _State_CheckErrors(const State_Inputs_t *in);
static VCU_State_t     _State_Transition(VCU_State_t curr,
                                         VCU_ErrorMask_t errors,
                                         const State_Inputs_t *in);
static void            _State_OnTransition(VCU_State_t from, VCU_State_t to);
static void            _State_PackCanMessage(VCU_State_t state,
                                             VCU_ErrorMask_t errors,
                                             CAN_Message_t *msg);

/* ---- (e) Init: called from main.c before the scheduler starts. */
HAL_StatusTypeDef State_Manager_Init(void)
{
    return State_InitState();
}

/* ---- (f) The task: one loop = one state machine update. */
void StateManager_Task(void *argument)
{
    (void)argument;
    State_Inputs_t  in;
    VCU_ErrorMask_t errors;
    VCU_State_t     curr_state;
    VCU_State_t     next_state;
    CAN_Message_t   msg;
    uint32_t        next_wake = osKernelGetTickCount();

    for (;;) {
        State_GetState(&curr_state);              /* 1. where are we   */
        _State_ReadInputs(&in);                   /* 2. read the world */
        errors = _State_CheckErrors(&in);         /* 3. what is wrong  */
        State_SetErrorMask(errors);

        next_state = _State_Transition(curr_state, errors, &in);  /* 4 */
        if (next_state != curr_state) {
            _State_OnTransition(curr_state, next_state);          /* 5 */
            State_SetState(next_state);
        }

        /* 6. dead-car on red car rising edge, in ANY state (TREV4 l.60) */
        if (!was_red_car && in.red_car) {
            /* TODO send dead car message (Part 1, item 9) */
        }
        was_red_car = in.red_car;

        _State_PackCanMessage(next_state, errors, &msg);          /* 7 */
        (void)CAN_SendCTRLMsg(&msg);

        next_wake += STATE_REFRESH_PERIOD_MS;                     /* 8 */
        osDelayUntil(next_wake);
    }
}

/* ---- (g) Fill the inputs struct. THIS is where Part 1 plugs in. */
static void _State_ReadInputs(State_Inputs_t *in)
{
    uint32_t now = osKernelGetTickCount();
    uint16_t dbg = 0U;

    /* Safe defaults: if a source doesn't exist yet the car can't drive. */
    *in = (State_Inputs_t){0};

    /* TODO APPS / BSE / RTD / IMD+BMS latched from data/io.h (Part 1, 1-4) */

    in->hvc_seen    = hvc_seen;
    in->sdc_ok      = !hvc_sdc_open;                        /* Part 1, item 6 */
    in->red_car     = hvc_imd_fault || hvc_bms_fault;       /* Part 1, item 4 [?] */
    in->buzzer_done = (now - rtd_sound_start) >= RTD_SOUND_MS;  /* item 5 */

    (void)Config_ReadVariable(CONFIG_STATE_DEBUG_BITS, &dbg);   /* item 7 */
    in->ignore_rtd_brakes         = (dbg & DEBUG_BIT_IGNORE_RTD_BRAKES) != 0U;
    in->ignore_brake_plausibility = (dbg & DEBUG_BIT_IGNORE_BRAKE_PLAUSIBILITY) != 0U;
}

/* ---- (h) Turn inputs into error bits. Same as TREV4 lines 48-51. */
static VCU_ErrorMask_t _State_CheckErrors(const State_Inputs_t *in)
{
    VCU_ErrorMask_t errors = 0U;
    uint32_t now = osKernelGetTickCount();

    if (CHECK_APPS    && !in->apps_valid) SET_ERROR(errors, VCU_ERR_APPS_INVALID);
    if (CHECK_BSE     && !in->bse_valid)  SET_ERROR(errors, VCU_ERR_BSE_INVALID);
    if (CHECK_SDC     && !in->sdc_ok)     SET_ERROR(errors, VCU_ERR_SDC_OPEN);
    if (CHECK_RED_CAR &&  in->red_car)    SET_ERROR(errors, VCU_ERR_RED_CAR);
    if (CHECK_HVC_TIMEOUT && (now - hvc_last_rx_tick) > HVC_TIMEOUT_MS)
        SET_ERROR(errors, VCU_ERR_HVC_TIMEOUT);

    if (!in->ignore_brake_plausibility &&
        in->apps_above_bap && in->bse_hard_braking)
        SET_ERROR(errors, VCU_ERR_BAP);

    return errors;
}

/* ---- (i) The state machine. Ported from TREV4 lines 65-134.
 *          Pure logic: no HAL, no CAN, no RTOS calls -> easy to unit test. */
static VCU_State_t _State_Transition(VCU_State_t curr,
                                     VCU_ErrorMask_t errors,
                                     const State_Inputs_t *in)
{
    const bool hard_fault = (errors & VCU_HARD_FAULT_MASK) != 0U;
    const bool bap_fault  = (errors & ((VCU_ErrorMask_t)1U << VCU_ERR_BAP)) != 0U;
    const bool ready_to_drive = in->rtd_active &&
                                (in->ignore_rtd_brakes || in->brakes_engaged);

    switch (curr) {
    case VCU_STATE_LOADING:
        if (in->hvc_seen) return VCU_STATE_LAMP_CHECK;     /* Part 1, item 8 */
        break;
    case VCU_STATE_LAMP_CHECK:
        return VCU_STATE_NOT_READY;                         /* TODO lamp check */
    case VCU_STATE_NOT_READY:
        if (ready_to_drive && !hard_fault && !bap_fault)
            return VCU_STATE_PLAYING_RTD_SOUND;
        break;
    case VCU_STATE_PLAYING_RTD_SOUND:
        if (!in->rtd_active)  return VCU_STATE_NOT_READY;
        if (hard_fault)       return VCU_STATE_HARD_FAULT;
        if (in->buzzer_done)  return VCU_STATE_DRIVING;
        break;
    case VCU_STATE_DRIVING:
        if (!in->rtd_active)  return VCU_STATE_NOT_READY;
        if (hard_fault)       return VCU_STATE_HARD_FAULT;
        if (bap_fault)        return VCU_STATE_BAP_FAULT;
        break;
    case VCU_STATE_BAP_FAULT:
        if (!in->rtd_active)  return VCU_STATE_NOT_READY;
        if (hard_fault)       return VCU_STATE_HARD_FAULT;
        if (in->apps_below_bap_reestablish) return VCU_STATE_DRIVING;
        break;
    case VCU_STATE_HARD_FAULT:
        if (!in->rtd_active)  return VCU_STATE_NOT_READY;
        break;
    default:
        return VCU_STATE_HARD_FAULT;      /* corrupted value -> safest state */
    }
    return curr;                          /* no change */
}

/* ---- (j) One-time actions when the state changes. */
static void _State_OnTransition(VCU_State_t from, VCU_State_t to)
{
    if (to == VCU_STATE_PLAYING_RTD_SOUND) {
        rtd_sound_start = osKernelGetTickCount();   /* start buzzer timer */
        /* TODO send "clear faults" message (TREV4 clear_faults_tx_pending) */
    }
    if (from == VCU_STATE_DRIVING &&
        (to == VCU_STATE_HARD_FAULT || to == VCU_STATE_BAP_FAULT)) {
        /* TODO send "dead car" message (TREV4 dead_car_tx_pending) */
    }
}

/* ---- (k) Broadcast our state, HVC style: byte 0 = state, 1-4 = errors. */
static void _State_PackCanMessage(VCU_State_t state, VCU_ErrorMask_t errors,
                                  CAN_Message_t *msg)
{
    msg->id      = CAN_ID_VCU_STATE;   /* [?] pick an ID, add to data/can.h + DBC */
    msg->data[0] = (uint8_t)state;
    msg->data[1] = (uint8_t)(errors);
    msg->data[2] = (uint8_t)(errors >> 8);
    msg->data[3] = (uint8_t)(errors >> 16);
    msg->data[4] = (uint8_t)(errors >> 24);
    msg->length  = 5U;
}

/* ---- (l) CAN receive. Called for EVERY received message (both buses)
 *          by the CAN manager task. Return true only if it was ours. */
bool State_RxHandler(const CAN_Message_t *msg)
{
    if (msg->id == CAN_ID_HVC_IO_SUMMARY && msg->length >= 1U) {
        hvc_sdc_open     = (msg->data[0] & 0x01U) != 0U;   /* bit 0 */
        hvc_imd_fault    = (msg->data[0] & 0x02U) != 0U;   /* bit 1 */
        hvc_bms_fault    = (msg->data[0] & 0x04U) != 0U;   /* bit 2 */
        hvc_last_rx_tick = osKernelGetTickCount();
        hvc_seen         = true;
        return true;
    }
    return false;
}

Explanation, part by part:
 (a) State_Inputs_t: one snapshot of everything the logic looks at. TREV4
     read globals all over the function; putting them in one struct means
     the transition function can be tested by just filling in a struct.
 (b) HVC data: the CAN handler and the state task are different tasks, so
     the shared variables are volatile. The timestamp is what lets us
     detect HVC going silent.
 (c) Private bookkeeping: buzzer start time and the previous red-car value
     (for the rising-edge "dead car" message, same as TREV4's was_red_car).
 (d) Prototypes: static = private to this file, like HVC.
 (e) Init: only creates the state mutex. Called from main.c.
 (f) The task loop, in order:
       1. read the current state
       2. gather inputs
       3. compute the error mask and publish it
       4. decide the next state
       5. run one-time actions if the state changed, then publish it
       6. red car rising edge -> dead car message (any state, like TREV4)
       7. send our state on CAN every cycle
       8. sleep until the next period
     osDelayUntil (not osDelay like HVC/TREV4) keeps an exact period, so the
     buzzer and timeouts don't drift with how long the loop took.
 (g) _State_ReadInputs: the ONLY function that knows where data comes from.
     Starts with everything false = "can't drive", then fills in what
     exists. As APPS/BSE/IO get written, only this function changes.
 (h) _State_CheckErrors: HVC's structure (CHECK_ flags + SET_ERROR) with
     TREV4's conditions. Adds the HVC timeout check TREV4 didn't have.
 (i) _State_Transition: TREV4's switch, nearly line for line, with two
     changes: it RETURNS the new state instead of assigning a global (pure
     function, easy to test), and unknown states go to HARD_FAULT.
     HVC calls State_SetState inside each case; here the task does it once.
 (j) _State_OnTransition: things that should happen once on a change,
     not every loop. Replaces TREV4's *_tx_pending flags: we just send
     the message right here.
 (k) _State_PackCanMessage: same format idea as HVC's state message, so
     the dash/DAQ can show state + reasons.
 (l) State_RxHandler: the VCU's version of TREV4's CAN_RX_GetHVCSummaryData.
     Note the existing Config_RxHandler only reacts to IDs with 0xC0 in
     bits 16-23, and 0x004001F0 has 0x40 there, so they don't clash.

===========================================================================
2.5  WIRING (small edits outside the new files)
===========================================================================
 1. Core/Src/main.c, inside "USER CODE BEGIN RTOS_THREADS" (so CubeMX
    keeps it), after Config_Manager_Init():
        if (State_Manager_Init() != HAL_OK) { Error_Handler(); }
 2. Core/Inc/managers/can_manager.h: #include "managers/state_manager.h"
    and add State_RxHandler to CAN_RxHandlers[] (line 17).
 3. Core/Inc/data/can.h: add
        #define CAN_ID_HVC_IO_SUMMARY  0x004001F0U
        #define CAN_ID_HVC_STATE       0x004001F1U
        #define CAN_ID_VCU_STATE       ...          [?]
 4. Config: CONFIG_STATE_DEBUG_BITS in config.h, config.c, config.json.
 5. VCU-Firmware.ioc: raise StateManager priority from Low to Normal in
    CubeMX (editing main.c directly gets overwritten on regenerate).
 6. Build: make sure data/state.c and managers/state_manager.c are in the
    build. flashing/STM32Make.make (trevware) looks like the real one.
 7. Tests (see testing/AGENTS.md):
      testing/tests/managers/test_state_manager.c  + test_state_manager.mk
        MOCKS_managers/state_manager := cmsis_os config can state
      testing/support/cmsis_os.h needs: osKernelGetTickCount, osDelayUntil,
        osMutexNew/Acquire/Release, osMutexId_t, osMutexAttr_t, osWaitForever
      test_can_manager.mk and data/test_can.mk: add state_manager to MOCKS
        (because can_manager.h's handler table will reference it).


###########################################################################
OPEN QUESTIONS
###########################################################################
 1. Are APPS and BSE read by the VCU ADC or by another board over CAN?
 2. What exactly was "red car" in TREV4 lights.c? (IMD/BMS fault?)
 3. RTD switch and latched inputs: active low or active high?
 4. CAN IDs + receivers for "clear faults", "dead car", and VCU state.
 5. Which bus is HVC on: CTRL or DAQ? (Handler gets both, so it works
    either way, but it matters for wiring and for where we send.)
 6. State period: 10 ms OK? RTD buzzer length?
 7. Keep TREV4's states as-is, or add a state for "tractive system not
    live yet" now that HVC reports its own state?
