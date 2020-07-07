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
 *      PROJECT:   common firmware
 *      $Revision: $
 *      LANGUAGE:  C
 */

/*! \file
 *
 *  \author M. Arpa
 *
 *  \brief Interface for stream packet handling.
 *
 */


#ifndef STREAM_DISPATCHER_H
#define STREAM_DISPATCHER_H

/* ------------ includes ----------------------------------------- */

#include <stdint.h>
#include "stream_driver.h"


/* ------------ constants ---------------------------------------- */

/*
 * The application should define its own firmware version. Please write
 * in your application file:
 *
 * E.g. for major 0x01, minor 0x03, releaseMarker 0x07
 * const uint32_t firmwareNumber = 0x010307UL;
 *
 * E.g. for major 0x02, minor 0x00, releaseMarker 0x18
 * const uint32_t firmwareNumber = 0x020018UL;
 */
extern const uint32_t firmwareNumber;



/* ------------ application functions ---------------------------- */

/*!
 *****************************************************************************
 *  \brief  reset peripherals of board
 *
 *  function which can be implemented by the application to reset
 *  the board peripherals.
 *  \return ST_STREAM_UNHANDLED_PROTOCOL if function was not overloaded by application
 *****************************************************************************
 */
extern uint8_t applPeripheralReset(void);

/*!
 *****************************************************************************
 *  \brief  get firmware information (zero-terminated) string
 *
 *  function which can be implemented by the application to return the
 *  firmware inforamtion string (zero-terminated). E.g. information about
 *  the chip and board.
 *  \return the a pointer to the firmware information
 *****************************************************************************
 */
extern const char * applFirmwareInformation(void);

/*!
 *****************************************************************************
 *  \brief  Command dispatcher for application-specific commands
 *
 *  function which can be implemented by the application process application-
 *  specific commands for I/O. If data should be returned, the txData buffer
 *  can be filled with data which should be sent to the PC. In argument txSize,
 *  the size is returned.
 *  \param[in] protocol : the protocol byte which needs to be processed
 *  \param[in] rxData : pointer to payload for appl commands (in stream protocol buffer).
 *  \param[in] rxSize : size of rxData
 *  \param[out] txData : pointer to buffer to store returned data (payload only)
 *  \param[out] txSize : size of returned data
 *  \return the status byte to be interpreted by the stream layer on the host
 *****************************************************************************
 */
extern uint8_t applProcessCmd( uint8_t protocol, uint16_t rxSize, const uint8_t * rxData, uint16_t * txSize, uint8_t * txData );

/*!
 *****************************************************************************
 *  \brief  Called cyclic (even when no full usb packet was received). Use
 *          this is you need to send several packets (time delayed) in
 *          response to one usb request.
 *
 *  function which can be implemented by the application process
 *  If data should be returned, the txData buffer must be filled with the data to
 *  be sent to the PC. In argument txSize, the size is returned. The function
 *  also must fill in the protocol byte (this is the protocol value that is
 *  filled in in the protocol header.
 *  \param[out] protocol : protocol byte to be used for the packet header
 *  \param[out] txData : pointer to buffer to store returned data (payload only)
 *  \param[out] txSize : size of returned data
 *  \param[in]  remainingSize : how many bytes are free in the buffer txData
 *  \return the status byte to be interpreted by the stream layer on the host
 *****************************************************************************
 */
extern uint8_t applProcessCyclic( uint8_t * protocol, uint16_t * txSize, uint8_t * txData, uint16_t remainingSize );

/*!
 *****************************************************************************
 *  \brief  Generic function to read one or more registers
 *
 *  Function which can be implemented by the application to read registers.
 *  If data should be returned, the txData buffer must be filled with the data to
 *  be sent to the PC. In argument txSize, the size is returned. The function
 *  also must fill in the protocol byte (this is the protocol value that is
 *  filled in in the protocol header.
 *  \param[in] rxData : pointer to payload for appl commands (in stream protocol buffer).
 *  \param[in] rxSize : size of rxData
 *  \param[out] txData : pointer to buffer to store returned data (payload only)
 *  \param[out] txSize : size of returned data
 *  \return the status byte to be interpreted by the stream layer on the host
 *****************************************************************************
 */
extern uint8_t applReadReg( uint16_t rxSize, const uint8_t * rxData, uint16_t * txSize, uint8_t * txData );

/*!
 *****************************************************************************
 *  \brief  Generic function to write one or more registers
 *
 *  Function which can be implemented by the application to write registers.
 *  If data should be returned, the txData buffer must be filled with the data to
 *  be sent to the PC. In argument txSize, the size is returned. The function
 *  also must fill in the protocol byte (this is the protocol value that is
 *  filled in in the protocol header.
 *  \param[in] rxData : pointer to payload for appl commands (in stream protocol buffer).
 *  \param[in] rxSize : size of rxData
 *  \param[out] txData : pointer to buffer to store returned data (payload only)
 *  \param[out] txSize : size of returned data
 *  \return the status byte to be interpreted by the stream layer on the host
 *****************************************************************************
 */
extern uint8_t applWriteReg( uint16_t rxSize, const uint8_t * rxData, uint16_t * txSize, uint8_t * txData );


/* ------------ functions ---------------------------------------- */


/********************************************************************
 *  \brief returns the last error that occured and clears the error
 *  *******************************************************************/
uint8_t StreamDispatcherGetLastError(void);

/********************************************************************
 *  \brief  initialization of stream dispatcher and connect to the
 *  communication stream (e.g. USB HID).
 *
 *  This function does all the necessary initialization for the Stream dispatcher.
 *  It also connects to the IO.
 *
 *  \param[in] sysClk : configured sysclk.
 *   Required to set correct transfer rates.
 *  *******************************************************************/
void StreamDispatcherInitAndConnect( void );

/********************************************************************
 *  \brief  initialisation of the stream dispatcher (no connect)
 *
 *  \param[in] sysClk : configured sysclk. Required to set correct spi/i2c data rates.
 *  *******************************************************************/
void StreamDispatcherInit( void );

/********************************************************************
 *  \brief  Main entry point into the stream dispatcher must be called cyclic.
 *
 *  This function checks the stream driver for received data. If new data is
 *  available it is processed and forwarded to the application
 *  functions.
 *  *******************************************************************/
void ProcessIO(void);

#endif /* STREAM_DISPATCHER */
