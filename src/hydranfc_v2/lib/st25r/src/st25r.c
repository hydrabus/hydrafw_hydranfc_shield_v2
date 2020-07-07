/**
 ******************************************************************************
 * @file    st25_discovery_st25r.c
 * @author  MMY Application Team
 * @version $Revision$
 * @date    $Date$
 * @brief   This file provides set of firmware functions to manage nfc tags
 *          from a ST25 Reader perspective (rfal based).
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT 2018 STMicroelectronics</center></h2>
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
 ******************************************************************************
 */
#include "st25r.h"
#include "hydranfc_v2_common.h"
#include "utils.h"

/* Hold current selected NFC device instance */
static BSP_NFCTAG_Device_t *Current = NULL;
/* Hold current protocol set */
static BSP_NFCTAG_Protocol_t *CurrentProtocol = NULL;

/* NFC T2 Read command */
static uint8_t t2tReadReq[] = {0x30, 0x00}; /* T2T READ Block:0 */
/* NFC T2 Write command */
static uint8_t t2tWriteReq[] = {0xA2, 0x00, 0x00, 0x00, 0x00, 0x00}; /* T2T READ Block:0 */
/* NFC T3 Check command */
static uint8_t t3tCheckReq[] = {0x06, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x01, 0x09, 0x00, 0x01, 0x80, 0x00}; /* T3T Check/Read command */
/* NFC T3 Update command */
static uint8_t t3tUpdateReq[] = {0x08, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x01, 0x09, 0x00, 0x01, 0x80, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; /* T3T Update command */
/* NFC T4 Select Ndef App command */
static uint8_t t4tSelectNdefApp[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD2, 0x76,
		0x00, 0x00, 0x85, 0x01, 0x01, 0x00};
/* NFC T4 Select File command */
static uint8_t t4tSelectFile[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, 0x00, 0x01};
/* NFC T4 Write command */
static uint8_t t4tWriteData[] = {0x00, 0xD6, 0x00, 0x00, 0x00};
/* NFC T4 Read Binary command */
/*                                              Offset     Length  */
static uint8_t t4tReadBinary[] = {0x00, 0xB0, 0x00, 0x00, 0x00};

/* NFC T4 Success code */
static uint8_t t4Success[] = {0x90, 0x00};

/****************** NFCA *****************************/
/* NFCA RX Buffer length in bytes */
#define BSP_NFCTAG_NFCA_RXBUF_LENGTH 0xFF
static uint8_t BSP_NFCTAG_ReadData_NfcA(uint8_t *buffer, uint32_t offset,
		uint32_t length);

/* NFCA callback when waiting for a for long latency tag response */
BSP_NFCTAG_WaitingCallback_t *BSP_NFCTAG_WaitingCb = NULL;

/* Sends a T4 command, and waits the response */
static ReturnCode t4tSendCommand(uint8_t *txBuf, uint16_t txLen,
		rfalIsoDepBufFormat *rxBuf, uint16_t *rxBufLen)
{

	ReturnCode err;
	rfalIsoDepTxRxParam isoDepTxRx;
	rfalIsoDepBufFormat isoDepTxBuf; /* ISO-DEP Tx buffer format (with header/prologue) */
	bool rxChaining = false; /* Rx chaining flag                                */

	memcpy(isoDepTxBuf.inf, txBuf, txLen);

	isoDepTxRx.DID = RFAL_ISODEP_NO_DID;
	isoDepTxRx.ourFSx = RFAL_ISODEP_FSX_KEEP;
	isoDepTxRx.FSx = Current->proto.isoDep.info.FSx;
	isoDepTxRx.dFWT = Current->proto.isoDep.info.dFWT;
	isoDepTxRx.FWT = Current->proto.isoDep.info.FWT;
	isoDepTxRx.txBuf = &isoDepTxBuf;
	isoDepTxRx.txBufLen = txLen;
	isoDepTxRx.isTxChaining = false;
	isoDepTxRx.rxBuf = rxBuf;
	isoDepTxRx.rxLen = rxBufLen;
	isoDepTxRx.isRxChaining = &rxChaining;

	/*******************************************************************************/
	/* Trigger a RFAL ISO-DEP Transceive                                           */
	err = rfalIsoDepStartTransceive(isoDepTxRx);
	if (err != ERR_NONE)
	{
		return err;
	}
	do {
		do {
			err = rfalIsoDepGetTransceiveStatus();
			rfalWorker();
			// user callback when timeout is too long!
			if (BSP_NFCTAG_WaitingCb != NULL)
			{
				if (!BSP_NFCTAG_WaitingCb())
					return err;
			}
		} while (err == ERR_BUSY);
	} while ((err == ERR_NONE) && *isoDepTxRx.isRxChaining);
	return err;
}

/**
 * @brief Selects the T4 tag file based on its fileId.
 * @param fileId Id of the file to be selected
 * @returns the command status, 0 is a success, other values are errors
 */
uint8_t BSP_NFCTAG_T4_SelectFile(uint16_t fileId)
{
	ReturnCode err;
	rfalIsoDepBufFormat rxBuf;
	uint16_t rxBufLen = sizeof(rxBuf);

	t4tSelectFile[5] = fileId >> 8;
	t4tSelectFile[6] = fileId & 0xFF;
	err = t4tSendCommand(t4tSelectFile, sizeof(t4tSelectFile), &rxBuf,
			&rxBufLen);
	if ((err != ERR_NONE) || (rxBuf.inf[0] != 0x90)
			|| (rxBuf.inf[1] != 0x00))
			{
		return NFCTAG_ERROR;
	}

	return NFCTAG_OK;
}

/* NFCA Tag activation stage (Select Tag and NDEF file) */
static uint8_t BSP_NFCTAG_Activate_NfcA(void)
{
	ReturnCode err;
	rfalNfcaSensRes sensRes;
	rfalNfcaSelRes selRes;
	rfalIsoDepBufFormat rxBuf;
	uint16_t rxBufLen = sizeof(rxBuf);
	uint16_t ndefFileId;

	if (Current->type != BSP_NFCTAG_NFCA)
			{
		return NFCTAG_ERROR;
	}

	// By default: claim that Ndef is not supported
	Current->NdefSupport = 0;

	rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
	rfalNfcaPollerInitialize();
	err = rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA,
			&sensRes); /* Wake up all cards  */
	if (err != ERR_NONE)
	{
		return NFCTAG_ERROR;
	}

	if (Current->dev.nfca.type != RFAL_NFCA_T1T) {
		err = rfalNfcaPollerSelect(Current->dev.nfca.nfcId1,
				Current->dev.nfca.nfcId1Len, &selRes); /* Select specific device  */
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
	} else {
		rfalT1TPollerInitialize();
		// Ndef are supported
		Current->NdefSupport = 1;
	}

	if (Current->dev.nfca.type == RFAL_NFCA_T4T)
			{
		/*******************************************************************************/
		rfalIsoDepInitialize();

		/* Perform ISO-DEP (ISO14443-4) activation: RATS and PPS if supported */
		err = rfalIsoDepPollAHandleActivation(
				(rfalIsoDepFSxI)RFAL_ISODEP_FSDI_DEFAULT,
				RFAL_ISODEP_NO_DID,
				RFAL_BR_848 /* RFAL_BR_424 RFAL_BR_106 */,
				&Current->proto.isoDep);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		err = t4tSendCommand(t4tSelectNdefApp, sizeof(t4tSelectNdefApp),
				&rxBuf, &rxBufLen);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		if (memcmp(t4Success, rxBuf.inf, sizeof(t4Success)))
				{
			return NFCTAG_RESPONSE_ERROR;
		}

		// get NDEF file Id from CCfile
		BSP_NFCTAG_T4_SelectFile(0xE103);
		BSP_NFCTAG_ReadData_NfcA((uint8_t*)&ndefFileId, 9, 2);
		ndefFileId = (ndefFileId >> 8) | ((ndefFileId & 0xFF) << 8);
		err = BSP_NFCTAG_T4_SelectFile(ndefFileId);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		// Ndef are supported
		Current->NdefSupport = 1;
	}
	return NFCTAG_OK;
}

/* NFCA Tag read data method */
static uint8_t BSP_NFCTAG_ReadData_NfcA(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint8_t rxBuf[RFAL_ISODEP_DEFAULT_FSC + RFAL_ISODEP_PROLOGUE_SIZE];
	uint16_t rxBufLen = 0xF0;
	uint8_t err;
	// read bytes
	uint16_t rxLen = rxBufLen;

	uint16_t readBytes = 0;
	uint16_t currentOffset = offset;
	uint16_t currentLength = length;
	while (currentLength > 0)
	{

		switch (Current->dev.nfca.type)
		{
		/*******************************************************************************/
		case RFAL_NFCA_T1T:
			/* To perform presence check, on this example a T1T Read command is used */
			err = rfalT1TPollerRall(Current->dev.nfca.nfcId1, rxBuf,
					rxBufLen, &rxLen);
			if ((rxLen < (length + currentOffset)) || (rxLen < 2))
					{
				// Read all read less than expected length
				// could be because of rxBufLen < tag length
				return NFCTAG_ERROR;
			}
			rxLen = length;
			memcpy(buffer, &rxBuf[currentOffset], length);
			break;

			/*******************************************************************************/
		case RFAL_NFCA_T2T:
			// 4 bytes blocks
			t2tReadReq[1] = currentOffset / 4;
			/* To perform presence check, on this example a T2T Read command is used */
			err = rfalTransceiveBlockingTxRx(t2tReadReq,
					sizeof(t2tReadReq), rxBuf, rxBufLen,
					&rxLen, RFAL_TXRX_FLAGS_DEFAULT,
					rfalConvMsTo1fc(20));
			rxLen -= currentOffset % 4;
			if (rxLen > currentLength)
				rxLen = currentLength;
			memcpy(buffer + readBytes, &rxBuf[currentOffset % 4],
					rxLen);
			break;

			/*******************************************************************************/
		case RFAL_NFCA_T4T:
			t4tReadBinary[2] = (currentOffset) >> 8;
			t4tReadBinary[3] = (currentOffset) & 0xFF;
			t4tReadBinary[4] =
					(currentLength) > rxBufLen ?
							rxBufLen :
							(currentLength);

			err = t4tSendCommand(t4tReadBinary,
					sizeof(t4tReadBinary),
					(rfalIsoDepBufFormat*)&rxBuf, &rxLen);
			if (rxLen > 2)
					{
				rxLen -= 2; // status bytes
				if (rxLen > currentLength)
					rxLen = currentLength;
				memcpy(buffer + readBytes,
						&((rfalIsoDepBufFormat*)rxBuf)->inf[0],
						rxLen);
			} else {
				rxLen = 0;
			}
			break;
		default:
			return NFCTAG_ERROR;
		}
		if ((err != ERR_NONE) || (rxLen == 0))
				{
			return NFCTAG_ERROR;
		} else {
			currentOffset += rxLen;
			currentLength -= rxLen;
			readBytes += rxLen;
		}
	} // while (currentLength > 0)
	return NFCTAG_OK;
}

/* NFCA Tag write data method */
uint8_t BSP_NFCTAG_WriteData_NfcA(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint8_t rxBuf[RFAL_ISODEP_DEFAULT_FSC + RFAL_ISODEP_PROLOGUE_SIZE];
	uint16_t rxLen = 0xF0;
	uint8_t status = 0xFF;
	uint16_t currentOffset = offset;
	uint16_t lastOffset = offset + length;
	uint16_t nbBytesToWrite = length;
	uint8_t cmd[256 + 5 + 10];
	uint8_t nbBytesWritten = 0;
	uint8_t block[10] = {0};

	while (currentOffset < lastOffset)
	{

		switch (Current->dev.nfca.type)
		{
		/*******************************************************************************/
		case RFAL_NFCA_T1T:
			// -2 as H0 & H1 are not considered for block id
			status = rfalT1TPollerWrite(Current->dev.nfca.nfcId1,
					currentOffset - 2, *buffer);
			if (status != ERR_NONE)
				return NFCTAG_ERROR;
			currentOffset++;
			buffer++;
			break;

			/*******************************************************************************/
		case RFAL_NFCA_T2T:
			{
			/* Manage offset and length for block based memories */
			uint8_t blockInternalOffset = currentOffset % 4;
			uint8_t blockInternalLength = 4 - (currentOffset % 4);
			blockInternalLength =
					blockInternalLength > nbBytesToWrite ?
							nbBytesToWrite :
							blockInternalLength;

			if ((blockInternalOffset) || (blockInternalLength % 4))
					{
				// 4 bytes block
				t2tReadReq[1] = currentOffset / 4;
				status = rfalTransceiveBlockingTxRx(t2tReadReq,
						sizeof(t2tReadReq), block, 0xF0,
						&rxLen, RFAL_TXRX_FLAGS_DEFAULT,
						rfalConvMsTo1fc(20));
				if (status != ERR_NONE)
				{
					return NFCTAG_ERROR;
				}
				// remove first byte (tag status)
				memcpy(&block[blockInternalOffset], buffer,
						blockInternalLength);
				buffer += blockInternalLength;
				nbBytesWritten = blockInternalLength;

			} else {
				memcpy(block, buffer, 4);
				buffer += 4;
				nbBytesWritten = 4;
			}

			t2tWriteReq[1] = currentOffset / 4;
			memcpy(&t2tWriteReq[2], block, 4);
			status = rfalTransceiveBlockingTxRx(t2tWriteReq,
					sizeof(t2tWriteReq), rxBuf, 0xF0,
					&rxLen, RFAL_TXRX_FLAGS_DEFAULT,
					rfalConvMsTo1fc(20));
			if (status >= ERR_INCOMPLETE_BYTE)
				status = ERR_NONE;
			if (status != ERR_NONE)
				return NFCTAG_ERROR;
			currentOffset += nbBytesWritten;
		}
			break;

			/*******************************************************************************/
		case RFAL_NFCA_T4T:
			memcpy(cmd, t4tWriteData, sizeof(t4tWriteData));
			cmd[2] = offset >> 8;
			cmd[3] = offset & 0xFF;
			cmd[4] = length;
			memcpy(&cmd[5], buffer, length);
			status = t4tSendCommand(cmd,
					sizeof(t4tWriteData) + length,
					(rfalIsoDepBufFormat*)&rxBuf, &rxLen);
			currentOffset += length;
			if (memcmp(&rxBuf[1], t4Success, sizeof(t4Success)))
					{
				// not a success code
				return NFCTAG_ERROR;
			}
			break;
		}
	}

	if (status == ERR_NONE)
		return NFCTAG_OK;
	else
		return NFCTAG_ERROR;
}

/* NFCA Api */
static BSP_NFCTAG_Protocol_t apiNfcA = {
		.activate = &BSP_NFCTAG_Activate_NfcA,
		.read_data = &BSP_NFCTAG_ReadData_NfcA,
		.write_data = &BSP_NFCTAG_WriteData_NfcA
};

/****************** NFCB *****************************/
/* NFCB Tag activation (Select tag & NDEF file) */
static uint8_t BSP_NFCTAG_Activate_NfcB(void)
{
	uint8_t err;
	rfalNfcbSensbRes sensbRes;
	uint8_t sensbResLen;

	Current->NdefSupport = 0;

	rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
	if (Current->type == BSP_NFCTAG_ST25TB)
			{
		rfalSt25tbPollerInitialize();
		rfalFieldOnAndStartGT();

		err = rfalSt25tbPollerCheckPresence(
				&Current->dev.st25tb.chipID);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		err = rfalSt25tbPollerSelect(Current->dev.st25tb.chipID);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}

	}
	if (Current->type == BSP_NFCTAG_NFCB)
			{
		rfalIsoDepBufFormat rxBuf;
		uint16_t rxBufLen = sizeof(rxBuf);
		rfalNfcbPollerInitialize();

		err = rfalNfcbPollerCheckPresence(RFAL_NFCB_SENS_CMD_ALLB_REQ,
				RFAL_NFCB_SLOT_NUM_1, &sensbRes, &sensbResLen);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}

		rfalIsoDepInitialize();
		err = rfalIsoDepPollBHandleActivation(
				(rfalIsoDepFSxI)RFAL_ISODEP_FSDI_DEFAULT,
				RFAL_ISODEP_NO_DID, RFAL_BR_424, 0x00,
				&Current->dev.nfcb, NULL, 0,
				&Current->proto.isoDep);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		err = t4tSendCommand(t4tSelectNdefApp, sizeof(t4tSelectNdefApp),
				&rxBuf, &rxBufLen);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}

		err = BSP_NFCTAG_T4_SelectFile(0x0001);
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		Current->NdefSupport = 1;
	}
	return NFCTAG_OK;
}

/* NFCB Tag read data method */
static uint8_t BSP_NFCTAG_ReadData_NfcB(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint8_t err;
	uint8_t rxBuf[4];
	if (Current->type == BSP_NFCTAG_ST25TB)
			{
		uint16_t readBytes = 0;
		while (readBytes < length)
		{
			uint16_t currentOffset = (offset + readBytes);
			uint16_t blockAddr = currentOffset / 4;
			// bytes still to be read
			uint32_t remainingBytes = length - readBytes;
			// bytes to read in current block
			uint8_t bytesInBlock = 4 - (currentOffset % 4);
			bytesInBlock = bytesInBlock > remainingBytes ?
					remainingBytes : bytesInBlock;
			err = rfalSt25tbPollerReadBlock(blockAddr,
					(rfalSt25tbBlock*)rxBuf);
			if (err != ERR_NONE)
			{
				return NFCTAG_ERROR;
			}
			memcpy(buffer + readBytes, &rxBuf[currentOffset % 4],
					bytesInBlock);
			readBytes += bytesInBlock;
		}
	} else if (Current->type == BSP_NFCTAG_NFCB) {
		uint8_t rxBuf[RFAL_ISODEP_DEFAULT_FSC
				+ RFAL_ISODEP_PROLOGUE_SIZE];
		uint16_t rxBufLen = 0xF0;
		uint8_t err;
		// read bytes
		uint16_t rxLen = rxBufLen;

		uint16_t readBytes = 0;
		uint16_t currentOffset = offset;
		uint16_t currentLength = length;
		while (currentLength > 0)
		{
			t4tReadBinary[2] = (currentOffset) >> 8;
			t4tReadBinary[3] = (currentOffset) & 0xFF;
			t4tReadBinary[4] =
					(currentLength) > rxBufLen ?
							rxBufLen :
							(currentLength);

			err = t4tSendCommand(t4tReadBinary,
					sizeof(t4tReadBinary),
					(rfalIsoDepBufFormat*)&rxBuf, &rxLen);
			if (rxLen > 2)
					{
				rxLen -= 2; // status bytes
				if (rxLen > currentLength)
					rxLen = currentLength;
				memcpy(buffer + readBytes,
						&((rfalIsoDepBufFormat*)rxBuf)->inf[0],
						rxLen);
			} else {
				rxLen = 0;
			}
			if ((err != ERR_NONE) || (rxLen == 0))
					{
				return NFCTAG_ERROR;
			} else {
				currentOffset += rxLen;
				currentLength -= rxLen;
				readBytes += rxLen;
			}
		}
	}
	return NFCTAG_OK;
}

/* NFCB Tag write data method */
static uint8_t BSP_NFCTAG_WriteData_NfcB(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint8_t rxBuf[0xFF];
	uint16_t rxLen = 0xF0;
	uint8_t status = 0xFF;
	uint16_t currentOffset = offset;
	uint16_t lastOffset = offset + length;
	uint16_t nbBytesToWrite = length;
	uint8_t cmd[256 + 5 + 10];
	uint8_t nbBytesWritten = 0;

	while (currentOffset < lastOffset)
	{
		switch (Current->type)
		{
		case BSP_NFCTAG_ST25TB:
			{
			/* Manage offset and length for block based memories */
			rfalSt25tbBlock block;
			uint8_t blockInternalOffset = currentOffset % 4;
			uint8_t blockInternalLength = 4 - (currentOffset % 4);
			blockInternalLength =
					blockInternalLength > nbBytesToWrite ?
							nbBytesToWrite :
							blockInternalLength;

			if ((blockInternalOffset) || (blockInternalLength % 4))
					{
				// 4 bytes block
				status = rfalSt25tbPollerReadBlock(
						currentOffset / 4, &block);
				if (status != ERR_NONE)
				{
					return NFCTAG_ERROR;
				}

				memcpy(&block[blockInternalOffset], buffer,
						blockInternalLength);
				buffer += blockInternalLength;
				nbBytesWritten = blockInternalLength;

			} else {
				memcpy(&block, buffer, 4);
				buffer += 4;
				nbBytesWritten = 4;
			}

			status = rfalSt25tbPollerWriteBlock(currentOffset / 4,
					(const rfalSt25tbBlock*)&block);

			if (status != ERR_NONE)
				return NFCTAG_ERROR;
			currentOffset += nbBytesWritten;
		}
			break;
		case BSP_NFCTAG_NFCB:
			memcpy(cmd, t4tWriteData, sizeof(t4tWriteData));
			cmd[2] = offset >> 8;
			cmd[3] = offset & 0xFF;
			cmd[4] = length;
			memcpy(&cmd[5], buffer, length);
			status = t4tSendCommand(cmd,
					sizeof(t4tWriteData) + length,
					(rfalIsoDepBufFormat*)&rxBuf, &rxLen);
			currentOffset += length;
			break;
		}
	}

	if (status == ERR_NONE)
		return NFCTAG_OK;
	else
		return NFCTAG_ERROR;

}

/* NFCB Api */
static BSP_NFCTAG_Protocol_t apiNfcB = {
		.activate = &BSP_NFCTAG_Activate_NfcB,
		.read_data = &BSP_NFCTAG_ReadData_NfcB,
		.write_data = &BSP_NFCTAG_WriteData_NfcB
};

/****************** NFCF *****************************/
/* NFCF Tag Select */
static uint8_t BSP_NFCTAG_Activate_NfcF(void)
{
	rfalFieldOff(); /* Turns the Field On and starts GT timer */
	HAL_Delay(500);
	rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
	rfalNfcfPollerInitialize(RFAL_BR_212);

	uint8_t testBuf[32];
	// to do: add a check for NDEF support
	if (CurrentProtocol->read_data(testBuf, 0, 16) != ERR_NONE)
		Current->NdefSupport = 0;
	else
		Current->NdefSupport = 1;

	return NFCTAG_OK;
}

/* NFCF read data method */
static uint8_t BSP_NFCTAG_ReadData_NfcF(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint8_t err;
	uint8_t txRxBuf[0xFF];
	uint16_t rxBufLen = 0xF0;
	// read bytes
	uint16_t rxLen = rxBufLen;
	uint16_t firstBlock = (offset / 16);
	uint16_t lastBlock = (length + offset - 1) / 16;
	uint16_t blockNumber = lastBlock - firstBlock + 1;
	memset(txRxBuf, 0, rxBufLen);

	/* To perform presence check, on this example a T3T Check/Read command is used */
	memcpy(&t3tCheckReq[1], Current->dev.nfcf.sensfRes.NFCID2,
			RFAL_NFCF_NFCID2_LEN); /* Assign device's NFCID for Check command */
	t3tCheckReq[12] = blockNumber;
	memcpy(txRxBuf, t3tCheckReq, sizeof(t3tCheckReq));
	int i = 0;
	int blockId;
	for (blockId = firstBlock; blockId <= lastBlock; blockId++)
			{
		txRxBuf[13 + i * 2] = 0x80;
		txRxBuf[14 + i * 2] = blockId;
		i++;
	}
	err = rfalTransceiveBlockingTxRx(txRxBuf,
			sizeof(t3tCheckReq) + ((blockNumber - 1) * 2), txRxBuf,
			rxBufLen, &rxLen, RFAL_TXRX_FLAGS_DEFAULT,
			rfalConvMsTo1fc(20));
	if (err != ERR_NONE)
	{
		return NFCTAG_ERROR;
	}
	if ((int)((int)rxLen - 13 - (int)(offset % 16)) < (int)length)
			{
		return NFCTAG_RESPONSE_ERROR;
	}

	memcpy(buffer, &txRxBuf[13 + (offset % 16)], length);
	return NFCTAG_OK;
}

/* NFCF write data method */
static uint8_t BSP_NFCTAG_WriteData_NfcF(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint8_t err;
	uint8_t txRxBuf[0xFF];
	uint16_t rxBufLen = 0xF0;
	// read bytes
	uint16_t rxLen = rxBufLen;
	uint16_t firstBlock = (offset / 16);
	uint16_t lastBlock = (length + offset - 1) / 16;

	memcpy(&t3tUpdateReq[1], Current->dev.nfcf.sensfRes.NFCID2,
			RFAL_NFCF_NFCID2_LEN); /* Assign device's NFCID for Check command */

	int currentOffset = offset;
	int blockId;
	for (blockId = firstBlock; blockId <= lastBlock; blockId++)
	{
		int currentLength = 16 - currentOffset % 16;
		currentLength = currentLength > length ? length : currentLength;

		t3tUpdateReq[14] = blockId;

		BSP_NFCTAG_ReadData_NfcF(&t3tUpdateReq[15], blockId * 16, 16);
		memcpy(&t3tUpdateReq[15] + currentOffset % 16, buffer,
				currentLength);

		err = rfalTransceiveBlockingTxRx(t3tUpdateReq,
				sizeof(t3tUpdateReq), txRxBuf, rxBufLen, &rxLen,
				RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(20));
		if (err != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		currentOffset += currentLength;
		length -= currentLength;
		buffer += currentLength;
	}

	return NFCTAG_OK;
}

/* NFCF Api */
static BSP_NFCTAG_Protocol_t apiNfcF = {
		.activate = &BSP_NFCTAG_Activate_NfcF,
		.read_data = &BSP_NFCTAG_ReadData_NfcF,
		.write_data = &BSP_NFCTAG_WriteData_NfcF
};

/****************** NFCV*****************************/
/* NFCV RX buffer length in bytes */
#define BSP_NFCTAG_NFCV_RXBUF_LENGTH 0xFF
/* T5 read single block command */
static uint8_t t5ExtReadSingleBlock[] = {0x22, 0x30, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t vicExtReadSingleBlock[] = {0x2A, 0x20, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
/* T5 read multiple block command */
static uint8_t t5ExtReadMultipleBlock[] = {0x22, 0x33, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t vicExtReadMultipleBlock[] = {0x2A, 0x23, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
/* T5 default block size */
static uint16_t t5BlockSize = 4;
static uint8_t isVicinity = 0;
static uint8_t t5MultipleBlockSupport = 0;

uint8_t BSP_NFCTAG_CheckVicinity()
{
	uint8_t *uid;
	if (Current == NULL)
	{
		return 0;
	}
	if (Current->type != BSP_NFCTAG_NFCV)
			{
		return 0;
	}
	uid = Current->dev.nfcv.InvRes.UID;
	if ((uid[7] == 0xE0) && (uid[6] == 0x02)
			&&
			((uid[5] == 0x44) || (uid[5] == 0x45)
					|| (uid[5] == 0x46) ||
					(uid[5] == 0x2C) || (uid[5] == 0x2D)
					|| (uid[5] == 0x2E) ||
					(uid[5] == 0x5C) || (uid[5] == 0x5D)
					|| (uid[5] == 0x5E) ||
					(uid[5] == 0x4C) || (uid[5] == 0x4D)
					|| (uid[5] == 0x4E)))
			{
		return 1;
	}
	return 0;
}

/* NFCV tag activation (set block size) */
static uint8_t BSP_NFCTAG_Activate_NfcV(void)
{
	if (Current->type == BSP_NFCTAG_NFCV)
			{
		uint8_t status;
		uint8_t rxBuf[16];

		isVicinity = BSP_NFCTAG_CheckVicinity();

		// let's consider it supports read multiple block
		t5MultipleBlockSupport = 1;

		// make sure the field is on
		rfalFieldOnAndStartGT();

		status = rfalNfcvPollerReadSingleBlock(0x22,
				Current->dev.nfcv.InvRes.UID, 0, rxBuf, 16,
				&t5BlockSize);

		// don't count the status byte
		t5BlockSize--;
		if (status == ERR_NONE)
		{
			Current->NdefSupport = 1;

		} else {
			// assume block size is 4
			t5BlockSize = 4;
			Current->NdefSupport = 0;
		}
		return NFCTAG_OK;
	} else {
		Current = NULL;
		return NFCTAG_ERROR;
	}
}

/* NFCV Tag read data */
static uint8_t BSP_NFCTAG_ReadData_NfcV(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{

	int i = 0;
	uint16_t nbBytesRead = 0;
	uint8_t status = ERR_NONE;
	uint8_t *txBuf;
	uint16_t txBufLen;
	uint8_t rxBuf[BSP_NFCTAG_NFCV_RXBUF_LENGTH];

	if ((Current->type != BSP_NFCTAG_NFCV) ||
			(length == 0))
		return NFCTAG_ERROR;

	// make sure the field is on
	rfalFieldOnAndStartGT();

	// stick with readSingleBlock
	while (i < length)
	{
		uint32_t rxLen = length - i;
		if (rxLen > 0xF0)
			rxLen = 0xF0;

		// first try to use readMultipleBlock
		if (t5MultipleBlockSupport)
		{
			if (isVicinity)
			{
				// use extended command
				vicExtReadMultipleBlock[10] = ((offset + i)
						/ t5BlockSize) & 0xFF;
				vicExtReadMultipleBlock[11] = ((offset + i)
						/ t5BlockSize) >> 8;
				vicExtReadMultipleBlock[12] = ((rxLen - 1)
						/ t5BlockSize) & 0xFF;
				txBuf = vicExtReadMultipleBlock;
				txBufLen = sizeof(vicExtReadMultipleBlock);
				memcpy(&txBuf[2], Current->dev.nfcv.InvRes.UID,
						8);
				nbBytesRead = rxLen;
				status = rfalTransceiveBlockingTxRx(
						vicExtReadMultipleBlock,
						txBufLen, rxBuf,
						BSP_NFCTAG_NFCV_RXBUF_LENGTH,
						&nbBytesRead,
						RFAL_TXRX_FLAGS_DEFAULT,
						rfalConvMsTo1fc(20));
			} else if ((offset + length) < 0xFF)
					{
				nbBytesRead = 0xF0;
				status = rfalNfcvPollerReadMultipleBlocks(0x22,
						Current->dev.nfcv.InvRes.UID,
						(offset + i) / t5BlockSize,
						(rxLen - 1) / t5BlockSize,
						rxBuf,
						BSP_NFCTAG_NFCV_RXBUF_LENGTH,
						&nbBytesRead);
			} else {
				// use extended command
				t5ExtReadMultipleBlock[10] = ((offset + i)
						/ t5BlockSize) & 0xFF;
				t5ExtReadMultipleBlock[11] = ((offset + i)
						/ t5BlockSize) >> 8;
				t5ExtReadMultipleBlock[12] = ((rxLen - 1)
						/ t5BlockSize) & 0xFF;
				t5ExtReadMultipleBlock[13] = ((rxLen - 1)
						/ t5BlockSize) >> 8;
				txBuf = t5ExtReadMultipleBlock;
				txBufLen = sizeof(t5ExtReadMultipleBlock);
				memcpy(&txBuf[2], Current->dev.nfcv.InvRes.UID,
						8);
				nbBytesRead = rxLen;
				status = rfalTransceiveBlockingTxRx(
						t5ExtReadMultipleBlock,
						txBufLen, rxBuf,
						BSP_NFCTAG_NFCV_RXBUF_LENGTH,
						&nbBytesRead,
						RFAL_TXRX_FLAGS_DEFAULT,
						rfalConvMsTo1fc(20));
			}
		}
		if ((status != ERR_NONE) || (!t5MultipleBlockSupport))
				{
			// looks like read multiple blocks failed!
			t5MultipleBlockSupport = 0;

			// try read single block
			if (isVicinity)
			{
				vicExtReadSingleBlock[10] = ((offset + i)
						/ t5BlockSize) & 0xFF;
				vicExtReadSingleBlock[11] = ((offset + i)
						/ t5BlockSize) >> 8;
				txBuf = vicExtReadSingleBlock;
				txBufLen = sizeof(vicExtReadSingleBlock);
				nbBytesRead = t5BlockSize;
				memcpy(&txBuf[2], Current->dev.nfcv.InvRes.UID,
						8);
				status = rfalTransceiveBlockingTxRx(
						vicExtReadSingleBlock, txBufLen,
						rxBuf,
						BSP_NFCTAG_NFCV_RXBUF_LENGTH,
						&nbBytesRead,
						RFAL_TXRX_FLAGS_DEFAULT,
						rfalConvMsTo1fc(20));
			} else if (((offset + i) / t5BlockSize) < 0xFF)
					{
				nbBytesRead = t5BlockSize;
				status = rfalNfcvPollerReadSingleBlock(0x22,
						Current->dev.nfcv.InvRes.UID,
						(offset + i) / t5BlockSize,
						rxBuf,
						BSP_NFCTAG_NFCV_RXBUF_LENGTH,
						&nbBytesRead);
			} else {
				t5ExtReadSingleBlock[10] = ((offset + i)
						/ t5BlockSize) & 0xFF;
				t5ExtReadSingleBlock[11] = ((offset + i)
						/ t5BlockSize) >> 8;
				txBuf = t5ExtReadSingleBlock;
				txBufLen = sizeof(t5ExtReadSingleBlock);
				nbBytesRead = t5BlockSize;
				memcpy(&txBuf[2], Current->dev.nfcv.InvRes.UID,
						8);
				status = rfalTransceiveBlockingTxRx(
						t5ExtReadSingleBlock, txBufLen,
						rxBuf,
						BSP_NFCTAG_NFCV_RXBUF_LENGTH,
						&nbBytesRead,
						RFAL_TXRX_FLAGS_DEFAULT,
						rfalConvMsTo1fc(20));
			}
		}
		if ((status != ERR_NONE)
				|| (nbBytesRead < (1 + (offset + i) % 4)))
			return NFCTAG_ERROR;
		// remove status byte & first bytes if offset is not aligned
		nbBytesRead--;
		nbBytesRead -= (offset + i) % t5BlockSize;
		memcpy(buffer + i, &rxBuf[1 + ((offset + i) % t5BlockSize)],
				(nbBytesRead > length) ? length : nbBytesRead);
		i += nbBytesRead;
	}
	return NFCTAG_OK;
}

/* NFCV Tag write data */
static uint8_t BSP_NFCTAG_WriteData_NfcV(uint8_t *buffer, uint32_t offset,
		uint32_t length)
{
	uint32_t currentOffset = offset;
	uint32_t lastOffset = offset + length;
	uint8_t status;
	uint8_t block[16] = {0};
	uint16_t nbBytesRead = 0;
	uint16_t nbBytesToWrite = length;
	uint8_t nbBytesWritten = 0;

	while (currentOffset < lastOffset)
	{
		uint8_t blockInternalOffset = currentOffset % t5BlockSize;
		uint8_t blockInternalLength = t5BlockSize
				- (currentOffset % t5BlockSize);
		blockInternalLength =
				blockInternalLength > nbBytesToWrite ?
						nbBytesToWrite :
						blockInternalLength;

		if ((blockInternalOffset) || (blockInternalLength % 4))
				{
			status = rfalNfcvPollerReadSingleBlock(0x22,
					Current->dev.nfcv.InvRes.UID,
					currentOffset / t5BlockSize, block, 10,
					&nbBytesRead);
			if ((status != ERR_NONE) || (block[0] != 0))
					{
				return NFCTAG_ERROR;
			}
			// remove first byte (tag status)
			memmove(block, &block[1], t5BlockSize);
			memcpy(&block[blockInternalOffset], buffer,
					blockInternalLength);
			buffer += blockInternalLength;
			nbBytesWritten = blockInternalLength;

		} else {
			memcpy(block, buffer, t5BlockSize);
			buffer += t5BlockSize;
			nbBytesWritten = t5BlockSize;
		}

		status = rfalNfcvPollerWriteSingleBlock(0x22,
				Current->dev.nfcv.InvRes.UID,
				currentOffset / t5BlockSize, block,
				t5BlockSize);
		if (status != ERR_NONE)
		{
			return NFCTAG_ERROR;
		}
		currentOffset += nbBytesWritten;
	}
	return NFCTAG_OK;
}

/* NFCV Api */
static BSP_NFCTAG_Protocol_t apiNfcV = {
		.activate = &BSP_NFCTAG_Activate_NfcV,
		.read_data = &BSP_NFCTAG_ReadData_NfcV,
		.write_data = &BSP_NFCTAG_WriteData_NfcV
};

/******************************* Common API ********************************/
/**
 * @brief Generic Tag activation method
 *        Perform required actions depending on the current selected protocol.
 *        E.g.: Run select command, get block size,...
 * @param device the device to run activation on.
 * @returns status, 0 is a success, other values are errors.
 */
uint8_t BSP_NFCTAG_Activate(BSP_NFCTAG_Device_t *device)
{
	// reset Current tag under operation
	Current = NULL;
	// select the protocol
	switch (device->type)
	{
	case BSP_NFCTAG_NFCA:
		CurrentProtocol = &apiNfcA;
		break;
	case BSP_NFCTAG_NFCB:
		case BSP_NFCTAG_ST25TB:
		CurrentProtocol = &apiNfcB;
		break;
	case BSP_NFCTAG_NFCF:
		CurrentProtocol = &apiNfcF;
		break;
	case BSP_NFCTAG_NFCV:
		CurrentProtocol = &apiNfcV;
		break;
	default:
		CurrentProtocol = NULL;
		return NFCTAG_ERROR;
	}

	// set current operating tag with provided instance
	Current = device;

	if ((CurrentProtocol != NULL) && (CurrentProtocol->activate != NULL))
		return CurrentProtocol->activate();
	else
		return NFCTAG_ERROR;

}

/**
 * @brief Generic Tag read method
 *        Perform required actions to read data from the previously activated tag.
 * @param buffer Pointer on the buffer to be used to store the read data.
 * @param offset Address of the tag to be read.
 * @param length Number of bytes to read.
 * @returns status, 0 is a success, other values are errors.
 */
uint8_t BSP_NFCTAG_ReadData(uint8_t *buffer, uint32_t offset, uint32_t length)
{
	if ((CurrentProtocol != NULL) && (CurrentProtocol->read_data != NULL))
		return CurrentProtocol->read_data(buffer, offset, length);
	else
		return NFCTAG_ERROR;
}

/**
 * @brief Generic Tag write method
 *        Perform required actions to write data to the previously activated tag.
 * @param buffer Pointer on the buffer with data to write.
 * @param offset Address of the tag to be written.
 * @param length Number of bytes to write.
 * @returns status, 0 is a success, other values are errors.
 */
uint8_t BSP_NFCTAG_WriteData(uint8_t *buffer, uint32_t offset, uint32_t length)
{
	if ((CurrentProtocol != NULL) && (CurrentProtocol->write_data != NULL))
		return CurrentProtocol->write_data(buffer, offset, length);
	else
		return NFCTAG_ERROR;
}

/**
 * @brief Generic method to get the tag size, in bytes
 *        Perform required actions to get tag size of the previously activated tag.
 * @returns size of the tag in bytes
 * @warning Not yest implemented - always return 512
 * @todo implement for each tag type...
 */
uint16_t BSP_NFCTAG_GetByteSize(void)
{
	return 512;
}

