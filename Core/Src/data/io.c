#include "data/io.h"

EFuse batt_fans_L_efuse = {0};
EFuse batt_fans_R_efuse = {0};
EFuse rad_fans_efuse = {0};
EFuse pump_efuse = {0};
EFuse drs_efuse = {0};

//Add things like these for each i/o so it aligns with a struct \/
AnalogIO lv_current_raw = {0};
Current lv_current = {0};
AnalogIO hv_current_raw = {0};
Current hv_current = {0};
//DigitalIO bms_fault = {0};
//AnalogIO cs_low_raw = {0};

HAL_StatusTypeDef IO_InitDigitalIO(DigitalIO* dio, const char* mutex_name) {
    if (dio == NULL || mutex_name == NULL) {
        return HAL_ERROR;
    }

    const osMutexAttr_t dio_mutex_attr = {
        .name = mutex_name
    };

    dio->mutex = osMutexNew(&dio_mutex_attr);
    if (dio->mutex == NULL) {
        return HAL_ERROR;
    }

    dio-> value = 0;
    dio->last_updated = 0;

    return HAL_OK;
}
HAL_StatusTypeDef IO_InitAnalogIO(AnalogIO* aio, const char* mutex_name) {
    if (aio == NULL || mutex_name == NULL) {
        return HAL_ERROR;
    }

    const osMutexAttr_t aio_mutex_attr = {
        .name = mutex_name
    };

    aio->mutex = osMutexNew(&aio_mutex_attr);
    if (aio->mutex == NULL) {
        return HAL_ERROR;
    }

    aio-> value = 0;
    aio->last_updated = 0;

    return HAL_OK;
}

HAL_StatusTypeDef IO_InitEFuse(
    EFuse *efuse,
    const char *mutex_name,
    GPIO_TypeDef *in_port,
    uint16_t in_pin,
    GPIO_TypeDef *den_port,
    uint16_t den_pin,
    ADC_HandleTypeDef *adc,
    uint32_t is_channel)
{
    if (efuse == NULL ||
        mutex_name == NULL ||
        in_port == NULL ||
        adc == NULL ||
        ((den_port == NULL) != (den_pin == 0U))) {
        return HAL_ERROR;
    }

    const osMutexAttr_t mutex_attr = {
        .name = mutex_name
    };

    efuse->mutex = osMutexNew(&mutex_attr);
    if (efuse->mutex == NULL) {
        return HAL_ERROR;
    }

    efuse->in_port = in_port;
    efuse->in_pin = in_pin;

    efuse->den_port = den_port;
    efuse->den_pin = den_pin;

    efuse->adc = adc;
    efuse->is_channel = is_channel;

    efuse->command = false;
    efuse->output_enabled = false;
    efuse->diagnostic_enabled = false;

    efuse->current_raw = 0U;
    efuse->current_ma = 0;

    efuse->status = EFUSE_STATUS_OFF;
    efuse->last_updated = 0U;

    return HAL_OK;
}

HAL_StatusTypeDef IO_SetEFuseCommand(EFuse *efuse, bool command)
{
    if (efuse == NULL || efuse->mutex == NULL || efuse->in_port == NULL) {
        return HAL_ERROR;
    }

    if (osMutexAcquire(efuse->mutex, osWaitForever) != osOK) {
        return HAL_ERROR;
    }

    HAL_GPIO_WritePin(
        efuse->in_port,
        efuse->in_pin,
        command ? GPIO_PIN_SET : GPIO_PIN_RESET);

    efuse->command = command;
    efuse->output_enabled = command;
    efuse->status = command ? EFUSE_STATUS_ON : EFUSE_STATUS_OFF;
    efuse->last_updated = HAL_GetTick();

    (void)osMutexRelease(efuse->mutex);
    return HAL_OK;
}
