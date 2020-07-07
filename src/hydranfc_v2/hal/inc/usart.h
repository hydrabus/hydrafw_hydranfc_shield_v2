/**
  ******************************************************************************
  * COPYRIGHT(c) 2016 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  * 1. Redistributions of source code must retain the above copyright notice,
  * this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  * this list of conditions and the following disclaimer in the documentation
  * and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of its contributors
  * may be used to endorse or promote products derived from this software
  * without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
*/
/*! \file
 *
 *  \author 
 *
 *  \brief UART communication header file
 *
 */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __usart_H
#define __usart_H

/* Includes ------------------------------------------------------------------*/
#include "platform.h"

/*!
 *****************************************************************************
 *  \brief  Initalize USART
 * 
 *  This function initalize the USART handle.
 *
 *  \param[in] husart : pointer to initalized USART block 
 *
 *****************************************************************************
 */   
void usartInit(UART_HandleTypeDef *husart);

/*!
 *****************************************************************************
 *  \brief  Transmit byte
 * 
 *  This funtion transmits one data byte via the USART.
 * 
 *  \param[in] data : byte to be transmitted.
 *
 *  \return : HAL error code
 *
 *****************************************************************************
 */
uint8_t usartTxByte(uint8_t data);

/*!
 *****************************************************************************
 *  \brief  Transmit n byte
 * 
 *  This funtion transmits "dataLen" bytes from "data" via the USART.
 * 
 *  \param[in] data : bytes to be transmitted.
 *
 *  \param[in] dataLen : no of bytes to be transmitted.
 *
 *  \return : HAL error code
 *
 *****************************************************************************
 */
uint8_t usartTx(uint8_t *data, uint16_t dataLen);

/*!
 *****************************************************************************
 *  \brief  Receive data 
 * 
 *  This funtion receives "dataLen" bytes from "data" from the USART.
 * 
 *  \param[out] data : pointer to buffer where received data shall be copied to.
 *
 *  \param[out] dataLen : max no of bytes that shall be received.
 *
 *  \return : HAL error code
 *
 *****************************************************************************
 */
uint8_t usartRx(uint8_t *data, uint16_t *dataLen);

#endif /*__usart_H */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
