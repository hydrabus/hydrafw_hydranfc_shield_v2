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
 *  \brief Module for controlling shield LEDs
 *
 */
/*!
 * 
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef LED_H
#define LED_H

/* Includes ------------------------------------------------------------------*/
#include "stdint.h"

/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/
extern volatile uint16_t msLedA;
extern volatile uint16_t msLedB;
extern volatile uint16_t msLedF;
extern volatile uint16_t msLedV;
extern volatile uint16_t msLedAP2P;


typedef enum
{
    LED_A       = 0x1,
    LED_B       = 0x2,
    LED_F       = 0x3,
    LED_V       = 0x4,
    LED_AP2P    = 0x5,
}st25R3911Led_t;

/*
******************************************************************************
* GLOBAL MACROS
******************************************************************************
*/

void ledOn(st25R3911Led_t Led);
void ledOff(st25R3911Led_t Led);
void ledToggle(st25R3911Led_t Led);

#define VISUAL_FEEDBACK_DELAY   600
void ledOnOff(st25R3911Led_t Led, uint32_t delay);
void ledFeedbackHandler(void);

#endif /* LED_H */

