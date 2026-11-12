#ifndef IO_MANAGER_H
#define IO_MANAGER_H
#include "stm32f4xx_hal_conf.h"
#include "main.h"
#include "io.h"

int IO_Manager_Init(void);
void IO_Manager_Task(void *argument);

#endif /* IO_MANAGER_H */