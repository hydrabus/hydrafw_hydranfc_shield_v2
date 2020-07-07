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
 *  \brief SPI communication header file
 *
 */
 
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __spi_H
#define __spi_H

/* Includes ------------------------------------------------------------------*/
#include "platform.h"

/*!
 *****************************************************************************
 *  \brief  Initalize SPI
 * 
 *  This function initalize the SPI handle.
 *
 *  \param[in] dev_num : bsp_dev_spi_t device number
 *
 *****************************************************************************
 */   
void hal_st25r3916_spiInit(bsp_dev_spi_t dev_num);

/*!
 *****************************************************************************
 *  \brief  Set SPI CS line
 * 
 *  \param[in] ssPort : pointer CS gpio port
 *
 *  \param[in] ssPin : CS pin
 *
 *  \return : none
 *
 *****************************************************************************
 */
void hal_st25r3916_spiSelect(GPIO_TypeDef *ssPort, uint16_t ssPin);

/*!
 *****************************************************************************
 *  \brief  Reset SPI CS line
 * 
 *  \param[in] ssPort : pointer CS gpio port
 *
 *  \param[in] ssPin : CS pin
 *
 *  \return : none
 *
 *****************************************************************************
 */
void hal_st25r3916_spiDeselect(GPIO_TypeDef *ssPort, uint16_t ssPin);

/*!
 *****************************************************************************
 *  \brief  Transmit Receive data 
 * 
 *  This funtion transmits first no of "length" bytes from "txData" and tries 
 *  then to receive "length" bytes.
 * 
 *  \param[in] txData : pointer to buffer to be transmitted.
 *
 *  \param[out] rxData : pointer to buffer to be received.
 *
 *  \param[in] length : buffer length
 *
 *  \return : HAL error code
 *
 *****************************************************************************
 */
HAL_StatusTypeDef hal_st25r3916_spiTxRx(const uint8_t *txData, uint8_t *rxData, uint16_t length);
   
#endif /*__spi_H */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
