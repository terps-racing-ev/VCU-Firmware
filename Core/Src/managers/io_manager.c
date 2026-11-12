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
//Firmware tutorial 11:48 seems to start how to read signals in
//check what need efuse
//Figure out pwm for drs, lv controller
//Start w/ analog and digital structs (done)
//Io defines values, managers define who sets values and gets them