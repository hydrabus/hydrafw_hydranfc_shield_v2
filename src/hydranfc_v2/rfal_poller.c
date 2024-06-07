/*
 * HydraBus/HydraNFC v2
 *
 * Copyright (C) 2020-2024 Benjamin VERNOUX
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

// Generic includes
#include <stdio.h> /* sprintf */
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// reader includes
#include "st_errno.h"
#include "platform.h"
#include "st25r.h"
/*
#include "rfal_nfca.h"
#include "rfal_nfcb.h"
#include "rfal_nfcf.h"
#include "rfal_nfcv.h"
#include "rfal_st25tb.h"
#include "rfal_isoDep.h"
#include "rfal_nfcDep.h"
*/
#include "rfal_analogConfig.h"
#include "lib_wrapper.h"


#include "utils.h"
#include "st25r3916.h"
#include "rfal_dpo.h"

#include "bsp_uart.h"

#include "rfal_poller.h"

/*
// tag content
#include "lib_NDEF.h"
#include "lib_NDEF_URI.h"

#include "st25r.h"
#include "TruST25.h"
#include "ndef_display.h"
*/

#define FREEZE_DISPLAY 1
/*
******************************************************************************
* GLOBAL DEFINES
******************************************************************************
*/
#define RFAL_POLLER_DEVICES      4    /* Number of devices supported */

#define RFAL_POLLER_FOUND_NONE   0x00  /* No device found Flag        */
#define RFAL_POLLER_FOUND_A      0x01  /* NFC-A device found Flag     */
#define RFAL_POLLER_FOUND_B      0x02  /* NFC-B device found Flag     */
#define RFAL_POLLER_FOUND_F      0x04  /* NFC-F device found Flag     */
#define RFAL_POLLER_FOUND_V      0x08  /* NFC-V device Flag           */
#define RFAL_POLLER_FOUND_ST25TB 0x10  /* ST25TB device found flag */

/*
******************************************************************************
* GLOBAL TYPES
******************************************************************************
*/

/*! Main state                                                                            */
typedef enum {
	RFAL_POLLER_STATE_INIT                =  0,  /* Initialize state            */
	RFAL_POLLER_STATE_TECHDETECT          =  1,  /* Technology Detection state  */
	RFAL_POLLER_STATE_COLAVOIDANCE        =  2,  /* Collision Avoidance state   */
	RFAL_POLLER_STATE_DEACTIVATION        =  9   /* Deactivation state          */
} RfalPollerState;

/*! Device interface */
typedef enum {
	RFAL_POLLER_INTERFACE_RF     = 0,            /* RF Frame interface */
	RFAL_POLLER_INTERFACE_ISODEP = 1,            /* ISO-DEP interface */
	RFAL_POLLER_INTERFACE_NFCDEP = 2             /* NFC-DEP interface */
} RfalPollerRfInterfaceEnum_t;

/*! Device struct containing all its details */
typedef struct {
	BSP_NFCTAG_Protocol_Id_t type; /* Device's type */
	union {
		rfalNfcaListenDevice nfca; /* NFC-A Listen Device instance */
		rfalNfcbListenDevice nfcb; /* NFC-B Listen Device instance */
		rfalNfcfListenDevice nfcf; /* NFC-F Listen Device instance */
		rfalNfcvListenDevice nfcv; /* NFC-V Listen Device instance */
		rfalSt25tbListenDevice st25tb; /* NFC-B Listen Device instance */
	} dev; /* Device's instance */
	union {
		rfalIsoDepDevice isoDep; /* ISO-DEP instance */
		rfalNfcDepDevice nfcDep; /* NFC-DEP instance */
	} proto;                                            /* Device's protocol */
	uint8_t NdefSupport;
	RfalPollerRfInterfaceEnum_t rfInterface;           /* Device's interface */
} RfalPollerRfInterfaceDevice_t;

/** Detection mode */
typedef enum {
	DETECT_MODE_POLL = 0,     /** Continuous polling for tags */
	DETECT_MODE_WAKEUP = 1,   /** Waiting for the ST25R3916 wakeup detection */
	DETECT_MODE_AWAKEN = 2    /** Awaken by the ST25R3916 wakeup feature */
} detectMode_t;

/** NFC Card Type */
typedef enum {
	NFC_T_NONE = 			0,
	NFC_T_MFCLASSIC_1K =	BIT0,
	NFC_T_MFCLASSIC_1K_2K =	BIT1,
	NFC_T_MFCLASSIC_4K =	BIT2,
	NFC_T_MFMINI = 			BIT3,
	NFC_T_MFDESFIRE = 		BIT4,
	NFC_T_NTAG424DNA =		BIT5,
	NFC_T_MFPLUS = 			BIT6,
	NFC_T_MFULTRALIGHT = 	BIT7,
	NFC_T_OTHER = 			BIT8
} nfc_card_type_t;

/*
 ******************************************************************************
 * LOCAL VARIABLES
 ******************************************************************************
 */
static uint8_t                 gDevCnt;                                 /* Number of devices found */
static RfalPollerRfInterfaceDevice_t gDevList[RFAL_POLLER_DEVICES];     /* Device List */
static RfalPollerState  gState;                                  /* Main state */
static uint8_t                 gTechsFound;                             /* Technologies found bitmask */
RfalPollerRfInterfaceDevice_t        *gActiveDev;                       /* Active device pointer */

static bool instructionsDisplayed = false;                              /* Keep track of demo instruction display */
static detectMode_t detectMode = DETECT_MODE_POLL;                      /* Current tag detection method */
static rfalBitRate rxbr,txbr;                                           /* Detected tag bitrate information */

#if 0
static const uint16_t yBox = 24;                                        /* Detected tag box margin*/
static const uint16_t boxHeight = 45;                                   /* Detected tag box height */
static const uint16_t boxSpace = 4;                                     /* Detected tag box spacing */
static uint16_t lineY[] = {75, 100, 116, 141, 157, 182, 192};           /* Tag information line position */

static char type1Str[] =  "Type 1 Tag";                                 /* NFC Forum Tag Type1 string */
static char type2Str[] =  "Type 2 Tag";                                 /* NFC Forum Tag Type2 string */
static char type3Str[] =  "Type 3 Tag";                                 /* NFC Forum Tag Type3 string */
static char type4aStr[] = "Type 4A Tag";                                /* NFC Forum Tag Type4A string */
static char type4bStr[] = "Type 4B Tag";                                /* NFC Forum Tag Type4B string */
static char type5Str[] =  "Type 5 Tag";                                 /* NFC Forum Tag Type5string */
static char iso15693Str[] =  "Iso15693 Tag";                            /* iso15693 Tag string */
static char st25tbStr[] = "ST25TB Tag";                                 /* ST25TB tag string */
static char iso14443aStr[] = "Iso14443A Tag";                           /* Iso14443A tag string */
static char felicaStr[] = "Felica Tag";                                 /* Felica tag string */

static const char *dpoProfile[] = {"Full power", "Med. power", "Low power "}; /* Dynamic Power Output profile strings*/
static uint16_t lastRssi;                                               /* RSSI history value */
static uint16_t uidLine = 1;                                            /* Line Id to display Tag UID */
static uint16_t hbrLine = 3;                                            /* Line Id to display High Bitrate info */
static uint16_t rssiLine = 5;                                           /* Line Id to display RSSI info */
static uint16_t rssiBarLine = 6;                                        /* Line Id to display RSSI info as a bar */
static uint16_t xMargin = 15;                                           /* Tag information margin */
static uint32_t timeRssi = 0;                                           /* Use to detect long latency in tag response */
#endif

/*
******************************************************************************
* LOCAL FUNCTION PROTOTYPES
******************************************************************************
*/
static bool RfalPollerTechDetection(t_hydra_console *con, nfc_technology_t nfc_tech);
static bool RfalPollerCollResolution(t_hydra_console *con);
static bool RfalPollerDeactivate(void);
static void RfalPollerRun(t_hydra_console *con, nfc_technology_t nfc_tech);
static nfc_card_type_t detect_nfc_card_type(t_hydra_console *con, uint8_t* uid, uint8_t uid_len, uint16_t atqa, uint8_t sak);
/*
void cprintf(t_hydra_console *con, const char *fmt, ...)
{
	mode_config_proto_t* proto = &con->mode->proto;
	va_list va_args;
	int real_size;
#define cprintf_BUFF_SIZE (511)
	char cprintf_buff[cprintf_BUFF_SIZE+1];

	va_start(va_args, fmt);
	real_size = vsnprintf(cprintf_buff, cprintf_BUFF_SIZE, fmt, va_args);
	va_end(va_args);

	bsp_uart_write_u8(proto->dev_num, cprintf_buff, real_size);
}
*/

/* Control Radio Button display */
static void setRadio(t_hydra_console *con)
{
	(void)(con);
	/*
	if(detectMode == DETECT_MODE_POLL)
	{
		cprintf(con, "detectMode=DETECT_MODE_POLL=%d\r\n", detectMode);
	} else {
		cprintf(con, "detectMode=%d\r\n", detectMode);
	}
	*/
}

void nfc_technology_to_str(nfc_technology_t nfc_tech, nfc_technology_str_t* str_tag)
{
	switch(nfc_tech) {
	case NFC_ALL:
		strcpy(str_tag->str, "A/B/V/F");
		break;
	case NFC_A:
		strcpy(str_tag->str, "A");
		break;
	case NFC_B:
		strcpy(str_tag->str, "B");
		break;
	case NFC_ST25TB:
		strcpy(str_tag->str, "ST25TB");
		break;
	case NFC_V:
		strcpy(str_tag->str, "V");
		break;
	case NFC_F:
		strcpy(str_tag->str, "F");
		break;
	default:
		strcpy(str_tag->str, "Unknown");
		break;
	}
}

/* Control display when no tag is detected (instruction + radio button) */
void tagDetectionNoTag(t_hydra_console *con, nfc_technology_t nfc_tech)
{
	nfc_technology_str_t tag;
	setRadio(con);
	instructionsDisplayed = true;
	nfc_technology_to_str(nfc_tech, &tag);
	cprintf(con, "Place NFC-%s tag(s) above the antenna...(Press UBTN to exit)\r\n", tag.str);
}

#if 0
/* Helper method to define tag information position */
uint16_t yLine(uint8_t l)
{
	return lineY[l];
}

/* Helper method to get the tag type as a string */
static char* getTypeStr(void)
{
	char* retPtr = NULL;
	// only for Felica check
	uint8_t buffer[16];
	switch(gActiveDev->type) {
	case BSP_NFCTAG_NFCA:
		switch (gActiveDev->dev.nfca.type) {
		case RFAL_NFCA_T1T:
			retPtr = type1Str;
			break;
		case RFAL_NFCA_T2T:
			retPtr = type2Str;
			break;
		case RFAL_NFCA_T4T:
			if(gActiveDev->NdefSupport)
				retPtr = type4aStr;
			else
				retPtr = iso14443aStr;
		case RFAL_NFCA_NFCDEP:
			/* TODO RFAL_NFCA_NFCDEP */
			break;
		case RFAL_NFCA_T4T_NFCDEP:
			/* TODO RFAL_NFCA_T4T_NFCDEP */
			break;
		}
		break;
	case BSP_NFCTAG_NFCB:
		retPtr = type4bStr;
		break;
	case BSP_NFCTAG_ST25TB:
		retPtr = st25tbStr;
		break;
	case BSP_NFCTAG_NFCF:
		if(BSP_NFCTAG_ReadData(buffer,0,1) == ERR_NONE)
			retPtr = type3Str;
		else
			retPtr = felicaStr;
		break;
	case BSP_NFCTAG_NFCV:
		if((!gActiveDev->NdefSupport) || (BSP_NFCTAG_CheckVicinity())) {
			retPtr = iso15693Str;
		} else {
			retPtr = type5Str;
		}
		break;
	}
	return retPtr;
}
#endif


/* Helper method to get the current tag bitrate as numerical b/s */
static uint16_t getSpeedVal(rfalBitRate br)
{
	if(br <= RFAL_BR_848)
		return 106 * (1<<br);
	else if (br == RFAL_BR_52p97 )
		return 52;
	else if (br == RFAL_BR_26p48)
		return 26;
	else
		return 00;
}

/* Helper method to get the current tag bitrate as a string */
static void getSpeedStr(char *retPtr)
{
	rfalGetBitRate(&txbr,&rxbr);

	sprintf(retPtr, "%d-%d kb/s",
	        getSpeedVal(rxbr),
	        getSpeedVal(txbr));
}

/* When a tag is slected, dispays the current DPO profile */
#ifndef FREEZE_DISPLAY
static void displayDpo(rfalDpoEntry *currentDpo)
{
	const char *txt;
	uint16_t rfoX = 170;
	BSP_LCD_SetFont(&Font16);
	uint16_t rfoWidth = 10*BSP_LCD_GetFont()->Width +20;
	uint16_t rfoTxtX;

	if(currentDpo != NULL) {
		BSP_LCD_SetColors(BSP_LCD_FadeColor(LCD_COLOR_BLUEST,LCD_COLOR_LIGHTBLUE,4,currentDpo->rfoRes),LCD_COLOR_WHITE);
		txt = dpoProfile[currentDpo->rfoRes];
		rfoTxtX = rfoX + (rfoWidth - strlen(txt)*BSP_LCD_GetFont()->Width) / 2;

		BSP_LCD_DrawRectangleWithRoundCorner(rfoX,
		                                     yLine(0)-4 - 5,
		                                     rfoWidth,
		                                     BSP_LCD_GetFont()->Height+14,
		                                     10);
		BSP_LCD_DisplayStringAt(rfoTxtX,yLine(0),(uint8_t*)txt,LEFT_MODE);

		BSP_LCD_SetFont(&Font22);
		Menu_SetStyle(PLAIN);
	}
}
#endif

/* Callback function used to detect long latenncy in tag response, displays "Wait..." and avoid display freeze */
#ifndef FREEZE_DISPLAY
static uint8_t waitRssi(void)
{
	static uint8_t checkTouch = 0;
	Menu_Position_t touch;

	uint16_t rssiX = xMargin + 5 * BSP_LCD_GetFont()->Width;
	uint32_t delay = ((int32_t) HAL_GetTick() - (int32_t)timeRssi);
	if(delay > 1000) {
		BSP_LCD_DisplayStringAt(rssiX,yLine(rssiLine),(uint8_t*)"Wait...  ",LEFT_MODE);
		lastRssi = 0;
	}
	if(checkTouch++ > 50) {
		checkTouch = 0;
		Menu_ReadPosition(&touch);
		if((touch.Sel) && (touch.Y > 60) )
			// exit
			return 0;
	}
	// continue
	return 1;
}
#endif

/* Tag information Tab initial callback function */
#ifndef FREEZE_DISPLAY
static Menu_Callback_Status_t tabFirstTagReadInfo(void)
{
	char line[40] = "";

	clearTab();
	uint8_t nbUidDigit = 8;
	uint8_t *devUid;

	char speedStr[20];
	// re-init lastRssi at dummy value != 0
	lastRssi = 1;
	BSP_NFCTAG_WaitingCb = &waitRssi;

	getDevUid(&devUid,&nbUidDigit);
	for(int d = 0; d < nbUidDigit; d++) {
		if(gActiveDev-> type != BSP_NFCTAG_NFCV)
			sprintf(line,"%s%02X:",line,devUid[d]);
		else
			// for type V invert UID
			sprintf(line,"%s%02X:",line,devUid[nbUidDigit - d - 1]);
	}
	// remove last colon
	line[strlen(line)-1] = '\0';

	Menu_SetStyle(PLAIN);
	BSP_LCD_SetFont(&Font16);

	BSP_LCD_DisplayStringAt(xMargin,yLine(0), (uint8_t*)getTypeStr(),LEFT_MODE);

	BSP_LCD_DisplayStringAt(xMargin,yLine(uidLine),(uint8_t*)"UID:",LEFT_MODE);
	BSP_LCD_DisplayStringAt(xMargin + 1 * Menu_GetFontWidth(),yLine(uidLine+1),(uint8_t*)line,LEFT_MODE);


	BSP_LCD_DisplayStringAt(xMargin,yLine(hbrLine),(uint8_t*)"Speed:",LEFT_MODE);
	getSpeedStr(speedStr);
	BSP_LCD_DisplayStringAt(xMargin + 1 * Menu_GetFontWidth(),yLine(hbrLine+1),(uint8_t*)speedStr,LEFT_MODE);


	BSP_LCD_DisplayStringAt(xMargin,yLine(rssiLine),(uint8_t*)"RSSI:",LEFT_MODE);

#ifdef ENABLE_TRUST25_SIGNATURE_VERIFICATION
	uint16_t trust25Line = 3;
	TruST25_Status_t TruST25_valid = TruST25_SignatureVerification(gActiveDev->type,gActiveDev->dev.nfca.type,devUid,nbUidDigit);
	if(TruST25_valid == TRUST25_VALID) {
		BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
		BSP_LCD_SetTextColor(LCD_COLOR_DARKGREEN);
		BSP_LCD_DisplayPicture(170,yLine(trust25Line),Success);
		BSP_LCD_DisplayStringAt(205,yLine(trust25Line),(uint8_t*)"TruST25",LEFT_MODE);
		BSP_LCD_DisplayStringAt(205,yLine(trust25Line)+16,(uint8_t*)"Validated",LEFT_MODE);
		Menu_SetStyle(PLAIN);
	} else if (TruST25_valid == TRUST25_INVALID) {
		BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
		BSP_LCD_SetTextColor(LCD_COLOR_RED);
		BSP_LCD_DisplayPicture(170+10,yLine(trust25Line),Error);
		BSP_LCD_DisplayStringAt(205+10,yLine(trust25Line),(uint8_t*)"TruST25",LEFT_MODE);
		BSP_LCD_DisplayStringAt(205+10,yLine(trust25Line)+16,(uint8_t*)"Error!",LEFT_MODE);
		Menu_SetStyle(PLAIN);
	}
#endif

	BSP_LCD_SetFont(&Font22);

	return MENU_CALLBACK_CONTINUE;
}
#endif

/* Tag Information Tab callback: displays the RSSI & DPO profile */
#ifndef FREEZE_DISPLAY
static Menu_Callback_Status_t tabTagReadInfo(void)
{
	uint8_t buffer[4];
	uint16_t rssi = 0;
	uint16_t rssiLen = 0;
	uint16_t front,back;
	static uint16_t execCount = 0;
	uint8_t status;


	if(execCount++ > 100) {
		execCount = 0;
		return MENU_CALLBACK_CONTINUE;
	}
	BSP_LCD_SetFont(&Font16);
	uint16_t rssiX = xMargin + BSP_LCD_GetFont()->Width;
	uint16_t rssi_max = 280 - 2*BSP_LCD_GetFont()->Width;


	timeRssi = HAL_GetTick();
	status = BSP_NFCTAG_ReadData(buffer,0,1);
	rfalGetTransceiveRSSI( &rssi );
	// with short frames, at High Bit Rate, rssi computation may not be relevant
	if((status == NFCTAG_OK) && (rssi == 0)) {
		status = BSP_NFCTAG_ReadData(buffer,0,8);
		rfalGetTransceiveRSSI( &rssi );
	}
	if((status != NFCTAG_OK) && (status != NFCTAG_RESPONSE_ERROR)) {
		// try to re-activate
		rfalFieldOff();
		HAL_Delay(5);
		timeRssi = HAL_GetTick();
		BSP_NFCTAG_Activate((BSP_NFCTAG_Device_t *)gActiveDev);
		rfalGetTransceiveRSSI( &rssi );
	}

	rssiLen = ceil ( (float)(rssi_max * rssi) / (float)0x550);
	rssiLen = rssiLen > rssi_max ? rssi_max : rssiLen;

	// don't update rssi display if rssi has not been measured
	if ((status == NFCTAG_OK) && (rssi == 0)) {
		rssiLen = lastRssi;
	}


	Menu_SetStyle(PLAIN);
	if((rssiLen == 0) && (lastRssi != 0)) {
		BSP_LCD_GetColors(&front,&back);
		BSP_LCD_SetColors(back,front);
		BSP_LCD_FillRect(rssiX + 6,yLine(rssiBarLine) + 6,rssi_max,2);
		BSP_LCD_SetColors(front,back);
		BSP_LCD_DisplayStringAt(rssiX+ 4 * BSP_LCD_GetFont()->Width,yLine(rssiLine),(uint8_t*)"Link lost",LEFT_MODE);
	} else if (rssiLen != 0) {
		if(lastRssi == 0)
			BSP_LCD_DisplayStringAt(rssiX+ 4 * BSP_LCD_GetFont()->Width,yLine(rssiLine),(uint8_t*)"         ",LEFT_MODE);
		char rssiTxt[50];
		sprintf(rssiTxt,"       ");
		BSP_LCD_DisplayStringAt(rssiX+ 4 * BSP_LCD_GetFont()->Width,yLine(rssiLine),(uint8_t*)rssiTxt,LEFT_MODE);

		BSP_LCD_GetColors(&front,&back);
		BSP_LCD_SetColors(back,front);
		BSP_LCD_FillRect(BSP_LCD_GetXSize()/2 - rssi_max/2,yLine(rssiBarLine) + 6,rssi_max/2 - lastRssi/2,2);
		BSP_LCD_FillRect(BSP_LCD_GetXSize()/2 + lastRssi/2 + 6,yLine(rssiBarLine) + 6,rssi_max/2 - lastRssi/2,2);
		BSP_LCD_SetColors(front,back);
		BSP_LCD_FillRect(BSP_LCD_GetXSize()/2 - rssiLen/2,yLine(rssiBarLine) + 6, rssiLen,2);
	}

	lastRssi = rssiLen;

	/* DPO */
	displayDpo(rfalDpoGetCurrentTableEntry());

	BSP_LCD_SetFont(&Font22);

	return MENU_CALLBACK_CONTINUE;
}
#endif

/* Tag content Tab initial callback: display NDEF tag content*/
#ifndef FREEZE_DISPLAY
static Menu_Callback_Status_t tabFirstTagReadNdef(void)
{
	char txt[50] = {0};

	uint8_t status;
	uint16_t ndefLen = 0;
	uint8_t* ndefBuffer = NULL;
	clearTab();

	Menu_SetStyle(PLAIN);
	BSP_LCD_SetFont(&Font16);

	// disable NFCTAG loop callback
	BSP_NFCTAG_WaitingCb = NULL;

	// reset start of NDEF buffer
	NDEF_getNDEFSize(&ndefLen);
	if(ndefLen > sizeof(NDEF_Buffer)) {
		ndefBuffer = (uint8_t *)malloc(ndefLen+2);
		if(ndefBuffer == NULL) {
			sprintf(txt, "NDEF is too big!");
			BSP_LCD_DisplayStringAt(0,102,(uint8_t*)txt,CENTER_MODE);
			status = NDEF_ERROR;
		}
	} else {
		ndefBuffer = NDEF_Buffer;
	}
	memset(ndefBuffer,0,100);
	status = NDEF_ReadNDEF(ndefBuffer);
	if((status == NDEF_OK) && ndefLen) {
		if(displayNdef(ndefBuffer) != NDEF_OK) {
			sprintf(txt, "Cannot identify NDEF record");
			Menu_SetStyle(PLAIN);
			BSP_LCD_SetFont(&Font16);
			BSP_LCD_DisplayStringAt(0,70,(uint8_t*)txt,CENTER_MODE);
			char data_str[30] = {0};
			status = BSP_NFCTAG_ReadData(ndefBuffer,0,16);
			if(status == NFCTAG_OK) {
				for(int data_index = 0; data_index < 4 ; data_index++) {
					sprintf(data_str,"0x%02X, 0x%02X, 0x%02X, 0x%02X",ndefBuffer[data_index*4],
					        ndefBuffer[data_index*4+1],
					        ndefBuffer[data_index*4+2],
					        ndefBuffer[data_index*4+3]);
					BSP_LCD_DisplayStringAt(0,86 + (data_index * 16),(uint8_t*)data_str,CENTER_MODE);
				}
			} else {
				sprintf(txt, "Cannot read data");
				BSP_LCD_DisplayStringAt(0,102,(uint8_t*)txt,CENTER_MODE);
			}
		}

	} else {
		if(status == NDEF_ERROR_MEMORY_INTERNAL) {
			sprintf(txt, "NDEF is too big");
		} else {
			sprintf(txt, "Cannot read NDEF");
		}

		char data_str[30] = {0};
		status = BSP_NFCTAG_ReadData(NDEF_Buffer,0,16);

		Menu_SetStyle(PLAIN);
		BSP_LCD_SetFont(&Font16);
		BSP_LCD_DisplayStringAt(0,70,(uint8_t*)txt,CENTER_MODE);

		if(status == NFCTAG_OK) {
			for(int data_index = 0; data_index < 4 ; data_index++) {
				sprintf(data_str,"0x%02X, 0x%02X, 0x%02X, 0x%02X",NDEF_Buffer[data_index*4],
				        NDEF_Buffer[data_index*4+1],
				        NDEF_Buffer[data_index*4+2],
				        NDEF_Buffer[data_index*4+3]);
				BSP_LCD_DisplayStringAt(0,86 + (data_index * 16),(uint8_t*)data_str,CENTER_MODE);
			}
		} else {
			sprintf(txt, "Cannot read data");
			BSP_LCD_DisplayStringAt(0,102,(uint8_t*)txt,CENTER_MODE);
		}

		BSP_LCD_SetFont(&Font22);
	}

	// write ndef icon
	if(gActiveDev->NdefSupport)
		BSP_LCD_DisplayPicture(260,160, send_icon);

	if((ndefBuffer != NDEF_Buffer) && (ndefBuffer != NULL)) {
		free(ndefBuffer);
		ndefBuffer = NULL;
	}
	return MENU_CALLBACK_CONTINUE;

}
#endif

/* Tag content Tab callback: manage NDEF message write */
#ifndef FREEZE_DISPLAY
static Menu_Callback_Status_t tabTagReadNdef(void)
{
	Menu_Position_t touch;

	if(!gActiveDev->NdefSupport)
		return MENU_CALLBACK_CONTINUE;

	if(Menu_ReadPosition(&touch)) {
		if((touch.X > 250) && (touch.Y > 150)) {
			uint16_t length = 0;
			sURI_Info w_uri = {URI_ID_0x01_STRING, "st.com/st25r3916-demo",""};
			NDEF_PrepareURIMessage(&w_uri,NDEF_Buffer,&length);
			if(NfcTag_WriteNDEF(length,NDEF_Buffer) == NFCTAG_OK) {
				clearTab();
				Menu_SetStyle(PLAIN);
				BSP_LCD_SetFont(&Font16);
				BSP_LCD_DisplayStringAt(0,120, (uint8_t*)"NDEF write success!", CENTER_MODE);
				HAL_Delay(500);
				tabFirstTagReadNdef();
			} else {
				clearTab();
				Menu_SetStyle(PLAIN);
				BSP_LCD_SetFont(&Font16);
				BSP_LCD_DisplayStringAt(0,120, (uint8_t*)"NDEF write error!", CENTER_MODE);
				HAL_Delay(500);
			}
			BSP_LCD_SetFont(&Font22);
		} // else {
		// return MENU_CALLBACK_CONTINUE;
		//}

	}
	return MENU_CALLBACK_CONTINUE;
}

/* Helper function to setup BSP with correct NFC protocol */
static void SelectNDEFProtocol(uint8_t devId)
{
	switch (gDevList[devId].type) {
	case BSP_NFCTAG_NFCA:
		switch (gDevList[devId].dev.nfca.type) {
		case RFAL_NFCA_T1T:
			NfcTag_SelectProtocol(NFCTAG_TYPE1);
			break;
		case RFAL_NFCA_T2T:
			NfcTag_SelectProtocol(NFCTAG_TYPE2);
			break;
		case RFAL_NFCA_T4T:
			NfcTag_SelectProtocol(NFCTAG_TYPE4);
			break;
		case RFAL_NFCA_NFCDEP:
			/* TODO RFAL_NFCA_NFCDEP */
			break;
		case RFAL_NFCA_T4T_NFCDEP:
			/* TODO RFAL_NFCA_T4T_NFCDEP */
			break;
		}
		break;
	case BSP_NFCTAG_NFCB:
		NfcTag_SelectProtocol(NFCTAG_TYPE4);
		break;
	case BSP_NFCTAG_ST25TB:
		NfcTag_SelectProtocol(NFCTAG_TYPE4);
		break;
	case BSP_NFCTAG_NFCF:
		NfcTag_SelectProtocol(NFCTAG_TYPE3);
		break;
	case BSP_NFCTAG_NFCV:
		NfcTag_SelectProtocol(NFCTAG_TYPE5);
		break;
	default:
		return;
	}
}
#endif

/* Tag Tab setup */
#ifndef FREEZE_DISPLAY
static Menu_Tab_Setup_t tabSetup[] = {
	{"Tag Info", &tabFirstTagReadInfo, &tabTagReadInfo},  /* Tag information Tab */
	{"NDEF", &tabFirstTagReadNdef, &tabTagReadNdef},      /* Tag content Tab */
};
#endif

/* Helper function to manage user touchscreen interface */
static uint8_t manageInput(t_hydra_console *con)
{
#ifndef FREEZE_DISPLAY
	Menu_Position_t touch;
	if(Menu_ReadPosition(&touch)) {
		if(touch.Y > 200) {
			// make sure the Wakeup mode is deactivated
			rfalWakeUpModeStop();
			// switch the field off before leaving the demo
			rfalFieldOff();
			return 1;
		} else if (instructionsDisplayed) {
			if(touch.Y < 60) {
				if(detectMode != DETECT_MODE_POLL) {
					detectMode = DETECT_MODE_POLL;
					rfalWakeUpModeStop();
					setRadio();
				}
			} else {
				if(detectMode != DETECT_MODE_WAKEUP) {
					detectMode = DETECT_MODE_WAKEUP;
					rfalFieldOff(); /* Turns the Field On and starts GT timer */
					HAL_Delay(100);
					rfalWakeUpModeStart(NULL);
					setRadio();
				}
			}
		} else {
			uint8_t devId = 0xFF;
			// tags are in the field
			if((touch.Y < getTagBoxY(1)) && (gDevCnt >= 1)) {
				devId = 0;
			} else if ((touch.Y < getTagBoxY(2)) && (gDevCnt >= 2)) {
				devId = 1;
			} else if ((touch.Y < getTagBoxY(3)) && (gDevCnt >= 3)) {
				devId = 2;
			} else if (gDevCnt >= 4) {
				devId = 3;
			}
			if (devId != 0xFF) {
				SelectNDEFProtocol(devId);
				gActiveDev = &gDevList[devId];

				/* Specific implementation for Random UIDs, as the selected UID has probably changed since last inventory */
				/* So re-run inventory and select the another random UID  */
				if((gActiveDev->type == BSP_NFCTAG_NFCA) &&
				   (gActiveDev->dev.nfca.type == RFAL_NFCA_T4T) &&
				   (gActiveDev->dev.nfca.nfcId1Len == 4) &&
				   (gActiveDev->dev.nfca.nfcId1[0] == 0x08)) {
					int err;
					uint8_t devCnt;
					// this is a random UID, need to refresh it
					rfalNfcaListenDevice nfcaDevList[RFAL_POLLER_DEVICES];

					rfalNfcaPollerInitialize();
					rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
					err = rfalNfcaPollerFullCollisionResolution( RFAL_COMPLIANCE_MODE_NFC, RFAL_POLLER_DEVICES, nfcaDevList, &devCnt );
					if( (err == ERR_NONE) && (devCnt != 0) ) {
						int searchIndex;
						for(searchIndex = 0; searchIndex < RFAL_POLLER_DEVICES ; searchIndex++ ) {
							if( (nfcaDevList[searchIndex].type == RFAL_NFCA_T4T) &&
							    (nfcaDevList[searchIndex].nfcId1Len == 4) &&
							    (nfcaDevList[searchIndex].nfcId1[0] == 0x08)) {
								// this is a random UID, select this one
								devId = 0;
								gDevList[devId].type     = BSP_NFCTAG_NFCA;
								gDevList[devId].dev.nfca = nfcaDevList[searchIndex];
								gActiveDev = &gDevList[devId];
								gDevCnt = 1;
								// stop searching random uid
								break;
							}
						}
					}
				}
				// be carefull this cast has not the same size!!!
				int status = BSP_NFCTAG_Activate((BSP_NFCTAG_Device_t *)&gDevList[devId]);

				Menu_MsgStatus("Tag read","",MSG_INFO);
				Menu_DisplayCenterString(10, "Touch here to exit");
				Menu_TabLoop(tabSetup,sizeof(tabSetup)/sizeof(Menu_Tab_Setup_t));

				BSP_LCD_SetFont(&Font22);
				Menu_SetStyle(CLEAR_PLAIN);
				Menu_FillRectangle(0,22,320,196);
				Menu_Delay(200);
				gActiveDev = NULL;
				gState = RFAL_POLLER_STATE_DEACTIVATION;
			}
		}
	}
#else
	if(hydrabus_ubtn()) {
		cprintf(con, "UBTN pressed exit\r\n");
		// make sure the Wakeup mode is deactivated
		rfalWakeUpModeStop();
		// switch the field off before leaving the demo
		rfalFieldOff();
		return 1;
	}

	if (instructionsDisplayed) {
		if(detectMode != DETECT_MODE_POLL) {
			detectMode = DETECT_MODE_POLL;
			rfalWakeUpModeStop();
			setRadio(con);
		}
	} else {
#if 0
		uint8_t devId = 0xFF;
		// tags are in the field
		if( (gDevCnt >= 1)) {
			devId = 0;
		} else if ((gDevCnt >= 2)) {
			devId = 1;
		} else if ((gDevCnt >= 3)) {
			devId = 2;
		} else if (gDevCnt >= 4) {
			devId = 3;
		}
		if (devId != 0xFF) {
			SelectNDEFProtocol(devId);
			gActiveDev = &gDevList[devId];

			/* Specific implementation for Random UIDs, as the selected UID has probably changed since last inventory */
			/* So re-run inventory and select the another random UID  */
			if((gActiveDev->type == BSP_NFCTAG_NFCA) &&
			   (gActiveDev->dev.nfca.type == RFAL_NFCA_T4T) &&
			   (gActiveDev->dev.nfca.nfcId1Len == 4) &&
			   (gActiveDev->dev.nfca.nfcId1[0] == 0x08)) {
				int err;
				uint8_t devCnt;
				// this is a random UID, need to refresh it
				rfalNfcaListenDevice nfcaDevList[RFAL_POLLER_DEVICES];

				err = rfalNfcaPollerInitialize();
				if( err != ERR_NONE ) {
					cprintf(con, "manageInput rfalNfcaPollerInitialize err=%d\r\n", err);
				}
				rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
				err = rfalNfcaPollerFullCollisionResolution( RFAL_COMPLIANCE_MODE_NFC, RFAL_POLLER_DEVICES, nfcaDevList, &devCnt );
				if( (err == ERR_NONE) && (devCnt != 0) ) {
					int searchIndex;
					for(searchIndex = 0; searchIndex < RFAL_POLLER_DEVICES ; searchIndex++ ) {
						if( (nfcaDevList[searchIndex].type == RFAL_NFCA_T4T) &&
						    (nfcaDevList[searchIndex].nfcId1Len == 4) &&
						    (nfcaDevList[searchIndex].nfcId1[0] == 0x08)) {
							// this is a random UID, select this one
							devId = 0;
							gDevList[devId].type     = BSP_NFCTAG_NFCA;
							gDevList[devId].dev.nfca = nfcaDevList[searchIndex];
							gActiveDev = &gDevList[devId];
							gDevCnt = 1;
							// stop searching random uid
							break;
						}
					}
				}
			}
			// be carefull this cast has not the same size!!!
			int status = BSP_NFCTAG_Activate((BSP_NFCTAG_Device_t *)&gDevList[devId]);
			gActiveDev = NULL;
			gState = RFAL_POLLER_STATE_DEACTIVATION;
		}
#endif
	}
#endif
	return 0;
}

/** @brief Scan Tags
	* Detect and display tags in the field.
	*/
void ScanTags(t_hydra_console *con, nfc_technology_t nfc_tech)
{
#ifndef FREEZE_DISPLAY
	Menu_MsgStatus("Tag detection","",MSG_INFO);
	Menu_SetStyle(CLEAR_PLAIN);
	Menu_DisplayCenterString(10,"Touch here to exit");
#endif
//	cprintf(con, "Tag detection\r\nPress UBTN to exit\r\n");
	instructionsDisplayed = false;

	//Menu_SetStyle(PLAIN);
	detectMode = DETECT_MODE_POLL;

	RfalPollerRun(con, nfc_tech);
}

/*!
 ******************************************************************************
 * \brief Passive Poller Run
 *
 * This method implements the main state machine going thought all the
 * different activities that a Reader/Poller device (PCD) needs to perform.
 *
 *
 ******************************************************************************
 */
static void RfalPollerRun(t_hydra_console *con, nfc_technology_t nfc_tech)
{
	/* Initialize RFAL */
	platformLog("\n\r RFAL Poller started \r\n");
	//cprintf(con, "\n\r RFAL Poller started \r\n");
	gState = RFAL_POLLER_STATE_INIT;

	do {
		rfalWorker();	/* Execute RFAL process */
		if (detectMode == DETECT_MODE_WAKEUP) {
			if(!rfalWakeUpModeHasWoke()) {
				if(manageInput(con)) {
					return;
				}
				// still sleeping, don't do nothing...
				continue;
			}
			// exit wake up mode
			detectMode = DETECT_MODE_AWAKEN;
			rfalWakeUpModeStop();
		}

		//cprintf(con, "gState=%d\r\n", gState);
		switch( gState ) {
		/*******************************************************************************/
		case RFAL_POLLER_STATE_INIT: {
			gTechsFound = RFAL_POLLER_FOUND_NONE;
			gActiveDev  = NULL;
			gDevCnt     = 0;

			gState = RFAL_POLLER_STATE_TECHDETECT;
			break;
		}
		/*******************************************************************************/
		case RFAL_POLLER_STATE_TECHDETECT: {
			if( !RfalPollerTechDetection(con, nfc_tech) ) { /* Poll for nearby devices in different technologies */
				gState = RFAL_POLLER_STATE_DEACTIVATION; /* If no device was found, restart loop */
				break;
			}

			gState = RFAL_POLLER_STATE_COLAVOIDANCE; /* One or more devices found, go to Collision Avoidance */
			break;
		}
		/*******************************************************************************/
		case RFAL_POLLER_STATE_COLAVOIDANCE: {

			if(instructionsDisplayed) {
#ifndef FREEZE_DISPLAY
				int clearline;
				for(clearline = 1; clearline<9; clearline++)
					BSP_LCD_ClearStringLine(clearline);
#endif
				instructionsDisplayed = false;
			}

			// add delay to avoid NFC-V Anticol frames back to back
			HAL_Delay(1); /* Wait 1ms required for some Tags */
			if( !RfalPollerCollResolution(con) ) { /* Resolve any eventual collision */
				gState = RFAL_POLLER_STATE_DEACTIVATION;	/* If Collision Resolution was unable to retrieve any device, restart loop */
				break;
			}

			platformLog("Device(s) found: %d \r\n", gDevCnt);
			//cprintf(con, "Device(s) found: %d\r\n", gDevCnt);

			gState = RFAL_POLLER_STATE_DEACTIVATION;
			break;
		}
		/*******************************************************************************/
		case RFAL_POLLER_STATE_DEACTIVATION: {
			if(!instructionsDisplayed) {
#ifndef FREEZE_DISPLAY
				BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
				BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
#endif
				if(gDevCnt == 0) {
					tagDetectionNoTag(con, nfc_tech);
					instructionsDisplayed = true;
				}
			}
			RfalPollerDeactivate();	/* If a card has been activated, properly deactivate the device */
			rfalFieldOff();	/* Turn the Field Off powering down any device nearby */
			/*
			for(delay = 0; delay < 100; delay++)
			{
				if(manageInput(con))
				{
					return;
				}
				platformDelay(1); // Remain a certain period with field off
			}
			*/
			if( (detectMode == DETECT_MODE_AWAKEN) && (gDevCnt == 0) ) {
				// no more tags, restart wakeup mode
				detectMode = DETECT_MODE_WAKEUP;
				rfalFieldOff();	/* Turns the Field On and starts GT timer */
				HAL_Delay(100);
				rfalWakeUpModeStart(NULL);
				setRadio(con);
			}

			gState = RFAL_POLLER_STATE_INIT;	/* Restart the loop */
			return; /* We exit */
			break;
		}
		/*******************************************************************************/
		default:
			return;
		}
		if(manageInput(con)) {
			return;
		}
	} while(1);
}

/*!
 ******************************************************************************
 * \brief Poller Technology Detection
 *
 * This method implements the Technology Detection / Poll for different
 * device technologies.
 *
 * \return true         : One or more devices have been detected
 * \return false         : No device have been detected
 *
 ******************************************************************************
 */
static bool RfalPollerTechDetection(t_hydra_console *con, nfc_technology_t nfc_tech)
{
	(void)(con);
	ReturnCode           err;
	rfalNfcaSensRes      sensRes;
	rfalNfcbSensbRes     sensbRes;
	rfalNfcvInventoryRes invRes;
	uint8_t              sensbResLen;

	gTechsFound = RFAL_POLLER_FOUND_NONE;
	/*******************************************************************************/
	/* NFC-A Technology Detection                                                  */
	/*******************************************************************************/
	if( (nfc_tech == NFC_ALL) || (nfc_tech == NFC_A)) {
		err = rfalNfcaPollerInitialize(); /* Initialize RFAL for NFC-A */
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerTechDetection rfalNfcaPollerInitialize err=%d\r\n", err);
		}
		rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
		err = rfalNfcaPollerTechnologyDetection(RFAL_COMPLIANCE_MODE_NFC, &sensRes ); /* Poll for NFC-A devices */
		if( err == ERR_NONE ) {
			gTechsFound |= RFAL_POLLER_FOUND_A;
		} else {
			//cprintf(con, "RfalPollerTechDetection rfalNfcaPollerTechnologyDetection err=%d\r\n", err);
		}
	}

	/*******************************************************************************/
	/* NFC-B Technology Detection                                                  */
	/*******************************************************************************/
	if( (nfc_tech == NFC_ALL) || (nfc_tech == NFC_B)) {
		err = rfalNfcbPollerInitialize(); /* Initialize RFAL for NFC-B */
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerTechDetection rfalNfcbPollerInitialize err=%d\r\n", err);
		}
		rfalFieldOnAndStartGT(); /* As field is already On only starts GT timer */
		err = rfalNfcbPollerTechnologyDetection(RFAL_COMPLIANCE_MODE_NFC, &sensbRes, &sensbResLen ); /* Poll for NFC-B devices */
		if( err == ERR_NONE ) {
			gTechsFound |= RFAL_POLLER_FOUND_B;
		} else {
			//cprintf(con, "RfalPollerTechDetection rfalNfcbPollerTechnologyDetection err=%d\r\n", err);
		}
	}

	/*******************************************************************************/
	/* ST25TB Technology Detection                                                 */
	/*******************************************************************************/
	if(nfc_tech == NFC_ST25TB) {
		uint8_t          chipId;
		err = rfalSt25tbPollerInitialize(); /* Initialize RFAL for NFC-B */
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerTechDetection rfalSt25tbPollerInitialize err=%d\r\n", err);
		}
		rfalFieldOnAndStartGT();
		/* As field is already On only starts GT timer */
		err = rfalSt25tbPollerCheckPresence( &chipId);
		if( err == ERR_NONE ) {
			gTechsFound |= RFAL_POLLER_FOUND_ST25TB;
		} else {
			//cprintf(con, "RfalPollerTechDetection rfalSt25tbPollerCheckPresence err=%d\r\n", err);
		}
	}

	/*******************************************************************************/
	/* NFC-F Technology Detection                                                  */
	/*******************************************************************************/
	if( (nfc_tech == NFC_ALL) || (nfc_tech == NFC_F)) {
		err = rfalNfcfPollerInitialize( RFAL_BR_212 ); /* Initialize RFAL for NFC-F */
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerTechDetection rfalNfcfPollerInitialize err=%d\r\n", err);
		}
		rfalFieldOnAndStartGT(); /* As field is already On only starts GT timer */
		err = rfalNfcfPollerCheckPresence(); /* Poll for NFC-F devices */
		if( err == ERR_NONE ) {
			gTechsFound |= RFAL_POLLER_FOUND_F;
		} else {
			//cprintf(con, "RfalPollerTechDetection rfalNfcfPollerCheckPresence err=%d\r\n", err);
		}
	}

	/*******************************************************************************/
	/* NFC-V Technology Detection                                                  */
	/*******************************************************************************/
	if( (nfc_tech == NFC_ALL) || (nfc_tech == NFC_V)) {
		err = rfalNfcvPollerInitialize(); /* Initialize RFAL for NFC-V */
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerTechDetection rfalNfcvPollerInitialize err=%d\r\n", err);
		}
		rfalFieldOnAndStartGT(); /* As field is already On only starts GT timer */
		err = rfalNfcvPollerCheckPresence( &invRes ); /* Poll for NFC-V devices */
		if( err == ERR_NONE ) {
			gTechsFound |= RFAL_POLLER_FOUND_V;
		} else {
			//cprintf(con, "RfalPollerTechDetection rfalNfcvPollerCheckPresence err=%d\r\n", err);
		}
	}
	return (gTechsFound != RFAL_POLLER_FOUND_NONE);
}

/*!
 ******************************************************************************
 * \brief Poller Collision Resolution
 *
 * This method implements the Collision Resolution on all technologies that
 * have been detected before.
 *
 * \return true         : One or more devices identified
 * \return false        : No device have been identified
 *
 ******************************************************************************
 */
static bool RfalPollerCollResolution(t_hydra_console *con)
{
	uint8_t    i;
	uint8_t    devCnt;
	ReturnCode err;

	/*******************************************************************************/
	/* NFC-A Collision Resolution                                                  */
	/*******************************************************************************/
	if( gTechsFound & RFAL_POLLER_FOUND_A ) {	/* If a NFC-A device was found/detected, perform Collision Resolution */
		rfalNfcaListenDevice nfcaDevList[RFAL_POLLER_DEVICES];

		err = rfalNfcaPollerInitialize(); /* Initialize RFAL for NFC-A */
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerCollResolution rfalNfcaPollerInitialize err=%d\r\n", err);
		}

		err = rfalNfcaPollerFullCollisionResolution( RFAL_COMPLIANCE_MODE_NFC, (RFAL_POLLER_DEVICES - gDevCnt), nfcaDevList, &devCnt );
		if( (err == ERR_NONE) && (devCnt != 0) ) {
			for( i=0; i<devCnt; i++ ) {	/* Copy devices found form local Nfca list into global device list */
#ifndef FREEZE_DISPLAY
				Menu_SetStyle(NFCA);
#endif
				int j;
				char speedStr[20];

				gDevList[gDevCnt].type     = BSP_NFCTAG_NFCA;
				gDevList[gDevCnt].dev.nfca = nfcaDevList[i];
				gDevCnt++;

				getSpeedStr(speedStr);
				cprintf(con, "NFC-A %s Type=0x%02X ATQA=0x%02X%02X SAK=0x%02X",
				        speedStr,
				        nfcaDevList[i].type,
				        nfcaDevList[i].sensRes.platformInfo,
				        nfcaDevList[i].sensRes.anticollisionInfo,
				        nfcaDevList[i].selRes.sak);

				if (gDevList[i].dev.nfca.type == RFAL_NFCA_T4T) {
					/*******************************************************************************/
					rfalIsoDepInitialize();
					/* Perform ISO-DEP (ISO14443-4) activation: RATS and PPS if supported */
					err = rfalIsoDepPollAHandleActivation(
					          (rfalIsoDepFSxI)RFAL_ISODEP_FSDI_DEFAULT,
					          RFAL_ISODEP_NO_DID,
					          RFAL_BR_848 /* RFAL_BR_424 RFAL_BR_106 */,
					          &gDevList[i].proto.isoDep);
					if( (err == ERR_NONE) && (gDevList[i].proto.isoDep.activation.A.Listener.ATSLen > 4) ) {
						cprintf(con, " ATS=0x%02X%02X%02X%02X%02X",
						        gDevList[i].proto.isoDep.activation.A.Listener.ATS.TL,
						        gDevList[i].proto.isoDep.activation.A.Listener.ATS.T0,
						        gDevList[i].proto.isoDep.activation.A.Listener.ATS.TA,
						        gDevList[i].proto.isoDep.activation.A.Listener.ATS.TB,
						        gDevList[i].proto.isoDep.activation.A.Listener.ATS.TC);
						for(j = 0; j < gDevList[i].proto.isoDep.activation.A.Listener.ATSLen-5; j++)
							cprintf(con,"%02X",gDevList[i].proto.isoDep.activation.A.Listener.ATS.HB[j]);
					}
				}

				cprintf(con, " UID:");
				for(j = 0; j < nfcaDevList[i].nfcId1Len; j++)
					cprintf(con,"%02X",nfcaDevList[i].nfcId1[j]);
				cprintf(con, "\r\n");

				detect_nfc_card_type(con, 
									 &nfcaDevList[i].nfcId1[0], nfcaDevList[i].nfcId1Len,
									 ((nfcaDevList[i].sensRes.platformInfo << 8)| nfcaDevList[i].sensRes.anticollisionInfo),
									 nfcaDevList[i].selRes.sak);
			}
		}
	}
	/*******************************************************************************/
	/* NFC-B Collision Resolution                                                  */
	/*******************************************************************************/
	if( gTechsFound & RFAL_POLLER_FOUND_B ) {	/* If a NFC-A device was found/detected, perform Collision Resolution */
		rfalNfcbListenDevice nfcbDevList[RFAL_POLLER_DEVICES];

		err = rfalNfcbPollerInitialize();
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerCollResolution rfalNfcbPollerInitialize err=%d\r\n", err);
		}
		err = rfalNfcbPollerCollisionResolution( RFAL_COMPLIANCE_MODE_NFC, (RFAL_POLLER_DEVICES - gDevCnt), nfcbDevList, &devCnt );
		if( (err == ERR_NONE) && (devCnt != 0) ) {
			for( i=0; i<devCnt; i++ ) {	/* Copy devices found form local Nfcb list into global device list */
#ifndef FREEZE_DISPLAY
				Menu_SetStyle(NFCB);
#endif
				uint j;
				char speedStr[20];
				getSpeedStr(speedStr);
				cprintf(con, "NFC-B %s UID:", speedStr);

				for(j = 0; j < RFAL_NFCB_NFCID0_LEN; j++)
					cprintf(con,"%02X",nfcbDevList[i].sensbRes.nfcid0[j]);
				cprintf(con, "\r\n");

				gDevList[gDevCnt].type     = BSP_NFCTAG_NFCB;
				gDevList[gDevCnt].dev.nfcb = nfcbDevList[i];
				gDevCnt++;
			}
		}
	}
	/*******************************************************************************/
	/* ST25TB Collision Resolution                                                  */
	/*******************************************************************************/
	if( gTechsFound & RFAL_POLLER_FOUND_ST25TB ) {	/* If a NFC-A device was found/detected, perform Collision Resolution */
		rfalSt25tbListenDevice st25tbDevList[RFAL_POLLER_DEVICES];
		err = rfalSt25tbPollerInitialize();
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerCollResolution rfalSt25tbPollerInitialize err=%d\r\n", err);
		}
		err = rfalSt25tbPollerCollisionResolution((RFAL_POLLER_DEVICES - gDevCnt), st25tbDevList, &devCnt);
		if( (err == ERR_NONE) && (devCnt != 0) ) {
			for( i=0; i<devCnt; i++ ) {	/* Copy devices found form local Nfcb list into global device list */
#ifndef FREEZE_DISPLAY
				Menu_SetStyle(NFCB);
#endif
				uint j;
				char speedStr[20];
				getSpeedStr(speedStr);
				cprintf(con, "ST25TB %s UID:", speedStr);

				for(j = 0; j<RFAL_ST25TB_UID_LEN; j++)
					cprintf(con,"%02X",st25tbDevList[i].UID[j]);
				cprintf(con, "\r\n");

				gDevList[gDevCnt].type     = BSP_NFCTAG_ST25TB;
				gDevList[gDevCnt].dev.st25tb = st25tbDevList[i];
				gDevCnt++;
			}
		}
	}
	/*******************************************************************************/
	/* NFC-F Collision Resolution                                                  */
	/*******************************************************************************/
	if( gTechsFound & RFAL_POLLER_FOUND_F ) {	/* If a NFC-F device was found/detected, perform Collision Resolution */
		rfalNfcfListenDevice nfcfDevList[RFAL_POLLER_DEVICES];

		err = rfalNfcfPollerInitialize( RFAL_BR_212 );
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerCollResolution rfalNfcfPollerInitialize err=%d\r\n", err);
		}
		err = rfalNfcfPollerCollisionResolution( RFAL_COMPLIANCE_MODE_NFC, (RFAL_POLLER_DEVICES - gDevCnt), nfcfDevList, &devCnt );
		if( (err == ERR_NONE) && (devCnt != 0) ) {
			for( i=0; i<devCnt; i++ ) {	/* Copy devices found form local Nfcf list into global device list */
#ifndef FREEZE_DISPLAY
				Menu_SetStyle(NFCF);
#endif
				uint j;
				char speedStr[20];
				getSpeedStr(speedStr);
				cprintf(con, "NFC-F %s UID:", speedStr);

				for(j = 0; j<RFAL_NFCF_NFCID2_LEN; j++)
					cprintf(con,"%02X",nfcfDevList[i].sensfRes.NFCID2[j]);
				cprintf(con, "\r\n");

				gDevList[gDevCnt].type     = BSP_NFCTAG_NFCF;
				gDevList[gDevCnt].dev.nfcf = nfcfDevList[i];
				gDevCnt++;
			}
		}
	}
	/*******************************************************************************/
	/* NFC-V Collision Resolution                                                  */
	/*******************************************************************************/
	if( gTechsFound & RFAL_POLLER_FOUND_V ) {	/* If a NFC-F device was found/detected, perform Collision Resolution */
		rfalNfcvListenDevice nfcvDevList[RFAL_POLLER_DEVICES];

		err = rfalNfcvPollerInitialize();
		if( err != ERR_NONE ) {
			//cprintf(con, "RfalPollerCollResolution rfalNfcvPollerInitialize err=%d\r\n", err);
		}
		err = rfalNfcvPollerCollisionResolution( RFAL_COMPLIANCE_MODE_NFC, (RFAL_POLLER_DEVICES - gDevCnt), nfcvDevList, &devCnt);
		if( (err == ERR_NONE) && (devCnt != 0) ) {
			for( i=0; i<devCnt; i++ ) {	/* Copy devices found form local Nfcf list into global device list */
#ifndef FREEZE_DISPLAY
				Menu_SetStyle(NFCV);
#endif
				uint8_t uid[RFAL_NFCV_UID_LEN];
				uint uid_i;
				for(uid_i = 0; uid_i < RFAL_NFCV_UID_LEN; uid_i ++)
					uid[uid_i] = nfcvDevList[i].InvRes.UID[RFAL_NFCV_UID_LEN - uid_i -1];

				uint j;
				char speedStr[20];
				getSpeedStr(speedStr);
				cprintf(con, "NFC-V %s UID:", speedStr);

				for(j = 0; j<RFAL_NFCV_UID_LEN; j++)
					cprintf(con,"%02X",uid[j]);
				cprintf(con, "\r\n");

				gDevList[gDevCnt].type     = BSP_NFCTAG_NFCV;
				gDevList[gDevCnt].dev.nfcv = nfcvDevList[i];
				gDevCnt++;
			}
		}
	}

	return (gDevCnt > 0);
}

/*!
 ******************************************************************************
 * \brief Poller NFC DEP Deactivate
 *
 * This method Deactivates the device if a deactivation procedure exists
 *
 * \return true         : Deactivation successful
 * \return false        : Deactivation failed
 *
 ******************************************************************************
 */
static bool RfalPollerDeactivate( void )
{
	if( gActiveDev != NULL ) { /* Check if a device has been activated */
		switch( gActiveDev->rfInterface ) {
		/*******************************************************************************/
		case RFAL_POLLER_INTERFACE_RF:
			break; /* No specific deactivation to be performed */

		/*******************************************************************************/
		case RFAL_POLLER_INTERFACE_ISODEP:
			rfalIsoDepDeselect(); /* Send a Deselect to device */
			break;

		/*******************************************************************************/
		case RFAL_POLLER_INTERFACE_NFCDEP:
			rfalNfcDepRLS(); /* Send a Release to device */
			break;

		default:
			return false;
		}
		platformLog("Device deactivated \r\n");
	}

	return true;
}

/*
 Integrated Circuit Manufacturer (ICM) https://www.kartenbezogene-identifier.de/de/chiphersteller-kennungen.html
 Based on ISO/IEC JTC1/SC17 STANDING DOCUMENT 5 (Updated March 2018) Register of IC manufacturers
*/
static void print_manufacturer_nfc_card(t_hydra_console *con, uint8_t* uid, uint8_t uid_len)
{
	if(uid_len > 0)
	{
		cprintf(con, "MANUFACTURER: ");
		switch(uid[0])
		{
			case 0x01: cprintf(con, "Motorola"); break;
			case 0x02: cprintf(con, "STMicroelectronics"); break;
			case 0x03: cprintf(con, "Hitachi Ltd"); break;
			case 0x04: cprintf(con, "NXP Semiconductors"); break;
			case 0x05: cprintf(con, "Infineon Technologies"); break;
			case 0x06: cprintf(con, "Cylink"); break;
			case 0x07: cprintf(con, "Texas Instruments"); break;
			case 0x08: cprintf(con, "Fujitsu Limited"); break;
			case 0x09: cprintf(con, "Matsushita Electronics"); break;
			case 0x0A: cprintf(con, "NEC"); break;
			case 0x0B: cprintf(con, "Oki Electric Industry Co Ltd"); break;
			case 0x0C: cprintf(con, "Toshiba Corp"); break;
			case 0x0D: cprintf(con, "Mitsubishi Electric Corp"); break;
			case 0x0E: cprintf(con, "Samsung Electronics Co Ltd"); break;
			case 0x0F: cprintf(con, "Hynix"); break;
			case 0x10: cprintf(con, "LG-Semiconductors Co Ltd"); break;
			case 0x11: cprintf(con, "Emosyn-EM Microelectronics"); break;
			case 0x12: cprintf(con, "INSIDE Technology"); break;
			case 0x13: cprintf(con, "ORGA Kartensysteme GmbH"); break;
			case 0x14: cprintf(con, "Sharp Corporation"); break;
			case 0x15: cprintf(con, "ATMEL"); break;
			case 0x16: cprintf(con, "EM Microelectronic-Marin"); break;
			case 0x17: cprintf(con, "SMARTRAC TECHNOLOGY GmbH"); break;
			case 0x18: cprintf(con, "ZMD AG"); break;
			case 0x19: cprintf(con, "XICOR Inc"); break;
			case 0x1A: cprintf(con, "Sony Corporation"); break;
			case 0x1B: cprintf(con, "Malaysia Microelectronic Solutions Sdn Bhd"); break;
			case 0x1C: cprintf(con, "Emosyn"); break;
			case 0x1D: cprintf(con, "Shanghai Fudan Microelectronics Co Ltd"); break;
			case 0x1E: cprintf(con, "Magellan Technology Pty Limited"); break;
			case 0x1F: cprintf(con, "Melexis NV BO"); break;
			case 0x20: cprintf(con, "Renesas Technology Corp"); break;
			case 0x21: cprintf(con, "TAGSYS"); break;
			case 0x22: cprintf(con, "Transcore"); break;
			case 0x23: cprintf(con, "Shanghai Belling Corp Ltd"); break;
			case 0x24: cprintf(con, "Masktech Germany GmbH"); break;
			case 0x25: cprintf(con, "Innovision Research and Technology Plc"); break;
			case 0x26: cprintf(con, "Hitachi ULSI Systems Co Ltd"); break;
			case 0x27: cprintf(con, "Yubico AB"); break;
			case 0x28: cprintf(con, "Ricoh"); break;
			case 0x29: cprintf(con, "ASK"); break;
			case 0x2A: cprintf(con, "Unicore Microsystems LLC"); break;
			case 0x2B: cprintf(con, "Dallas semiconductor/Maxim"); break;
			case 0x2C: cprintf(con, "Impinj Inc"); break;
			case 0x2D: cprintf(con, "RightPlug Alliance"); break;
			case 0x2E: cprintf(con, "Broadcom Corporation"); break;
			case 0x2F: cprintf(con, "MStar Semiconductor Inc"); break;
			case 0x30: cprintf(con, "BeeDar Technology Inc"); break;
			case 0x31: cprintf(con, "RFIDsec"); break;
			case 0x32: cprintf(con, "Schweizer Electronic AG"); break;
			case 0x33: cprintf(con, "AMIC Technology Corp"); break;
			case 0x34: cprintf(con, "Mikron JSC"); break;
			case 0x35: cprintf(con, "Fraunhofer Institute for Photonic Microsystems"); break;
			case 0x36: cprintf(con, "IDS Microship AG"); break;
			case 0x37: cprintf(con, "Kovio"); break;
			case 0x38: cprintf(con, "HMT Microelectronic Ltd"); break;
			case 0x39: cprintf(con, "Silicon Craft Technology"); break;
			case 0x3A: cprintf(con, "Advanced Film Device Inc."); break;
			case 0x3B: cprintf(con, "Nitecrest Ltd"); break;
			case 0x3C: cprintf(con, "Verayo Inc."); break;
			case 0x3D: cprintf(con, "HID Global"); break;
			case 0x3E: cprintf(con, "Productivity Engineering Gmbh"); break;
			case 0x3F: cprintf(con, "Austriamicrosystems AG"); break;
			case 0x40: cprintf(con, "Gemalto SA"); break;
			case 0x41: cprintf(con, "Renesas Electronics Corporation"); break;
			case 0x42: cprintf(con, "3Alogics Inc"); break;
			case 0x43: cprintf(con, "Top TroniQ Asia Limited"); break;
			case 0x44: cprintf(con, "Gentag Inc"); break;
			case 0x45: cprintf(con, "Invengo Information Technology Co.Ltd"); break;
			case 0x46: cprintf(con, "Guangzhou Sysur Microelectronics, Inc"); break;
			case 0x47: cprintf(con, "CEITEC S.A."); break;
			case 0x48: cprintf(con, "Shanghai Quanray Electronics Co. Ltd."); break;
			case 0x49: cprintf(con, "MediaTek Inc"); break;
			case 0x4A: cprintf(con, "Angstrem PJSC"); break;
			case 0x4B: cprintf(con, "Celisic Semiconductor"); break;
			case 0x4C: cprintf(con, "LEGIC Identsystems AG"); break;
			case 0x4D: cprintf(con, "Balluff GmbH"); break;
			case 0x4E: cprintf(con, "Oberthur Technologies"); break;
			case 0x4F: cprintf(con, "Silterra Malaysia Sdn. Bhd."); break;
			case 0x50: cprintf(con, "DELTA Danish Electronics, Light & Acoustics"); break;
			case 0x51: cprintf(con, "Giesecke & Devrient GmbH"); break;
			case 0x52: cprintf(con, "Shenzhen China Vision Microelectronics Co., Ltd."); break;
			case 0x53: cprintf(con, "Shanghai Feiju Microelectronics Co. Ltd."); break;
			case 0x54: cprintf(con, "Intel Corporation"); break;
			case 0x55: cprintf(con, "Microsensys GmbH"); break;
			case 0x56: cprintf(con, "Sonix Technology Co., Ltd."); break;
			case 0x57: cprintf(con, "Qualcomm Technologies Inc"); break;
			case 0x58: cprintf(con, "Realtek Semiconductor Corp"); break;
			case 0x59: cprintf(con, "Freevision Technologies Co. Ltd"); break;
			case 0x5A: cprintf(con, "Giantec Semiconductor Inc."); break;
			case 0x5B: cprintf(con, "JSC Angstrem-T"); break;
			case 0x5C: cprintf(con, "STARCHIP France"); break;
			case 0x5D: cprintf(con, "SPIRTECH"); break;
			case 0x5E: cprintf(con, "GANTNER Electronic GmbH"); break;
			case 0x5F: cprintf(con, "Nordic Semiconductor"); break;
			case 0x60: cprintf(con, "Verisiti Inc"); break;
			case 0x61: cprintf(con, "Wearlinks Technology Inc."); break;
			case 0x62: cprintf(con, "Userstar Information Systems Co., Ltd"); break;
			case 0x63: cprintf(con, "Pragmatic Printing Ltd."); break;
			case 0x64: cprintf(con, "Associação do Laboratório de Sistemas Integráveis Tecnológico – LSI-TEC"); break;
			case 0x65: cprintf(con, "Tendyron Corporation"); break;
			case 0x66: cprintf(con, "MUTO Smart Co., Ltd."); break;
			case 0x67: cprintf(con, "ON Semiconductor"); break;
			case 0x68: cprintf(con, "TÜBİTAK BİLGEM"); break;
			case 0x69: cprintf(con, "Huada Semiconductor Co., Ltd"); break;
			case 0x6A: cprintf(con, "SEVENEY"); break;
			case 0x6B: cprintf(con, "ISSM"); break;
			case 0x6C: cprintf(con, "Wisesec Ltd"); break;
			case 0x7E: cprintf(con, "Holtek"); break;
			default:
				cprintf(con, "Unknown");
		}
		cprintf(con, "\r\n");
	} else
	{
		cprintf(con, "MANUFACTURER: Unknown\r\n");
	}
}

/*
 See MIFARE Type Identification Procedure AN10833 Rev3.6 - 11 July 2016 https://www.nxp.com/docs/en/application-note/AN10833.pdf
  => See Table 5. ATQA Coding of NXP Contactless Card ICs
  => See Table 6. UIDs (4 Bytes) SAK coding of NXP Contactless Card ICs
  => MIFARE Ultralight EV1, MIFARE Plus EV1 and MIFARE DESFire EV2 support the command “GET_VERSION” to exactly identify the IC
 See also http://nfc-tools.org/index.php/ISO14443A
 See also https://www.nxp.com/docs/en/application-note/AN10834.pdf Rev. 4.1 — 20 March 2020 => Figure 3. MIFARE Card Activation examples*
 Note: Detection accuracy is not very good for some tags as it based only on atqa/sak
*/
nfc_card_type_t detect_nfc_card_type(t_hydra_console *con, uint8_t* uid, uint8_t uid_len, uint16_t atqa, uint8_t sak)
{
	nfc_card_type_t type = NFC_T_NONE;

	print_manufacturer_nfc_card(con, uid, uid_len);

	cprintf(con, "TYPE: ");
	switch(sak)
	{
		case 0x00: // MIFARE Ultralight CL2 & MIFARE Ultralight C => Double Size UID (7 bytes)
			if (atqa == 0x0044) {
				cprintf(con, "NXP MIFARE Ultralight");
				type |= NFC_T_MFULTRALIGHT;
			} else {
				cprintf(con, "Unknown");
				type |= NFC_T_OTHER;
			}
		break;

		case 0x01:
			cprintf(con, "NXP TNP3xxx");
			type |= NFC_T_OTHER;
		break;

		case 0x04:
			cprintf(con, "NXP Any MIFARE CL1 (except DESFire)");
			type |= NFC_T_MFDESFIRE;
		break;

		case 0x08:
			switch(atqa)
			{
				case 0x0004:
				case 0x0044: 
					cprintf(con, "NXP MIFARE CLASSIC 1K");
					type |= NFC_T_MFCLASSIC_1K;
				break;

				default:
					cprintf(con, "NXP MIFARE CLASSIC 1K/Plus 2K\r\n");
					type |= NFC_T_MFCLASSIC_1K_2K;
				break;
			}
		break;

		case 0x09:
			if (atqa == 0x0004) {
				cprintf(con, "NXP MIFARE Mini 0.3K");
				type |= NFC_T_MFMINI;
			} else {
				cprintf(con, "NXP MIFARE other");
				type |= NFC_T_OTHER;
			}
		break;

		case 0x10:
			cprintf(con, "NXP MIFARE Plus 2K");
			type |= NFC_T_MFPLUS;
		break;

		case 0x11:
			cprintf(con, "NXP MIFARE Plus 4K");
			type |= NFC_T_MFPLUS;
		break;

		case 0x18:
			switch(atqa)
			{
				case 0x0002:
					cprintf(con, "NXP MIFARE CLASSIC 4K");
					type |= NFC_T_MFCLASSIC_4K;
				break;

				case 0x0042:
					cprintf(con, "\r\nMIFARE Plus 4K / Plus EV1 4K\r\n");
					cprintf(con, "MIFARE Plus CL2 4K / Plus CL2 EV1 4K");
					type |= NFC_T_MFPLUS;
				break;

				default:
					cprintf(con, "MIFARE Classic 4K / Classic 4K CL2");
					type |= NFC_T_MFCLASSIC_4K;
				break;
			}
			break;

		case 0x20:
			switch(atqa)
			{
				case 0x0044:
					cprintf(con, "MIFARE Plus");
					type |= NFC_T_MFPLUS;
				break;

				case 0x0048:
					cprintf(con, "IBM JCOP31/JCOP41");
					type |= NFC_T_OTHER;
				break;

				case 0x0304:
					cprintf(con, "NXP NTAG424DNA (Random ID feature)");
					type |= NFC_T_NTAG424DNA;
				break;

				case 0x0344:
					cprintf(con, "\r\nMIFARE DESFire MF3ICD40\r\n");
					cprintf(con, "MIFARE DESFire EV1 2K/4K/8K / DESFire EV1 CL2 2K/4K/8K\r\n");
					cprintf(con, "MIFARE NTAG424DNA");
					type |= NFC_T_MFDESFIRE;
				break;

				default:
					cprintf(con, "Unknown");
					type |= NFC_T_OTHER;
/*
					cprintf(con, "\r\nMIFARE Plus 2K/4K / Plus EV1 2K/4K\r\n");
					cprintf(con, "MIFARE Plus CL2 2K/4K / Plus CL2 EV1 2K/4K");
					type |= NFC_T_MFPLUS;
*/
				break;
			}
			break;

		case 0x24:
			switch(atqa)
			{
				case 0x0344:
					cprintf(con, "MIFARE DESFire CL1 / DESFire EV1 CL1");
					type |= NFC_T_MFDESFIRE;
				break;

				default:
				 cprintf(con, "NXP MIFARE DESFire | DESFire EV1");
				 type |= NFC_T_MFDESFIRE;
				break;
			}
			break;

		case 0x28:
			switch(atqa)
			{
				case 0x0004:
					cprintf(con, "IBM JCOP41");
					type |= NFC_T_OTHER;
				break;

				case 0x0304:
					cprintf(con, "IBM JCOP31");
					type |= NFC_T_OTHER;
				break;

				case 0x0344:
					cprintf(con, "MIFARE DESFire CL1 / DESFire EV1 CL1");
					type |= NFC_T_MFDESFIRE;
				break;
			}
			break;

		case 0x38:
			cprintf(con, "Nokia 6212 or 6131 MIFARE CLASSIC 4K");
			type |= NFC_T_MFCLASSIC_4K;
			break;

		case 0x88:
			cprintf(con, "Infineon MIFARE CLASSIC 1K");
			type |= NFC_T_MFCLASSIC_1K_2K;
			break;

		case 0x98:
			if (atqa == 0x0002) {
				cprintf(con, "Gemplus MPCOS");
				type |= NFC_T_OTHER;
			}else {
				cprintf(con, "Unknown Gemplus MPCOS");
				type |= NFC_T_OTHER;
			}
			break;

		default:
			if (atqa == 0x0c00) {
				cprintf(con, "Innovision R&T Jewel");
				type |= NFC_T_OTHER;
			} else {
				cprintf(con, "Unknown");
				type |= NFC_T_OTHER;
			}
			break;
	}
	cprintf(con, "\r\n");

	return type;
}
