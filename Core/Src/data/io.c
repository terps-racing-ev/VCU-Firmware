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