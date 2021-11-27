#include "stm32f4xx.h"

#include "usb_core.h"
#include "usbd_core.h"
#include "usbd_audio_core.h"

extern uint32_t USBD_OTG_ISR_Handler (USB_OTG_CORE_HANDLE *pdev);
extern void TimingDelay_Decrement(void);

extern USB_OTG_CORE_HANDLE USB_OTG_dev;

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  TimingDelay_Decrement();
}


/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

#ifdef USE_USB_OTG_FS
void OTG_FS_WKUP_IRQHandler(void)
{
  if(USB_OTG_dev.cfg.low_power)
  {
    *(uint32_t *)(0xE000ED10) &= 0xFFFFFFF9 ;
    SystemInit();
    USB_OTG_UngateClock(&USB_OTG_dev);
  }
  EXTI_ClearITPendingBit(EXTI_Line18);
}
#endif

/**
  * @brief  This function handles EXTI15_10_IRQ Handler.
  * @param  None
  * @retval None
  */
#ifdef USE_USB_OTG_HS
void OTG_HS_WKUP_IRQHandler(void)
{
  if(USB_OTG_dev.cfg.low_power)
  {
    *(uint32_t *)(0xE000ED10) &= 0xFFFFFFF9 ;
    SystemInit();
    USB_OTG_UngateClock(&USB_OTG_dev);
  }
  EXTI_ClearITPendingBit(EXTI_Line20);
}
#endif

/**
  * @brief  This function handles OTG_HS Handler.
  * @param  None
  * @retval None
  */
#ifdef USE_USB_OTG_HS
void OTG_HS_IRQHandler(void)
#else
void OTG_FS_IRQHandler(void)
#endif
{
  USBD_OTG_ISR_Handler(&USB_OTG_dev);
}

#ifdef USB_OTG_HS_DEDICATED_EP1_ENABLED
/**
  * @brief  This function handles EP1_IN Handler.
  * @param  None
  * @retval None
  */
void OTG_HS_EP1_IN_IRQHandler(void)
{
  USBD_OTG_EP1IN_ISR_Handler(&USB_OTG_dev);
}

/**
  * @brief  This function handles EP1_OUT Handler.
  * @param  None
  * @retval None
  */
void OTG_HS_EP1_OUT_IRQHandler(void)
{
  USBD_OTG_EP1OUT_ISR_Handler(&USB_OTG_dev);
}
#endif

/**
  * @brief  This function handles External line 1 interrupt request.
  * @param  None
  * @retval None
  */
void EXTI1_IRQHandler(void)
{
}

void DMA1_Stream5_IRQHandler(void)
{
  if(DMA_GetITStatus(DMA1_Stream5, DMA_IT_HTIF5))
  {
          DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_HTIF5);
  }
  else if (DMA_GetITStatus(DMA1_Stream5, DMA_IT_TCIF5))
  {
          DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_TCIF5);
  }
}

void DMA1_Stream3_IRQHandler(void)
{
  if(DMA_GetITStatus(DMA1_Stream3, DMA_IT_HTIF3))
  {
          DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_HTIF3);
  }
  else if (DMA_GetITStatus(DMA1_Stream3, DMA_IT_TCIF3))
  {
          DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_TCIF3);
  }
}

void TIM8_TRG_COM_TIM14_IRQHandler(void)
{
  if (TIM_GetITStatus(TIM14, TIM_IT_CC1) != RESET)
  {
          TIM_ClearITPendingBit(TIM14, TIM_IT_CC1);
  }
}

void TIM8_UP_TIM13_IRQHandler(void)
{
  if (TIM_GetITStatus(TIM13, TIM_IT_CC1) != RESET)
  {
          TIM_ClearITPendingBit(TIM13, TIM_IT_CC1);
  }
}

/**
  * @}
  */ 
  
/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
