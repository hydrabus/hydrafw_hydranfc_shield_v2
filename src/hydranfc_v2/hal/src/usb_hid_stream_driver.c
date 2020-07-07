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
 *          M. Arpa
 *
 *  \brief USB HID streaming driver implementation.
 *
 */

/*
 ******************************************************************************
 * INCLUDES
 ******************************************************************************
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "st_stream.h"
#include "st_errno.h"
#include "stream_driver.h"
#include "stream_dispatcher.h"
#include "usb_hid_stream_driver.h"
#include "logger.h"
#include "usbd_custom_hid_if.h"

/*
 ******************************************************************************
 * LOCAL VARIABLES
 ******************************************************************************
 */
static uint8_t initalized = false;
static uint8_t usbRxBuffer[USB_HID_REPORT_SIZE];
static uint8_t usbTxBuffer[USB_HID_REPORT_SIZE];

static uint8_t * rxBuffer;   /* INFO: buffer location is set in StreamInitialize */
static uint8_t * txBuffer;   /* INFO: buffer location is set in StreamInitialize */

static uint8_t txTid;
static uint8_t rxTid;
static uint16_t rxSize;
static uint8_t * rxEnd; /* pointer to next position where to copy the received data */


#define ioLedOn()     ;
#define ioLedOff()    ;


/*
 ******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************
 */

void usbStreamInitialize (uint8_t * rxBuf, uint8_t * txBuf)
{
  
  /* setup pointers to buffers */
  rxBuffer = rxBuf;
  txBuffer = txBuf;
  /* so far we have received nothing */
  txTid = 0;
  rxTid = 0;
  rxSize = 0;
  rxEnd = rxBuffer;
  initalized = true;
}

void usbStreamConnect (void)
{
  /* dummy implementation */
}

void usbStreamDisconnect (void)
{
  /* dummy implementation */
}

uint8_t usbStreamReady (void)
{
  return initalized;
}

void usbStreamPacketProcessed ( uint16_t rxed )
{
  /*
   * FIXME: knowing that a memmove() is not the most performant variant,
   * it is used here to keep the code simpler.
   * If we run into performance issues on the PIC, this can be replaced
   * by pointer operations.
   */
  rxed += ST_STREAM_HEADER_SIZE;
  /* decrease remaining data length by length of consumed packet */
  rxSize -= rxed;

  memmove( rxBuffer, rxBuffer + rxed, rxSize );
  /* correct end pointer to the new end of the buffer */
  rxEnd = rxBuffer + rxSize;
}

int8_t usbStreamHasAnotherPacket ( )
{
  return (  rxSize >= ST_STREAM_HEADER_SIZE
            && rxSize >= ( ST_STREAM_DR_GET_RX_LENGTH( rxBuffer ) + ST_STREAM_HEADER_SIZE )
         );
}

uint16_t usbStreamOldFormatRequest ( )
{
  uint8_t protocol = usbRxBuffer[ 2 ];
  uint8_t toTx = usbRxBuffer[ 3 ];

  ioLedOn();

  /* send back the special answer */
  if ( toTx > 0 || ( protocol & 0x40 ) ) { /* response was required */
   
    /* wait here (and before copying the IN-buffer) until the USBInHandle is free again */


    usbTxBuffer[ 0 ] = ST_STREAM_COMPATIBILITY_TID;
    usbTxBuffer[ 1 ] = 0x03; /* payload */
    usbTxBuffer[ 2 ] = protocol;
    usbTxBuffer[ 3 ] = 0xFF; /* status = failed -> wrong protocol version */
    usbTxBuffer[ 4 ] = 0x00; /* no data will be sent back */

    /* initiate transfer now */   
    UsbTransmit(usbTxBuffer, USB_HID_REPORT_SIZE);
  }

  return 0;
}

uint16_t usbStreamReceive ( )
{
    uint16_t rxLen = 0;
    if( USBD_OK == UsbReceive(usbRxBuffer, &rxLen) ) {			
      
      /*
       * Read the usb hid buffer into the local buffer.
       * When the RX-Length within the first streaming packet is
       * longer than the HID payload, we have to concatenate several
       * packets (up to max. 256 u8s)
       */

    uint16_t packetSize;
    uint8_t payload  = USB_HID_PAYLOAD_SIZE( usbRxBuffer );

    if ( USB_HID_STATUS( usbRxBuffer ) != 0 ) { 
      /* this is a request in the old format */
      /* in the old format at this position we had the protocol id - which was never 0
             in the new format here this uint8_t is resered and 0 when sent from host to device */
      return usbStreamOldFormatRequest( );
    }

    ioLedOn();

    rxTid = USB_HID_TID( usbRxBuffer );
    /* adjust number of totally received u8s */
    rxSize += payload;

    /* add the new data at the end of the data in the rxBuffer (i.e. where rxEnd points to) */
    memcpy( rxEnd, USB_HID_PAYLOAD( usbRxBuffer ), payload );
    rxEnd += payload;
    packetSize = ST_STREAM_DR_GET_RX_LENGTH( rxBuffer ) + ST_STREAM_HEADER_SIZE;

    ioLedOff();

    if ( packetSize > rxSize ) {
      /* indicate that we must continue receiving */
      return 0;
    }
    rxEnd = rxBuffer;   /* next time we receive new data, we start over at buffer start */
    return rxSize;
  }
  /* indicate that we did not receive anything - try next time again */
  return 0;
}

void usbStreamTransmit ( uint16_t totalTxSize )
{
  uint16_t offset = 0;

  while ( totalTxSize > 0 ) {
    uint8_t payload = ( totalTxSize > USB_HID_MAX_PAYLOAD_SIZE ? USB_HID_MAX_PAYLOAD_SIZE : totalTxSize );
    ioLedOn( );

    /* wait here (and before copying the IN-buffer) until the USBInHandle is free again */
    while ( !UsbTransmitReady() )
      ;
 
    /* generate a new tid for tx */
    USB_HID_GENERATE_TID_FOR_TX( rxTid, txTid );

    /* TX-packet setup */
    USB_HID_TID( usbTxBuffer )          = txTid;
    USB_HID_PAYLOAD_SIZE( usbTxBuffer ) = payload;
    USB_HID_STATUS( usbTxBuffer )       = StreamDispatcherGetLastError();

    /* copy data to usb buffer */
    memcpy( USB_HID_PAYLOAD( usbTxBuffer ), txBuffer + offset, payload );

    totalTxSize -= payload;
    offset += payload;

    /* initiate transfer now */
    UsbTransmit(usbTxBuffer, USB_HID_REPORT_SIZE);

    ioLedOff( );
  }
}
