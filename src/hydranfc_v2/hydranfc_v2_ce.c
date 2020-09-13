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

#include <stdio.h> /* sprintf */
#include "common.h"
#include "ce.h"
#include "string.h"
#include "stdbool.h"
#include "rfal_rf.h"
#include "rfal_isoDep.h"

#include "dispatcher.h"
#include "stdlib.h"

#include "lib_NDEF_URI.h"
#include "lib_NDEF_Email.h"

#include "st25r3916_com.h"
#include "st25r3916_irq.h"

#include "bsp_print_dbg.h"
#include "hydranfc_v2_ce.h"
#include "hydranfc_v2.h"

static uint8_t rxtxFrameBuf[512] __attribute__ ((section(".cmm")));

void dispatcherInterruptHandler(void);

/* Emulated cards param: max NDEF size */
#define NDEF_SIZE 0xFFFE
/* Class byte value 00 */
#define T4T_CLA_00 0x00
/* Select Instruction byte value */
#define T4T_INS_SELECT 0xA4
/* Read Instruction byte value */
#define T4T_INS_READ 0xB0
/* Update Instruction byte value */
#define T4T_INS_UPDATE 0xD6
/* Capacity Container file Id */
#define FID_CC 0xE103
/* NDEF file Id */
#define FID_NDEF 0xE104

/* Emulated card state definition */
enum States
{
    STATE_IDLE                      = 0,
    STATE_APP_SELECTED              = 1,
    STATE_CC_SELECTED               = 2,
    STATE_FID_SELECTED              = 3,
};

/* pointer to the NDEF file buffer */
static uint8_t *ndefFile = NULL;
/* Emulated card state */
static int nState = STATE_IDLE;
/* Selected fiel Id */
static int nSelectedIdx = -1;
/* Number of supported files */
static int nFiles = 2;
/* capacity container file content definition */
static uint8_t ccfile[] = {
0x00,                                // CCLEN high
0x0F,                                // CCLEN low
0x20,                                // Mapping Version
0x00,                                // MLe high: Max R-APDU size
0x7F,                                // MLe low: Max R-APDU size
0x00,                                // MLc high: Max C-APDU size
0x7F,                                // MLc low: Max C-APDU size
0x04,                                // NDEF FCI TLV 0   T
0x06,                                // NDEF FCI TLV 1   L
(FID_NDEF & 0xFF00) >> 8,    // NDEF FCI TLV 2   V: FID
(FID_NDEF & 0x00FF),         // NDEF FCI TLV 3   V: FID
(NDEF_SIZE & 0xFF00) >> 8,   // NDEF FCI TLV 4   V: NDEF size
(NDEF_SIZE & 0x00FF),        // NDEF FCI TLV 5   V: NDEF size
0x00,                                // NDEF FCI TLV 6   V: NDEF Read AC
0x00                                 // NDEF FCI TLV 7   V: NDEF Write AC
};

static uint16_t dispatcherInterruptResults[32];

/* File size array */
static uint32_t pdwFileSize[] = {sizeof(ccfile), NDEF_SIZE};

// sURI_Info is huge!
sURI_Info w_uri = {URI_ID_0x01_STRING, "github.com/hydrabus", ""};

sUserTagProperties user_tag_properties;

/* Helper method to test if a command is present */
static bool cmdFind(uint8_t *cmd, uint8_t *find, uint16_t len)
{
	int i;
  for(i = 0; i < 20; i++)
  {
    if(!memcmp(&cmd[i],find, len))
    {
      return true;
    }
  }
  return false;
}

/* execute the file select command */
static uint16_t t4select(uint8_t *cmdData, uint8_t *rspData)
{
    bool success = false;

    uint8_t aid[] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};

    uint8_t fidCC[] = {FID_CC >> 8 , FID_CC & 0xFF};

    uint8_t fidNDEF[] = {FID_NDEF >> 8, FID_NDEF & 0xFF};
    uint8_t selectFileId[] = {0xA4, 0x00, 0x0C, 0x02, 0x00, 0x01 };

    // Select NDEF App ?
    if(cmdFind( cmdData, aid, sizeof(aid)))
    {
        nState = STATE_APP_SELECTED;
        success = true;
    }else
    // Select CC ?
    if((nState >= STATE_APP_SELECTED) && cmdFind(cmdData, fidCC, sizeof(fidCC)))
    {
        nState = STATE_CC_SELECTED;
        nSelectedIdx = 0;
        success = true;
    }else
    // Select NDEF ?
    if((nState >= STATE_APP_SELECTED) && (cmdFind(cmdData,fidNDEF,sizeof(fidNDEF)) || cmdFind(cmdData,selectFileId,sizeof(selectFileId))))
    {
        nState = STATE_FID_SELECTED;
        nSelectedIdx = 1;
        success = true;
    }else
    {
        nState = STATE_IDLE;
    }

    rspData[0] = (success ? (char)0x90 : 0x6A);
    rspData[1] = (success ? (char)0x00 : 0x82);
    success = true;
    return 2;
}

/* Execute the Read command */
static uint16_t read(uint8_t *cmdData, uint8_t *rspData)
{
    // Cmd: CLA(1) | INS(1) | P1(1).. offset inside file high | P2(1).. offset inside file high | Le(1).. nBytes to read
    // Rsp: BytesRead | SW12

    unsigned short offset = (cmdData[2] << 8) | cmdData[3];
    unsigned short toRead = cmdData[4];

    // any file selected ?
    if(nSelectedIdx < 0 || nSelectedIdx >= nFiles)
    {
        rspData[0] = ((char)0x6A);
        rspData[1] = ((char)0x82);
        return 2;
    }

    // offset + length exceed file size ?
    if((unsigned long)(offset + toRead) > pdwFileSize[nSelectedIdx])
    {
        toRead = pdwFileSize[nSelectedIdx] - offset;
    }

    uint8_t * ppbMemory =  nSelectedIdx == 0 ? ccfile : ndefFile;
    // read data
    memcpy(rspData,&ppbMemory[offset], toRead);

    rspData[toRead] = ((char)0x90);
    rspData[toRead+1] = ((char)0x00);
    return toRead + 2;
}

/* Execute the Update command */
static uint16_t update(uint8_t *cmdData, uint8_t *rspData)
{
  uint32_t offset = (cmdData[2] << 8) | cmdData[3];
  uint32_t length = cmdData[4];

  if(nSelectedIdx != 1)
  {
    rspData[0] = ((char)0x6A);
    rspData[1] = ((char)0x82);
    return 2;
  }

  if((unsigned long)(offset + length) > pdwFileSize[nSelectedIdx])
  {
    rspData[0] = ((char)0x62);
    rspData[1] = ((char)0x82);
    return 2;
  }
  ndefFile = NDEF_Buffer;

  memcpy(ndefFile + offset,&cmdData[5], length);

//  ce_displayNDEF();

  rspData[0] = ((char)0x90);
  rspData[1] = ((char)0x00);
  return 2;
}

uint8_t ul_image[] = {
		0x04, 0x3b, 0x99, 0x2e, 0x0a, 0x9d, 0x32, 0x80, 0x25, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		// roll over bit
		0x04, 0x3b, 0x99, 0x2e, 0x0a, 0x9d, 0x32, 0x80, 0x25, 0x48, 0x00, 0x00
};

// page set at compatibility write first part command
// ul_cwrite_page_set = page set + 1 (0 is page not set)
// after first part is processed, second part can come after any delay (no time constraints)
// or HLTA can be sent
uint8_t ul_cwrite_page_set = 0;

#define CMD_UL_READ 0x30
#define CMD_UL_WRITE 0xA2
#define CMD_UL_CWRITE 0xA0
#define MIF_ACK 0xA
#define MIF_NAK 0x0

/* Main command management function */
uint16_t processCmd(uint8_t *cmdData, uint8_t *rspData, uint16_t cmdLen, bool *card_reset)
{
	uint8_t ul_cwrite_addr = 0;
	bool cwrite_phase2 = false;

	rfalLmState state = rfalListenGetState(NULL, NULL);
	*card_reset = false;

	if (ul_cwrite_page_set) {
		ul_cwrite_addr = ul_cwrite_page_set - 1;
		// todo ul_cwrite_page_set must also be reset at power off etc
		ul_cwrite_page_set = 0;
		cwrite_phase2 = true;
	}

	switch (state)
	{
	case RFAL_LM_STATE_ACTIVE_A:
	case RFAL_LM_STATE_ACTIVE_Ax:
		// 'nodelay' response will come in ~60uS, whereas FDT (Frame Delay Time)
		// from PICC to PCD is at least 87uS
		// in the meantime ACK/NAK timeout is 5ms and Write timeout is 10ms
		// the above are upper and lower bounds to play with timing.
		// add 440 uS
		DelayUs(100);

		if (cmdLen == 2 && cmdData[0] == 0x50 && cmdData[1] == 0x00) {
			// normally we shouldn't get here, but RFAL is funny with its state machine, so
			// I observed several instances of this behaviour
			// todo - sleep here or at tx stage?
			printf_dbg("HLTA in processCmd\r\n");
			*card_reset = true;
			return 0;
		} else if (cwrite_phase2) {
			uint8_t ul_addr = ul_cwrite_addr;

			if (cmdLen != 16) {
				rspData[0] = MIF_NAK; // NAK for invalid format
				*card_reset = true;
				return 4;
			}
			if (ul_addr >= 0x4 && ul_addr <= 0xf) {
				// ordinary write
				memcpy(&ul_image[ul_addr*4], &cmdData[0], 4);
				rspData[0] = MIF_ACK;
				return 4;
			} else if (ul_addr == 0x3) {
				// OTP page - bitwise OR
				ul_image[0x3*4+0] |= cmdData[0];
				ul_image[0x3*4+1] |= cmdData[1];
				ul_image[0x3*4+2] |= cmdData[2];
				ul_image[0x3*4+3] |= cmdData[3];
				rspData[0] = MIF_ACK;
				return 4;
			} else if (ul_addr == 0x2) {
				// todo lock bytes are not supported, so write to page 2 will yield nothing
				rspData[0] = MIF_ACK;
				return 4;
			}
			else {
				rspData[0] = MIF_NAK; // NAK for invalid address
				*card_reset = true;
				return 4;
			}
		} else if (cmdData[0] == CMD_UL_READ) {
			// ul read
			if (cmdLen != 2) {
				rspData[0] = MIF_NAK; // NAK for invalid format
				*card_reset = true;
				return 4;
			}
			int ul_addr = cmdData[1];
			if (ul_addr >= 0 && ul_addr <= 0xf) {
				memcpy(&rspData[0], &ul_image[ul_addr*4], 16);
				return 8*16;
			}
			else {
				rspData[0] = MIF_NAK; // NAK for invalid address
				*card_reset = true;
				return 4;
			}
		} else if (cmdData[0] == CMD_UL_WRITE) {
			// ul write
			if (cmdLen != 6) {
				rspData[0] = MIF_NAK; // NAK for invalid format
				*card_reset = true;
				return 4;
			}
			uint8_t ul_addr = cmdData[1];
			if (ul_addr >= 0x4 && ul_addr <= 0xf) {
				// ordinary write
				memcpy(&ul_image[ul_addr*4], &cmdData[2], 4);
				rspData[0] = MIF_ACK;
				return 4;
			} else if (ul_addr == 0x3) {
				// OTP page - bitwise OR
				ul_image[0x3*4+0] |= cmdData[2];
				ul_image[0x3*4+1] |= cmdData[3];
				ul_image[0x3*4+2] |= cmdData[4];
				ul_image[0x3*4+3] |= cmdData[5];
				rspData[0] = MIF_ACK;
				return 4;
			} else if (ul_addr == 0x2) {
				// todo lock bytes are not supported, so write to page 2 will yield nothing
				rspData[0] = MIF_ACK;
				return 4;
			}
			else {
				rspData[0] = MIF_NAK; // NAK for invalid address
				*card_reset = true;
				return 4;
			}
		} else if (cmdData[0] == CMD_UL_CWRITE) {
			// ul compatibility write - part 1
			if (cmdLen != 2) {
				rspData[0] = MIF_NAK; // NAK for invalid format
				*card_reset = true;
				return 4;
			}
			if (cmdData[1] >= 0x2 && cmdData[1] <= 0xf) {
				// page num is valid - save it
				ul_cwrite_page_set = cmdData[1] + 1;
				rspData[0] = MIF_ACK;
				return 4;
			} else {
				rspData[0] = MIF_NAK; // NAK for invalid address
				*card_reset = true;
				return 4;
			}

		} else {
			printf_dbg("rx %x... ignored \r\n", cmdData[0]);
			// todo check if it makes sense to stay in active state
			*card_reset = true;
			return 0;
		}
		break;
	case RFAL_LM_STATE_CARDEMU_4A:
		if(cmdData[0] == T4T_CLA_00)
		{
			switch(cmdData[1])
			{
			case T4T_INS_SELECT:
				return 8*t4select(cmdData, rspData);

			case T4T_INS_READ:
				return 8*read(cmdData, rspData);

			case T4T_INS_UPDATE:
				return 8*update(cmdData, rspData);

			default:
				break;
			}
		}

		// Function not supported ..
		rspData[0] = ((char)0x68);
		rspData[1] = ((char)0x00);
		return 8*2;
	case RFAL_LM_STATE_SLEEP_A:
		break;
	default:
		printf_dbg("processCmd unhandled LGS %d\r\n", state);
		break;
	}

	return 0;
}

static const uint8_t dispatcherInterruptResultRegs[32]=
{
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, /* 1st byte */
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, /* 2nd byte */
    ST25R3916_REG_CAPACITANCE_MEASURE_RESULT,
    ST25R3916_REG_PHASE_MEASURE_RESULT,
    ST25R3916_REG_AMPLITUDE_MEASURE_RESULT,0xff,0xff,0xff,0xff,0xff, /* 3rd byte */
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, /* 4th byte */
};

void dispatcherInterruptHandler()
{
    int i;
    uint8_t val;
    uint16_t isrs;

    for (i=0; i<32; i++)  /* !!!!!!!!!!!!!!!!! */
    {
        if(dispatcherInterruptResultRegs[i] >= 0x40) continue;
        if(!st25r3916GetInterrupt(1UL<<i)) continue;

        isrs = dispatcherInterruptResults[i] >> 8;
        if (isrs < 255) isrs++;

        st25r3916ReadRegister(dispatcherInterruptResultRegs[i], &val);

        dispatcherInterruptResults[i] = (isrs<<8) | val;
    }
}

static void ceRun(t_hydra_console *con)
{
	ReturnCode err = ERR_NONE;
	uint16_t dataSize;
	int i;
	bool is_card_reset_needed = false;

	dispatcherInterruptHandler(); // 2.5 uS

	rfalWorker(); // |
	ceHandler();  // | 130 uS in idle

	dataSize = sizeof(rxtxFrameBuf);
//	rxSize = 512;
	err = ceGetRx(CARDEMULATION_CMD_GET_RX_A, rxtxFrameBuf, &dataSize);

	if(err == ERR_NONE)
	{
		printf_dbg("rx:");
		for (i = 0; i < (dataSize > 16? 16 : dataSize); i++)
			printf_dbg(" %02X", rxtxFrameBuf[i]);
		printf_dbg("\r\n");

		dataSize = processCmd(rxtxFrameBuf, rxtxFrameBuf, dataSize, &is_card_reset_needed);
		err = ceSetTx(CARDEMULATION_CMD_SET_TX_A, rxtxFrameBuf, dataSize, is_card_reset_needed);

		if(err != ERR_NONE) {
			printf_dbg("ceSetTx err %d\r\n", err);
		}
		// dataSize in bits after processCmd()
		dataSize = rfalConvBitsToBytes(dataSize);

		printf_dbg("tx:");
		for (i = 0; i < (dataSize > 16? 16 : dataSize); i++)
			printf_dbg(" %02X", rxtxFrameBuf[i]);
		printf_dbg("\r\n");

	}
	else
	{
		switch(err)
		{
		case ERR_NOTFOUND:
		case ERR_BUSY:
		case ERR_SLEEP_REQ:
		case ERR_LINK_LOSS:
			break;
		default:
			printf_dbg("ceGetRx err %d\r\n", err);
			break;
		}
	}
	//D1_OFF;
	//D3_OFF;
	//D4_OFF;
}


void hydranfc_ce_common(t_hydra_console *con)
{
	uint16_t length = 0;
	ReturnCode err = ERR_NONE;

	// NDEF/ ISO Level 4
	uint8_t startCmd[] = {
			CARDEMULATION_MODE_NDEF,                          // command : not used!
			0x04, 0x0E,                                       // 11 bytes data
			0x07,                                             // 7 bytes UIDs
			0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x00, 0x00, 0x00,         // UID
			0x44, 0x00,                                       // ATQA (04 00 for 4 byte uid, 44 00 for 7 byte uid)
			0x20,                                             // SAK (00 - UL, 08 - classic, 20 - ISO L4)
			0x04, 0x00, 0x04, 0x00,                           // disable TypeB & Felica
			0x04, 0x07,                                       // ATS length 7
			0x08, 0x0A, 0x00, 0x00, 0x02, 's', 't'            // FSCI, FWI, SFGI, DRI, DSI, hist length, hist...
	};
	// ats = 07 78 00 80 00 73 74 00

	if (user_tag_properties.uri != NULL) {
		// safe strcpy
		snprintf(w_uri.URI_Message, sizeof(w_uri.URI_Message) - 1, "%s", (char *)user_tag_properties.uri);
	}
	NDEF_PrepareURIMessage(&w_uri, &NDEF_Buffer[2], &length);
	NDEF_Buffer[0] = length >> 8;
	NDEF_Buffer[1] = length & 0xFF;
	ndefFile = NDEF_Buffer;

	// adjust params according to user setup
	// startCmd is in TLV format, so no fixed offsets to the data, hence ugly hardcode
	if (user_tag_properties.sak_len != 0) {
		memcpy(&startCmd[16], user_tag_properties.sak, user_tag_properties.sak_len);
	}
	if (user_tag_properties.uid_len != 0) {
		startCmd[3] = user_tag_properties.uid_len;
		memcpy(&startCmd[4], user_tag_properties.uid, user_tag_properties.uid_len);
		switch (user_tag_properties.uid_len)
		{
		case 4:
			startCmd[14] = 0x04;
			break;
		case 7:
			startCmd[14] = 0x44;
			break;
		default:
			break;
		}
	}

	rfalFieldOff();
	ceInitalize();
	rfalIsoDepInitialize();

	err = ceStart(startCmd, sizeof(startCmd));
	if (err == ERR_NONE)
	{
		cprintf(con, "CE started. Press user button to stop.\r\n");

		while (!hydrabus_ubtn()) {
			ceRun(con);
			chThdYield();
		}

		ceStop();
		cprintf(con, "CE finished\r\n");
	}
	else
	{
		cprintf(con, "CE failed: %d\r\n", err);
	}
}


