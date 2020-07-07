/******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2016 STMicroelectronics</center></h2>
  *
  * Licensed under ST MYLIBERTY SOFTWARE LICENSE AGREEMENT (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/myliberty
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
  * AND SPECIFICALLY DISCLAIMING THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
******************************************************************************/
/*
 *      PROJECT:   ST25R3911 firmware
 *      $Revision: $
 *      LANGUAGE:  ANSI C
 */

/*! \file
 *
 *  \author
 *
 *  \brief Implementation for controlling shield LEDs functionality 
 *
 */
 
/*!
 * 
 */
 
/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include "bsp.h"
#include "led.h"

volatile uint16_t msLedA = 0;
volatile uint16_t msLedB = 0;
volatile uint16_t msLedF = 0;
volatile uint16_t msLedV = 0;
volatile uint16_t msLedAP2P = 0;

void ledOn(st25R3911Led_t Led)
{
#ifdef _NUCLEO_
  switch(Led){
    case LED_A:     HAL_GPIO_WritePin(LED_A_GPIO_Port     , LED_A_Pin     , GPIO_PIN_SET); break;
    case LED_B:     HAL_GPIO_WritePin(LED_B_GPIO_Port     , LED_B_Pin     , GPIO_PIN_SET); break;
    case LED_F:     HAL_GPIO_WritePin(LED_F_GPIO_Port     , LED_F_Pin     , GPIO_PIN_SET); break;
    case LED_V:     HAL_GPIO_WritePin(LED_V_GPIO_Port     , LED_V_Pin     , GPIO_PIN_SET); break;
    case LED_AP2P:  HAL_GPIO_WritePin(LED_AP2P_GPIO_Port  , LED_AP2P_Pin  , GPIO_PIN_SET); break;
    default:
      break;
  }
#endif
}

void ledOff(st25R3911Led_t Led)
{
#ifdef _NUCLEO_
  switch(Led){
    case LED_A:     HAL_GPIO_WritePin(LED_A_GPIO_Port     , LED_A_Pin     , GPIO_PIN_RESET); break;
    case LED_B:     HAL_GPIO_WritePin(LED_B_GPIO_Port     , LED_B_Pin     , GPIO_PIN_RESET); break;
    case LED_F:     HAL_GPIO_WritePin(LED_F_GPIO_Port     , LED_F_Pin     , GPIO_PIN_RESET); break;
    case LED_V:     HAL_GPIO_WritePin(LED_V_GPIO_Port     , LED_V_Pin     , GPIO_PIN_RESET); break;
    case LED_AP2P:  HAL_GPIO_WritePin(LED_AP2P_GPIO_Port  , LED_AP2P_Pin  , GPIO_PIN_RESET); break;
    default:
      break;
  }
#endif
}

void ledToggle(st25R3911Led_t Led)
{
#ifdef _NUCLEO_
  switch(Led){
    case LED_A:     HAL_GPIO_TogglePin(LED_A_GPIO_Port     , LED_A_Pin     ); break;
    case LED_B:     HAL_GPIO_TogglePin(LED_B_GPIO_Port     , LED_B_Pin     ); break;
    case LED_F:     HAL_GPIO_TogglePin(LED_F_GPIO_Port     , LED_F_Pin     ); break;
    case LED_V:     HAL_GPIO_TogglePin(LED_V_GPIO_Port     , LED_V_Pin     ); break;
    case LED_AP2P:  HAL_GPIO_TogglePin(LED_AP2P_GPIO_Port  , LED_AP2P_Pin  ); break;
    default:
      break;
  }
#endif
}



void ledOnOff(st25R3911Led_t Led, uint32_t delay)
{
  ledOn(Led);

  switch(Led){
    case LED_A:     msLedA = delay; break;
    case LED_B:     msLedB = delay; break;
    case LED_F:     msLedF = delay; break;
    case LED_V:     msLedV = delay; break;
    case LED_AP2P:  msLedAP2P = delay; break;
    default:
      break;
  }  
}
void ledFeedbackHandler()
{
  if(msLedA > 0){
    msLedA--;
    if(msLedA == 0)
      ledOff(LED_A);
  }
  
  if(msLedB > 0){
    msLedB--;
    if(msLedB == 0)
      ledOff(LED_B);
  }   
  
  if(msLedF > 0){
    msLedF--;
    if(msLedF == 0)
      ledOff(LED_F);
  }  
  
  if(msLedV > 0){
    msLedV--;
    if(msLedV == 0)
      ledOff(LED_V);
  }    
  
  if(msLedAP2P > 0){
    msLedAP2P--;
    if(msLedAP2P == 0)
      ledOff(LED_AP2P);
  }  
}
