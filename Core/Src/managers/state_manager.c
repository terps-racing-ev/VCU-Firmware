#include "managers/state_manager.h"

#include <stdatomic.h>

#include "data/config.h"
#include "main.h"

_Static_assert(LAMP_CHECK_DURATION_MS >= 2000U && LAMP_CHECK_DURATION_MS <= 6000U,
               "Lamp check must last 2-6 s (test plan, state LAMP_CHECK)");

/* HVC_IO_Summary byte 0 */
#define HVC_IO_SDC_OPEN_BIT   0x01U   /* 1 = open = NOT ok */
#define HVC_IO_IMD_FAULT_BIT  0x02U
#define HVC_IO_BMS_FAULT_BIT  0x04U


static atomic_bool clear_faults_tx_pending = false;
static atomic_bool dead_car_tx_pending     = false;

/* ---- (b) Private bookkeeping, only touched by the state task. */
static bool     was_red_car      = false;   /* for the rising-edge detect */
static uint32_t rtd_sound_start  = 0U;      /* tick when PLAYING_RTD_SOUND began */
static uint32_t lamp_check_start = 0U;      /* tick when LAMP_CHECK began */


static volatile uint8_t  hvc_io_bits      = HVC_IO_SDC_OPEN_BIT; /* assume worst */
static volatile uint32_t hvc_last_rx_tick = 0U;
static volatile bool     hvc_seen         = false;

static void            _State_ReadInputs(State_Inputs_t *in);
static VCU_ErrorMask_t _State_CheckErrors(const State_Inputs_t *in);
static VCU_State_t     _State_Transition(VCU_State_t curr,
                                         VCU_ErrorMask_t errors,
                                         const State_Inputs_t *in);
static void            _State_OnTransition(VCU_State_t from, VCU_State_t to);

HAL_StatusTypeDef State_Manager_Init(void)
{
    HAL_StatusTypeDef status = State_InitState();   /* state = LAMP_CHECK */
    if (status != HAL_OK) {
        return status;
    }

    atomic_store(&clear_faults_tx_pending, false);
    atomic_store(&dead_car_tx_pending, false);
    was_red_car      = false;
    rtd_sound_start  = 0U;
    lamp_check_start = 0U;

    hvc_io_bits      = HVC_IO_SDC_OPEN_BIT;
    hvc_last_rx_tick = 0U;
    hvc_seen         = false;
    return HAL_OK;
}

// one loop is one state machine update
void StateManager_Task(void *argument)
{
    (void)argument;
    State_Inputs_t in;
    uint32_t next_wake = osKernelGetTickCount();

    lamp_check_start = next_wake;           /* lamp check starts when we do */

    for (;;) {
        _State_ReadInputs(&in);             /* 1. read the world            */
        State_Manager_Step(&in);            /* 2. run the state machine     */

        /* TODO 3. broadcast VCU state + error mask on CAN every cycle.
         *         Needs a CAN ID (open question) - see plan Part 2, (k). */

        next_wake += STATE_REFRESH_PERIOD_MS;
        osDelayUntil(next_wake);            /* fixed period, no drift       */
    }
}


void State_Manager_Step(const State_Inputs_t *in)
{
    VCU_State_t     curr;
    VCU_State_t     next;
    VCU_ErrorMask_t errors;

    State_GetState(&curr);
    errors = _State_CheckErrors(in);
    State_SetErrorMask(errors);

    /* Red car turning ON sends "dead car" in ANY state (TREV4 behaviour). */
    if (!was_red_car && in->red_car) {
        atomic_store(&dead_car_tx_pending, true);
    }
    was_red_car = in->red_car;

    next = _State_Transition(curr, errors, in);
    if (next != curr) {
        _State_OnTransition(curr, next);
        State_SetState(next);
    }
}


static void _State_ReadInputs(State_Inputs_t *in)
{
    uint16_t dbg = 0U;


    *in = (State_Inputs_t){ 0 };


    /* Lamp check / buzzer timers (so the transition function needs no clock) */
    uint32_t last = hvc_last_rx_tick;       /* read BEFORE 'now', see below */
    uint32_t now  = osKernelGetTickCount();

    in->lamp_check_done = (now - lamp_check_start) >= LAMP_CHECK_DURATION_MS;
    in->buzzer_done     = (now - rtd_sound_start)  >= RTD_SOUND_MS;

// hvc data
    {
        const uint8_t bits = hvc_io_bits;
        in->sdc_ok      = (bits & HVC_IO_SDC_OPEN_BIT) == 0U;
        in->red_car     = (bits & (HVC_IO_IMD_FAULT_BIT | HVC_IO_BMS_FAULT_BIT)) != 0U; /* [?] */
        in->hvc_timeout = !hvc_seen || ((now - last) > HVC_TIMEOUT_MS);
    }

    /* Debug bits. If the read fails dbg stays 0 = nothing ignored (safe). */
    (void)Config_ReadVariable(CONFIG_STATE_DEBUG_BITS, &dbg);
    in->ignore_rtd_brakes         = (dbg & DEBUG_BIT_IGNORE_RTD_BRAKES) != 0U;
    in->ignore_brake_plausibility = (dbg & DEBUG_BIT_IGNORE_BRAKE_PLAUSIBILITY) != 0U;
}

/* =======================================================================
 * Inputs -> error bits. TREV4's hard_fault / bap_fault conditions.
 * ===================================================================== */
static VCU_ErrorMask_t _State_CheckErrors(const State_Inputs_t *in)
{
    VCU_ErrorMask_t errors = 0U;

    if (CHECK_APPS        && !in->apps_valid)  { SET_ERROR(errors, VCU_ERR_APPS_INVALID); }
    if (CHECK_BSE         && !in->bse_valid)   { SET_ERROR(errors, VCU_ERR_BSE_INVALID);  }
    if (CHECK_SDC         && !in->sdc_ok)      { SET_ERROR(errors, VCU_ERR_SDC_OPEN);     }
    if (CHECK_RED_CAR     &&  in->red_car)     { SET_ERROR(errors, VCU_ERR_RED_CAR);      }
    if (CHECK_HVC_TIMEOUT &&  in->hvc_timeout) { SET_ERROR(errors, VCU_ERR_HVC_TIMEOUT); }

    if (!in->ignore_brake_plausibility &&
        in->apps_above_bap && in->bse_hard_braking) {
        SET_ERROR(errors, VCU_ERR_BAP);
    }
    return errors;
}

/* =======================================================================
 * The state machine. TREV4's switch, line for line, plus LAMP_CHECK.
 * Pure: returns the next state, changes nothing, calls nothing.
 * ===================================================================== */
static VCU_State_t _State_Transition(VCU_State_t curr,
                                     VCU_ErrorMask_t errors,
                                     const State_Inputs_t *in)
{
    const bool hard_fault = (errors & VCU_HARD_FAULT_MASK) != 0U;
    const bool bap_fault  = (errors & VCU_BAP_MASK) != 0U;
    const bool ready_to_drive = in->rtd_active &&
                                (in->ignore_rtd_brakes || in->brakes_engaged);

    switch (curr) {

    case VCU_STATE_LAMP_CHECK:
        /* Not gated by hard/BAP faults: lamps run their full time no matter
         * what. Faults still show in the error mask and block NOT_READY->RTD. */
        if (in->lamp_check_done) { return VCU_STATE_NOT_READY; }
        break;

    case VCU_STATE_NOT_READY:
        if (ready_to_drive && !hard_fault && !bap_fault) {
            return VCU_STATE_PLAYING_RTD_SOUND;
        }
        break;

    case VCU_STATE_PLAYING_RTD_SOUND:
        if (!in->rtd_active)  { return VCU_STATE_NOT_READY;  }
        if (hard_fault)       { return VCU_STATE_HARD_FAULT; }
        if (in->buzzer_done)  { return VCU_STATE_DRIVING;    }
        break;

    case VCU_STATE_DRIVING:
        if (!in->rtd_active)  { return VCU_STATE_NOT_READY;  }
        if (hard_fault)       { return VCU_STATE_HARD_FAULT; }
        if (bap_fault)        { return VCU_STATE_BAP_FAULT;  }
        break;

    case VCU_STATE_BAP_FAULT:
        if (!in->rtd_active)  { return VCU_STATE_NOT_READY;  }
        if (hard_fault)       { return VCU_STATE_HARD_FAULT; }
        /* Hysteresis: recover below the 5% threshold, NOT merely when
         * bap_fault is false (which would recover at <25%). */
        if (in->apps_below_bap_reestablish) { return VCU_STATE_DRIVING; }
        break;

    case VCU_STATE_HARD_FAULT:
        /* LATCHED: only leaves when RTD is switched off, even if the fault
         * has already cleared. NOT_READY then re-checks faults on entry. */
        if (!in->rtd_active)  { return VCU_STATE_NOT_READY; }
        break;

    default:
        return VCU_STATE_HARD_FAULT;      /* corrupted/unknown value -> safest */
    }
    return curr;                          /* no change */
}

/* =======================================================================
 * One-time actions on a state CHANGE (not every loop).
 * Replaces the scattered *_tx_pending assignments in TREV4's switch.
 * ===================================================================== */
static void _State_OnTransition(VCU_State_t from, VCU_State_t to)
{
    if (to == VCU_STATE_LAMP_CHECK) {
        lamp_check_start = osKernelGetTickCount();
    }
    if (to == VCU_STATE_PLAYING_RTD_SOUND) {
        rtd_sound_start = osKernelGetTickCount();   /* IO manager turns the buzzer on */
        atomic_store(&clear_faults_tx_pending, true);
    }
    /* Dead car only when a fault KILLS an active drive (TREV4 behaviour). */
    if (from == VCU_STATE_DRIVING &&
        (to == VCU_STATE_HARD_FAULT || to == VCU_STATE_BAP_FAULT)) {
        atomic_store(&dead_car_tx_pending, true);
    }
}

/* =======================================================================
 * CAN receive. Called for EVERY received message by the CAN manager task.
 * ===================================================================== */
bool State_RxHandler(const CAN_Message_t *msg)
{
    if (msg->id == CAN_ID_HVC_IO_SUMMARY && msg->length >= 1U) {
        hvc_io_bits      = msg->data[0];
        hvc_last_rx_tick = osKernelGetTickCount();
        hvc_seen         = true;               /* last, so data is valid first */
        return true;
    }
    return false;
}

/* =======================================================================
 * Request flags: true once per event, then false until the next one.
 * atomic_exchange = read and clear in one uninterruptible step.
 * ===================================================================== */
bool State_Manager_ClearFaultsTxTrigger(void)
{
    return atomic_exchange(&clear_faults_tx_pending, false);
}

bool State_Manager_DeadCarTxTrigger(void)
{
    return atomic_exchange(&dead_car_tx_pending, false);
}