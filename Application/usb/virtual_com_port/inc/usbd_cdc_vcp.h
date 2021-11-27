/**
  ******************************************************************************
  * @file    usbd_cdc_vcp.h
  * @author  MCD Application Team
  * @version V1.2.1
  * @date    17-March-2018
  * @brief   Header for usbd_cdc_vcp.c file.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      <http://www.st.com/SLA0044>
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USBD_CDC_VCP_H
#define __USBD_CDC_VCP_H

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_core.h"
#include "usbd_conf.h"


/* Exported typef ------------------------------------------------------------*/
/* The following structures groups all needed parameters to be configured for the 
   ComPort. These parameters can modified on the fly by the host through CDC class
   command class requests. */
typedef struct
{
  uint32_t bitrate;
  uint8_t  format;
  uint8_t  paritytype;
  uint8_t  datatype;
}LINE_CODING;

/* Exported constants --------------------------------------------------------*/
/* The following define is used to route the USART IRQ handler to be used.
   The IRQ handler function is implemented in the usbd_cdc_vcp.c file. */
          
#ifdef USE_STM3210C_EVAL
 #define EVAL_COM_IRQHandler            USART2_IRQHandler
#elif defined(USE_STM324x9I_EVAL)
 #define EVAL_COM_IRQHandler            USART1_IRQHandler 
#else
 #define EVAL_COM_IRQHandler            USART3_IRQHandler 
#endif /* USE_STM322xG_EVAL */


#define DEFAULT_CONFIG                  0
#define OTHER_CONFIG                    1

/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
void EVAL_COM_IRQHandler(void);

#endif /* __USBD_CDC_VCP_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
