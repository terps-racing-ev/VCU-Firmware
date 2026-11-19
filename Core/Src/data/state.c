#include "data/state.h"



void State_GetState(VCU_state *state) {
    if (state == NULL)
        return;
        
    *state = VCU_Info.state;

}

void State_SetState(VCU_state inserted) {
    if (inserted == NULL)
        return;

    VCU_Info.state = inserted;
}

void State_GetErrorMask(ErrorBits *mask) {
    
}