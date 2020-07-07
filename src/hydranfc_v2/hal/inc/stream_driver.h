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
 *  \author
 *
 *  \brief Streaming driver interface declarations.
 *  The defines allow switching between different stream drivers,
 *  currently implemented are:
 *  - USB   
 *  - UART  
 *
 */

/*!
 *
 *
 */
#ifndef STREAM_DRIVER_H
#define STREAM_DRIVER_H
/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include <stdint.h>
#include "st_stream.h"  /* stream protocol definitions */

#include "usb_hid_stream_driver.h"
#include "usbd_custom_hid_if.h"


/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define USE_UART_STREAM_DRIVER 0
/* redirect according to underlying communication protocol being usb-hid, uart */
#if USE_UART_STREAM_DRIVER

#define StreamInitialize       uartStreamInitialize
#define StreamConnect          uartStreamConnect
#define StreamDisconnect       uartStreamDisconnect
#define StreamReady            uartStreamReady
#define StreamHasAnotherPacket uartStreamHasAnotherPacket
#define StreamPacketProcessed  uartStreamPacketProcessed
#define StreamReceive          uartStreamReceive
#define StreamTransmit         uartStreamTransmit


#else /* USE_USB_STREAM_DRIVER */

#define StreamInitialize       usbStreamInitialize
#define StreamConnect          usbStreamConnect
#define StreamDisconnect       usbStreamDisconnect
#define StreamReady            usbStreamReady
#define StreamHasAnotherPacket usbStreamHasAnotherPacket
#define StreamPacketProcessed  usbStreamPacketProcessed
#define StreamReceive          usbStreamReceive
#define StreamTransmit         usbStreamTransmit

#endif



#endif /* STREAM_DRIVER_H */

