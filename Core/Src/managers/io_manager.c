#include "cmsis_os.h"
#include "managers/io_manager.h"
#include "main.h"

int IO_Manager_Init(void)
{
  return HAL_OK;
}

void IO_Manager_Task(void *argument)
{
  (void)argument;

  for (;;)
  {
    osDelay(1U);
  }
}
