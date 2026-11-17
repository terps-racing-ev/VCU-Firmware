#include "io.h"

//Add things like these for each i/o so it aligns with a struct \/
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
        den_port == NULL ||
        adc == NULL) {
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
