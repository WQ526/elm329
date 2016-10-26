/**
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2009-2016 ObdDiag.Net. All rights reserved.
 *
 */

#ifndef __ADAPTER_DEFS_H__ 
#define __ADAPTER_DEFS_H__

#ifdef  __CC_ARM
  #define unique_ptr auto_ptr
  #define nullptr    NULL
#elif  __GNUC__ 
#endif

#ifdef BLUETOOTH_MINI_CFG
  const int UART_SPEED     =  38400;
  const int TX_LED_PORT    =  1;
  const int RX_LED_PORT    =  1;
  const int POWER_LED_PORT =  1;
  const int TX_LED_NUM     =  3;
  const int RX_LED_NUM     =  4;
  const int POWER_LED_NUM  =  5;
#else
  const int UART_SPEED     =  115200;
  const int TX_LED_PORT    =  0;
  const int RX_LED_PORT    =  0;
  const int TX_LED_NUM     =  7;
  const int RX_LED_NUM     =  6;
#endif
  
#endif //__ADAPTER_DEFS_H__
