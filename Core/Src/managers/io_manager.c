#include "cmsis_os.h"
#include "managers/io_manager.h"
#include "main.h"
#include "io_manager.h"

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
//Get io.h and io manager reading all the signals in
//Can look at HVC firmware, io.h houses all the structs