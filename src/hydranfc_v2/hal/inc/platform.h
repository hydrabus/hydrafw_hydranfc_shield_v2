/*
 * HydraBus/HydraNFC v2
 *
 * Copyright (C) 2020 Benjamin VERNOUX
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

/* Platform definitions for ST25RFAL002 V2.2.0 / 22-May-2020 */

/*
******************************************************************************
* INCLUDES
******************************************************************************
*/
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>

// hydrafw\src\drv\stm32cube\bsp.h
#include "bsp.h"
#include "bsp_spi.h"
#include "hydranfc_v2_common.h"

#include "st_errno.h"

#include "spi.h"
#include "timer.h"

void DelayMs(uint32_t delay_ms);
void rfalPreTransceiveCb(void);

/* USER CODE END Private defines */

#ifdef __cplusplus
 extern "C" {
#endif
void _Error_Handler(char *, int);

#define Error_Handler() _Error_Handler(__FILE__, __LINE__)
#ifdef __cplusplus
}
#endif

/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/
#define ST25R391X_SPI_DEVICE	BSP_DEV_SPI2

#define ST25R_COM_SINGLETXRX /*!< Use single Transceive */
#define ST25R_SS_PIN            GPIO_PIN_1 /*!< GPIO pin used for ST25R SPI SS */ 
#define ST25R_SS_PORT           GPIOC /*!< GPIO port used for ST25R SPI SS port */                
#define ST25R_INT_PIN           GPIO_PIN_1 /*!< GPIO pin used for ST25R External Interrupt */
#define ST25R_INT_PORT          GPIOA /*!< GPIO port used for ST25R External Interrupt */

#ifdef LED_FIELD_Pin            
#define PLATFORM_LED_FIELD_PIN  LED_FIELD_Pin /*!< GPIO pin used as field LED */
#endif                          

#ifdef LED_FIELD_GPIO_Port      
#define PLATFORM_LED_FIELD_PORT LED_FIELD_GPIO_Port /*!< GPIO port used as field LED */
#endif                          

#ifdef LED_RX_Pin               
#define PLATFORM_LED_RX_PIN     LED_RX_Pin /*!< GPIO pin used as field LED */
#endif                          

#ifdef LED_RX_GPIO_Port         
#define PLATFORM_LED_RX_PORT    LED_RX_GPIO_Port /*!< GPIO port used as field LED */
#endif

/*
******************************************************************************
* GLOBAL MACROS
******************************************************************************
*/
#define HAL_Delay(Delay) ( DelayMs(Delay) )
#define HAL_GetTickMs() ( (HAL_GetTick()/10) ) // On HydraBus HAL_GetTick returns common/chconf.h/CH_CFG_ST_FREQUENCY set by default to 10000 for accurate ticks
#define HAL_GetTick() ( HAL_GetTickMs() )
#define _Error_Handler(__FILE__, __LINE__) // Ignore errors

/* Protect ST25RComm from EXTI1_IRQ used with PA1 & ST25R3916 IRQ to avoid ST25R3916 IRQ to preempt spi transfer in user code */
#define platformProtectST25RComm()         do{ globalCommProtectCnt++; __DSB();NVIC_DisableIRQ(EXTI1_IRQn);__DSB();__ISB();}while(0) /*!< Protect unique access to ST25R391x communication channel - IRQ disable on single thread environment (MCU) ; Mutex lock on a multi thread environment      */
#define platformUnprotectST25RComm()       do{ if (--globalCommProtectCnt==0) {NVIC_EnableIRQ(EXTI1_IRQn);} }while(0)                /*!< Unprotect unique access to ST25R391x communication channel - IRQ enable on a single thread environment (MCU) ; Mutex unlock on a multi thread environment */

#define platformProtectST25RIrqStatus()    platformProtectST25RComm()                       /*!< Protect unique access to IRQ status var - IRQ disable on single thread environment (MCU) ; Mutex lock on a multi thread environment */
#define platformUnprotectST25RIrqStatus()  platformUnprotectST25RComm()                     /*!< Unprotect the IRQ status var - IRQ enable on a single thread environment (MCU) ; Mutex unlock on a multi thread environment         */


#define platformLedOff( port, pin )        platformGpioClear((port), (pin))                 /*!< Turns the given LED Off */
#define platformLedOn( port, pin )         platformGpioSet((port), (pin))                   /*!< Turns the given LED On */
#define platformLedToogle( port, pin )     platformGpioToogle((port), (pin))                /*!< Toogle the given LED */

#define platformGpioSet( port, pin )       HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET)       /*!< Turns the given GPIO High */
#define platformGpioClear( port, pin )     HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET)     /*!< Turns the given GPIO Low */
#define platformGpioToogle( port, pin )    HAL_GPIO_TogglePin(port, pin)                    /*!< Toogles the given GPIO */
#define platformGpioIsHigh( port, pin )    (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET)    /*!< Checks if the given LED is High */
#define platformGpioIsLow(port, pin)       (!platformGpioIsHigh(port, pin))                 /*!< Checks if the given LED is Low */

#define platformTimerCreate( t)            timerCalculateTimer(t)                           /*!< Create a timer with the given time (ms) */
#define platformTimerIsExpired(timer)      timerIsExpired(timer)                            /*!< Checks if the given timer is expired */
#define platformDelay( t )                 HAL_Delay( t )                                   /*!< Performs a delay for the given time (ms) */

#define platformGetSysTick()               HAL_GetTick()                                    /*!< Get System Tick (1 tick = 1 ms) */

#define platformAssert( exp )              assert_param( exp )                              /*!< Asserts whether the given expression is true */
#define platformErrorHandle()              _Error_Handler(__FILE__, __LINE__)               /*!< Global error handle\trap */

#define platformSpiSelect()                platformGpioClear( ST25R_SS_PORT, ST25R_SS_PIN ) /*!< SPI SS\CS: Chip|Slave Select */
#define platformSpiDeselect()              platformGpioSet( ST25R_SS_PORT, ST25R_SS_PIN )   /*!< SPI SS\CS: Chip|Slave Deselect */
#define platformSpiTxRx(txBuf, rxBuf, len) hal_st25r3916_spiTxRx(txBuf, rxBuf, len)         /*!< SPI transceive */

/* I2C not used disabled */
#if 0
#define platformI2CTx(txBuf, len, last, txOnly) /*!< I2C Transmit */
#define platformI2CRx(txBuf, len)               /*!< I2C Receive */
#define platformI2CStart()                      /*!< I2C Start condition */
#define platformI2CStop()                       /*!< I2C Stop condition */
#define platformI2CRepeatStart()                /*!< I2C Repeat Start */
#define platformI2CSlaveAddrWR(add)             /*!< I2C Slave address for Write operation */
#define platformI2CSlaveAddrRD(add)             /*!< I2C Slave address for Read operation */
#endif

#define platformLog(...)
#define LOGGER_ON   1
#define LOGGER_OFF  0
#define USE_LOGGER LOGGER_OFF

/*
******************************************************************************
* GLOBAL VARIABLES
******************************************************************************
*/
extern uint8_t globalCommProtectCnt; /* Global Protection Counter provided per platform - instantiated in hydranfc_v2.c */

/*
******************************************************************************
* RFAL FEATURES CONFIGURATION
******************************************************************************
*/
#define RFAL_FEATURE_LISTEN_MODE            true /*!< Enable/Disable RFAL support for Listen Mode */
#define RFAL_FEATURE_WAKEUP_MODE            true /*!< Enable/Disable RFAL support for the Wake-Up mode */
#define RFAL_FEATURE_LOWPOWER_MODE          false /*!< Enable/Disable RFAL support for the Low Power mode */
#define RFAL_FEATURE_NFCA                   true /*!< Enable/Disable RFAL support for NFC-A (ISO14443A) */
#define RFAL_FEATURE_NFCB                   true /*!< Enable/Disable RFAL support for NFC-B (ISO14443B) */
#define RFAL_FEATURE_NFCF                   true /*!< Enable/Disable RFAL support for NFC-F (FeliCa) */
#define RFAL_FEATURE_NFCV                   true /*!< Enable/Disable RFAL support for NFC-V (ISO15693) */
#define RFAL_FEATURE_T1T                    true /*!< Enable/Disable RFAL support for T1T (Topaz) */
#define RFAL_FEATURE_T2T                    true /*!< Enable/Disable RFAL support for T2T (MIFARE Ultralight) */
#define RFAL_FEATURE_T4T                    true /*!< Enable/Disable RFAL support for T4T */
#define RFAL_FEATURE_ST25TB                 true /*!< Enable/Disable RFAL support for ST25TB */
#define RFAL_FEATURE_ST25xV                 true /*!< Enable/Disable RFAL support for ST25TV/ST25DV */
#define RFAL_FEATURE_DYNAMIC_ANALOG_CONFIG  true /*!< Enable/Disable Analog Configs to be dynamically updated (RAM) */
#define RFAL_FEATURE_DPO                    true /*!< Enable/Disable RFAL Dynamic Power Output support */
#define RFAL_FEATURE_ISO_DEP                true /*!< Enable/Disable RFAL support for ISO-DEP (ISO14443-4) */
#define RFAL_FEATURE_ISO_DEP_POLL           true /*!< Enable/Disable RFAL support for Poller mode (PCD) ISO-DEP (ISO14443-4) */
#define RFAL_FEATURE_ISO_DEP_LISTEN         true /*!< Enable/Disable RFAL support for Listen mode (PICC) ISO-DEP (ISO14443-4) */
#define RFAL_FEATURE_NFC_DEP                true /*!< Enable/Disable RFAL support for NFC-DEP (NFCIP1/P2P) */

#define RFAL_FEATURE_ISO_DEP_IBLOCK_MAX_LEN 256U /*!< ISO-DEP I-Block max length. Please use values as defined by rfalIsoDepFSx */
#define RFAL_FEATURE_NFC_DEP_BLOCK_MAX_LEN  254U /*!< NFC-DEP Block/Payload length. Allowed values: 64, 128, 192, 254 */
#define RFAL_FEATURE_NFC_RF_BUF_LEN         256U /*!< RF buffer length used by RFAL NFC layer */

#define RFAL_FEATURE_ISO_DEP_APDU_MAX_LEN   512U /*!< ISO-DEP APDU max length. */
#define RFAL_FEATURE_NFC_DEP_PDU_MAX_LEN    512U /*!< NFC-DEP PDU max length. */

/* Use HydraNFC Shield v2 RFAL Analog Config Custom */
#define RFAL_ANALOG_CONFIG_CUSTOM           true
// Custom configuration is defined in rfal_analogConfigCustomTbl.c

/* SELFTEST Helpfull to diagnose porting issues.  */
#define ST25R_SELFTEST
#define ST25R_SELFTEST_TIMER

/*
 ******************************************************************************
 * RFAL OPTIONAL MACROS            (Do not change)
 ******************************************************************************
 */                                           
#ifndef platformProtectST25RIrqStatus         
    #define platformProtectST25RIrqStatus()   /*!< Protect unique access to IRQ status var - IRQ disable on single thread environment (MCU) ; Mutex lock on a multi thread environment */
#endif /* platformProtectST25RIrqStatus */    

#ifndef platformUnprotectST25RIrqStatus       
    #define platformUnprotectST25RIrqStatus() /*!< Unprotect the IRQ status var - IRQ enable on a single thread environment (MCU) ; Mutex unlock on a multi thread environment */
#endif /* platformUnprotectST25RIrqStatus */  

#ifndef platformProtectWorker                 
    #define platformProtectWorker()           /* Protect RFAL Worker/Task/Process from concurrent execution on multi thread platforms */
#endif /* platformProtectWorker */            

#ifndef platformUnprotectWorker               
    #define platformUnprotectWorker()         /* Unprotect RFAL Worker/Task/Process from concurrent execution on multi thread platforms */
#endif /* platformUnprotectWorker */          

#ifndef platformIrqST25RPinInitialize         
    #define platformIrqST25RPinInitialize()   /*!< Initializes ST25R IRQ pin */
#endif /* platformIrqST25RPinInitialize */    

#ifndef platformIrqST25RSetCallback           
    #define platformIrqST25RSetCallback( cb ) /*!< Sets ST25R ISR callback */
#endif /* platformIrqST25RSetCallback */      

#ifndef platformLedsInitialize                
    #define platformLedsInitialize()          /*!< Initializes the pins used as LEDs to outputs */
#endif /* platformLedsInitialize */           

#ifndef platformLedOff                        
    #define platformLedOff( port, pin )       /*!< Turns the given LED Off */
#endif /* platformLedOff */                                                                     

#ifndef platformLedOn                                                                           
    #define platformLedOn( port, pin )        /*!< Turns the given LED On */
#endif /* platformLedOn */                                                                      

#ifndef platformLedToogle                                                                       
    #define platformLedToogle( port, pin )    /*!< Toggles the given LED */
#endif /* platformLedToogle */                                                                  

#ifndef platformGetSysTick                                                                      
    #define platformGetSysTick()              /*!< Get System Tick (1 tick = 1 ms) */
#endif /* platformGetSysTick */                                                                 

#ifndef platformTimerDestroy                                                                    
    #define platformTimerDestroy( timer )     /*!< Stops and released the given timer */
#endif /* platformTimerDestroy */             

#ifndef platformAssert                                                                    
    #define platformAssert( exp )             /*!< Asserts whether the given expression is true */
#endif /* platformAssert */                   

#ifndef platformErrorHandle                                                                    
    #define platformErrorHandle()             /*!< Global error handler or trap */
#endif /* platformErrorHandle */

#endif /* PLATFORM_H */
