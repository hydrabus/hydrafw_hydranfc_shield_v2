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
 *      PROJECT:   
 *      $Revision: $
 *      LANGUAGE:  ANSI C
 */

/*! \file
 *
 *  \author Wolfgang Reichart
 *
 *  \brief USB HID streaming driver declarations.
 *
 */

/*!
 *
 *
 */
#ifndef _USB_HID_STREAM_DRIVER_H
#define _USB_HID_STREAM_DRIVER_H

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include <stdint.h>
#include "st_stream.h"
#include "stream_driver.h"

/*
 ******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************
 */


/*!
 *****************************************************************************
 *  \brief  initializes the USB HID Stream driver variables
 *
 *  This function takes care for proper initialisation of buffers, variables, etc.
 *
 *  \param rxBuf : buffer where received packets will be copied into
 *  \param txBuf : buffer where to be transmitted packets will be copied into
 *****************************************************************************
 */
void usbStreamInitialize (uint8_t * rxBuf, uint8_t * txBuf);

/*!
 *****************************************************************************
 *  \brief  initialises the usb endpoints and connects to host
 *****************************************************************************
 */
void usbStreamConnect(void);

/*!
 *****************************************************************************
 *  \brief  disconnects from usb host
 *****************************************************************************
 */
void usbStreamDisconnect(void);

/*!
 *****************************************************************************
 *  \brief  returns 1 if stream init is finished
 *
 *  \return 0=init has not finished yet, 1=stream has been initialized
 *****************************************************************************
 */
uint8_t usbStreamReady(void);

/*!
 *****************************************************************************
 *  \brief  tells the stream driver that the packet has been processed and can
 *   be moved from the rx buffer
 *
 *  \param rxed : number of bytes which have been processed
 *****************************************************************************
 */
void usbStreamPacketProcessed (uint16_t rxed);

/*!
 *****************************************************************************
 *  \brief  returns 1 if another packet is available in buffer
 *
 *  \return 0=no packet available in buffer, 1=another packet available
 *****************************************************************************
 */
int8_t usbStreamHasAnotherPacket (void);

/*!
 *****************************************************************************
 *  \brief checks if there is data received on the HID device from the host
 *  and copies the received data into a local buffer
 *
 *  Checks if the usb HID device has data received from the host, copies this
 *  data into a local buffer. The data in the local buffer is than interpreted
 *  as a packet (with header, rx-length and tx-length). As soon as a full
 *  packet is received the function returns non-null.
 *
 *  \return 0 = nothing to process, >0 at least 1 packet to be processed
 *****************************************************************************
 */
uint16_t usbStreamReceive (void);

/*!
 *****************************************************************************
 *  \brief checks if there is data to be transmitted from the HID device to
 *  the host.
 *
 *  Checks if there is data waiting to be transmitted to the host. Copies this
 *  data from a local buffer to the usb buffer and transmits this usb buffer.
 *  If more than 1 usb hid report is needed to transmit the data, the function
 *  waits until the first one is sent, and than refills the usb buffer with
 *  the next chunk of data and transmits the usb buffer again. And so on, until
 *  all data is sent.
 *
 *  \param [in] totalTxSize: the size of the data to be transmitted (the HID
 *  header is not included)
 *****************************************************************************
 */
void usbStreamTransmit( uint16_t totalTxSize );

#endif // _USB_HID_STREAM_DRIVER_H

