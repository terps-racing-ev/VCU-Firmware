#include "data/config.h"

const Config_Value_t Config_Values[] = {
    // ID                            Default   Min  Max
    {CONFIG_DEFAULT_ADDR,               0,      0,    0},
    {CONFIG_CTRL_CAN_MANAGER_DELAY_MS,  10,     0,  255},
    {CONFIG_DAQ_CAN_MANAGER_DELAY_MS,   10,     0,  255},
    {CONFIG_CONFIG_MANAGER_DELAY_MS,    10,     0,  255},

};
const uint8_t Config_Values_Size = (uint8_t)(sizeof(Config_Values)/sizeof(Config_Value_t));

EEPROM_StatusTypeDef Config_ReadVariable(uint8_t virtualAddress, uint16_t *value) 
{
    if (EEPROM_ReadVariable(virtualAddress, value) == EEPROM_OK) 
    {
        return EEPROM_OK;
    }

    for (int i = 0; i < Config_Values_Size; i++) {
        if (Config_Values[i].virtual_address == virtualAddress) {
            *value = Config_Values[i].default_value;
            return EEPROM_OK;
        }
    }

    return EEPROM_NOT_FOUND;
}

EEPROM_StatusTypeDef Config_WriteVariable(uint8_t virtualAddress, uint16_t value) {

    for (int i = 0; i < Config_Values_Size; i++) {
        if (Config_Values[i].virtual_address == virtualAddress) {
            if (Config_Values[i].min_value > value || Config_Values[i].max_value < value) {
                return EEPROM_INVALID_ARGUMENT;
            } else {
                return EEPROM_WriteVariable(virtualAddress, value);
            }
        }
    }

    return EEPROM_NOT_FOUND;
}
