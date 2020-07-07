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
 *      PROJECT:   ASxxxx firmware
 *      $Revision: $
 *      LANGUAGE:  ANSI C
 */

/*! \file
 *
 *  \author M. Arpa originated by W. Reichart
 *  \author 2020 Benjamin VERNOUX modified for hydrafw
 *
 *  \brief main dispatcher for streaming protocol uses a stream_driver
 *   for the io.
 *
 */

/* --------- includes ------------------------------------------------------- */

#include <string.h>
#include <stdint.h>
#include "st_stream.h"
#include "stream_dispatcher.h"
#include "stream_driver.h"
#include "logger.h"
#include "platform.h"

/* ------------- local variables -------------------------------------------- */
static uint8_t rxBuffer[ST_STREAM_BUFFER_SIZE]; /*! buffer to store protocol packets received from the Host */
static uint8_t txBuffer[ST_STREAM_BUFFER_SIZE]; /*! buffer to store protocol packets which are transmitted to the Host */

static uint8_t lastError; /* flag indicating different types of errors that cannot be reported in the protocol status field */

static uint8_t handleFirmwareInformation(uint16_t *toTx, uint8_t *txData)
{
	uint16_t size = strlen(applFirmwareInformation());
	if (size >= *toTx - 1) {
		size = *toTx - 1;
	}
	if (size >= ST_STREAM_SHORT_STRING - 1) {
		size = ST_STREAM_SHORT_STRING - 1;
	}
	*toTx = size + 1; /* for zero terminator */
	memcpy(txData, applFirmwareInformation(), size + 1);
	return ST_STREAM_NO_ERROR;
}

static uint8_t handleFirmwareNumber(uint16_t *toTx, uint8_t *txData)
{
	*toTx = 3; /* 1-byte major, 1-byte minor, 1-byte release marker */
	txData[0] = (firmwareNumber >> 16) & 0xFF;
	txData[1] = (firmwareNumber >> 8) & 0xFF;
	txData[2] = firmwareNumber & 0xFF;
	return ST_STREAM_NO_ERROR;
}

static uint16_t processReceivedPackets(void)
{
	/* every time we enter this function, the last txBuffer was already sent.
	 So we fill a new transfer buffer */
	uint8_t *txEnd = txBuffer; /* the txEnd always points to the next position to be filled with data */
	uint16_t txSize = 0;

	while ( StreamHasAnotherPacket()) {
		/* read out protocol header data */
		uint8_t protocol = ST_STREAM_DR_GET_PROTOCOL(rxBuffer);
		uint16_t rxed = ST_STREAM_DR_GET_RX_LENGTH(rxBuffer);
		uint16_t toTx = ST_STREAM_DR_GET_TX_LENGTH(rxBuffer);
		uint8_t *rxData = ST_STREAM_PAYLOAD(rxBuffer);
		/* set up tx pointer for any data to be transmitted back to the host */
		uint8_t *txData = ST_STREAM_PAYLOAD(txEnd);
		uint8_t status = ST_STREAM_NO_ERROR;
		int8_t isReadCommand = !(protocol & ST_COM_WRITE_READ_NOT);
		protocol &= ~ST_COM_WRITE_READ_NOT; /* remove direction flag */

		if (rxed > ST_STREAM_MAX_DATA_SIZE) {
			/* package is damaged or comes from an unknown source */
			lastError = ST_STREAM_SIZE_ERROR;
			/* truncate received packet - will most likely cause problems
			 at the receiving function, but the size is out of bounds */
			rxed = ST_STREAM_MAX_DATA_SIZE;
		}

		/* decide which protocol to execute */

		switch (protocol) {
		case ST_COM_CTRL_CMD_FW_INFORMATION:
			status = handleFirmwareInformation(&toTx, txData);
			break;
		case ST_COM_CTRL_CMD_FW_NUMBER:
			status = handleFirmwareNumber(&toTx, txData);
			break;
		case ST_COM_CTRL_CMD_ENTER_BOOTLOADER:
			//bootloaderReboot(  );
			break;
		case ST_COM_WRITE_REG:
			status = applWriteReg(rxed, rxData, &toTx, txData);
			break;
		case ST_COM_READ_REG:
			status = applReadReg(rxed, rxData, &toTx, txData);
			break;
		default:
			if (protocol > ST_COM_CONFIG) {
				/* reserved protocol value and not handled so far */
				status = ST_STREAM_UNHANDLED_PROTOCOL;
			} else { /* application protocol */
				status = applProcessCmd(protocol, rxed, rxData,
						&toTx, txData);
			}
			break;
		}

		/* fill out transmit packet header if any data was produced */
		if (toTx > 0 || isReadCommand) {
			ST_STREAM_DT_SET_PROTOCOL(txEnd, protocol);
			ST_STREAM_DT_SET_STATUS(txEnd, status);
			ST_STREAM_DT_SET_TX_LENGTH(txEnd, toTx);

			/* adjust pointer to the enter next data, and total size */
			txEnd += (toTx + ST_STREAM_HEADER_SIZE);
			txSize += (toTx + ST_STREAM_HEADER_SIZE);
		} else if (status != ST_STREAM_NO_ERROR) { /* protocol failed, we indicate an error if command itself does not send back */
			lastError = status;
		}

		/* remove the handled packet, and move on to next packet */
		StreamPacketProcessed(rxed);
	}

	return txSize;
}

static uint16_t processCyclic(void)
{
	/* every time we enter this function, the last txBuffer was already sent.
	 So we fill a new transfer buffer */
	uint8_t *txEnd = txBuffer; /* the txEnd always points to the next position to be filled with data */
	uint16_t txSize = 0;
	uint16_t toTx;
	uint8_t protocol;
	uint8_t status = ST_STREAM_NO_ERROR;

	do {
		/* set up tx pointer for any data to be transmitted back to the host */
		uint8_t *txData = ST_STREAM_PAYLOAD(txEnd);
		toTx = 0;
		status = applProcessCyclic(&protocol, &toTx, txData,
				ST_STREAM_MAX_DATA_SIZE - txSize);

		/* fill out transmit packet header if any data was produced */
		if (toTx > 0) {
			ST_STREAM_DT_SET_PROTOCOL(txEnd, protocol);
			ST_STREAM_DT_SET_STATUS(txEnd, status);
			ST_STREAM_DT_SET_TX_LENGTH(txEnd, toTx);

			/* adjust pointer to the enter next data, and total size */
			txEnd += (toTx + ST_STREAM_HEADER_SIZE);
			txSize += (toTx + ST_STREAM_HEADER_SIZE);
		} else if (status != ST_STREAM_NO_ERROR) { /* protocol failed, we indicate an error if command itself does not send back */
			lastError = status;
		}
	} while (toTx > 0);

	return txSize;
}

/* --------- global functions ------------------------------------------------------------- */

void StreamDispatcherInitAndConnect(void)
{
	StreamDispatcherInit();
	StreamConnect();
}

void StreamDispatcherInit(void)
{
	StreamDispatcherGetLastError();
	StreamInitialize(rxBuffer, txBuffer);
}

uint8_t StreamDispatcherGetLastError(void)
{
	uint8_t temp = lastError;
	lastError = ST_STREAM_NO_ERROR;
	return temp;
}

void ProcessIO(void)
{
	uint16_t txSize;

	if ( StreamReady()) {
		/* read out data from stream driver, and move it to module-local buffer */
		if ( StreamReceive() > 0) {
			/* if we have at least one fully received packet, we start execution */

			/* interpret one (or more) packets in the module-local buffer */
			txSize = processReceivedPackets();

			/* transmit any data waiting in the module-local buffer */
			StreamTransmit(txSize);
		}

		/* we need to call the processCyclic function for all applications that
		 have any data to send (without receiving a hid packet). The data to
		 be sent is written into the module-local buffer */
		txSize = processCyclic();

		/* transmit any data waiting in the module-local buffer */
		StreamTransmit(txSize);
	}
}

