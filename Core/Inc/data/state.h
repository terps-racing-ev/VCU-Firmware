typedef enum VCU_state{
    VCU_STATE_LAMP_CHECK = 0,
    VCU_STATE_NOT_READY = 1,
    VCU_STATE_RTD_SFX_PLAYING = 2,
    VCU_STATE_DRIVING = 3,
    VCU_STATE_BAP_FAULT = 4,
    VCU_STATE_HARD_FAULT = 5
} VCU_state;

//TODO: error bit mask, function types