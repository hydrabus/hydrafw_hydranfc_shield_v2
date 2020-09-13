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

#include "common.h"
#include "ce.h"
#include "utils.h"
#include "rfal_rf.h"
#include "st_errno.h"
#include "rfal_isoDep.h"
#include "rfal_nfca.h"
#include "logger.h"

#include "st25r3916.h"

#include "bsp_print_dbg.h"
#include "hydranfc_v2_ce.h"
#include "hydranfc_v2_nfc_mode.h"

void ceNfcaCardemu4A(uint8_t mode, rfalIsoDepTxRxParam *isoDepTxRxParam);
void ceNfcfCardemu3(uint8_t mode, rfalTransceiveContext *ctx);

#define TX_BUF_LENGTH       (256+3)
static uint8_t txBuf_ce[TX_BUF_LENGTH] __attribute__ ((section(".cmm")));

#define RX_BUF_LENGTH       (256+3)
static uint8_t rxBuf_ce[RX_BUF_LENGTH] __attribute__ ((section(".cmm")));
static uint16_t rxRcvdLen = 0;
static bool  ceEnabled;


rfalLmConfPA                            configA;
rfalLmConfPB                            configB;
rfalLmConfPF                            configF;
rfalIsoDepAtsParam                      atsParam;
uint8_t                                 histChar[16];
uint32_t                                configMask = 0;
rfalTransceiveContext                   transceiveCtx;

uint8_t                                 emuMode = 0;

rfalIsoDepBufFormat                     *rxBufA;
rfalIsoDepBufFormat                     *txBufA;
bool                                    bIsRxChaining;
bool                                    isActivatedA = false;
bool                                    isFirstF_Frame = false;
bool                                    isFirstA3_Frame = true;
rfalIsoDepTxRxParam                     isoDepTxRxParam;

bool                                    rxReady = false;
bool                                    txReady = false;

rfalLmState statePrev = RFAL_LM_STATE_NOT_INIT;
rfalTransceiveState     tStateCur = 3;
rfalTransceiveState     tStatePrev = 3;

void ceInitalize( void )
{
	transceiveCtx.txBuf = txBuf_ce;
	transceiveCtx.txBufLen = 0;

	transceiveCtx.rxBuf = rxBuf_ce;
	transceiveCtx.rxBufLen = RX_BUF_LENGTH;
	transceiveCtx.rxRcvdLen = &rxRcvdLen;

	transceiveCtx.flags = RFAL_TXRX_FLAGS_DEFAULT;
	transceiveCtx.fwt = RFAL_FWT_NONE;

	rxBufA = (rfalIsoDepBufFormat*)rxBuf_ce;
	txBufA = (rfalIsoDepBufFormat*)txBuf_ce;

	isoDepTxRxParam.rxBuf = rxBufA;
	isoDepTxRxParam.rxLen = &rxRcvdLen;
	isoDepTxRxParam.txBuf = txBufA;
	isoDepTxRxParam.ourFSx = RFAL_ISODEP_FSX_KEEP;
	isoDepTxRxParam.isRxChaining = &bIsRxChaining;
	isoDepTxRxParam.isTxChaining = false;

	//
	configMask = 0;
	emuMode = 0;
	isActivatedA = false;
	isFirstF_Frame = false;
	isFirstA3_Frame = true;
	rxReady = false;
	txReady = false;

	ceEnabled = false;
	ul_cwrite_page_set = 0;
}

static rfalLmState state;

void ceHandler( void )
{
	bool dataFlag = false;
	ReturnCode retCode;

	/* Check whether CE is enabled */
	if( !ceEnabled ) {
		return;
	}

	state = rfalListenGetState(&dataFlag, NULL);
	if (state != statePrev) {
		// new state
		printf_dbg("rLGS %d\r\n", state);
		statePrev = state;
	}
	tStateCur = rfalGetTransceiveState( );
	if (tStateCur != tStatePrev) {
		// new state
		printf_dbg("rGTS %d\r\n", tStateCur);
		tStatePrev = tStateCur;
	}


	switch (state) {
	// ------------------------------------------------------------------
	// NFC A
	// ------------------------------------------------------------------
	//
	case RFAL_LM_STATE_ACTIVE_A:
	case RFAL_LM_STATE_ACTIVE_Ax: {
		if(dataFlag == true) {
			if (rfalIsoDepIsRats(rxBuf_ce, rfalConvBitsToBytes(rxRcvdLen))) {
				if (user_tag_properties.level4_enabled) {
					printf_dbg("RATS!\r\n");

					// enter next already state
					rfalListenSetState(RFAL_LM_STATE_CARDEMU_4A);

					// prepare for RATS
					rfalIsoDepListenActvParam rxParam;
					rxParam.rxBuf = rxBufA;
					rxParam.rxLen = &rxRcvdLen;
					rxParam.isoDepDev = NULL;
					rxParam.isRxChaining = &bIsRxChaining;
					//
					isoDepTxRxParam.FSx = rfalIsoDepFSxI2FSx(atsParam.fsci);
					rfalIsoDepListenStartActivation( &atsParam, NULL, rxBuf_ce, rxRcvdLen, rxParam );
				} else {
					// idle/halt state for all the commands we "don't support"

//					int i, rxSize;
					if (rxRcvdLen > 0) {
						printf_dbg("RATS ignored...\r\n");

//						rxSize = rfalConvBitsToBytes(rxRcvdLen);
//						for (i = 0; i < (rxSize > 16? 16 : rxSize); i++)
//							printf_dbg(" %02X", rxBuf_ce[i]);
//						printf_dbg("\r\n");

						if (state == RFAL_LM_STATE_ACTIVE_Ax) {
							rfalListenSleepStart( RFAL_LM_STATE_SLEEP_A, rxBuf_ce, RX_BUF_LENGTH, &rxRcvdLen );
						} else {
							rfalListenSetState(RFAL_LM_STATE_IDLE);
						}

					}
				}
			} else if (true == rfalNfcaListenerIsSleepReq( rxBuf_ce, rfalConvBitsToBytes(rxRcvdLen) ) ) {
				printf_dbg("HLTA\r\n");
				rfalListenSleepStart( RFAL_LM_STATE_SLEEP_A, rxBuf_ce, RX_BUF_LENGTH, &rxRcvdLen );
			} else {
//				int i, rxSize;
				if (rxRcvdLen > 0) {
//					printf_dbg("Hndler A3 rx: ");
//
//					rxSize = rfalConvBitsToBytes(rxRcvdLen);
//					for (i = 0; i < (rxSize > 16? 16 : rxSize); i++)
//						printf_dbg(" %02X", rxBuf_ce[i]);
//					printf_dbg("\r\n");

					// not HLTA and not RATS - layer3 comms
					rxReady = true;
				}
			}
		} else {
			if(!isFirstA3_Frame) {
				// IMPORTANT: We can not call rfalGetTransceiveStatus before we did the first
				// call rfalStartTransceive
				retCode = rfalGetTransceiveStatus();
				if (retCode != ERR_BUSY) {
					switch(retCode) {
					case ERR_LINK_LOSS:
						rfalListenStop();
						rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
						rxReady = txReady = false;
						isFirstA3_Frame = true;
						ul_cwrite_page_set = 0;
						printf_dbg("LLoss1\r\n");
						break;

					case ERR_NONE:
						break;

					// all other error cases are simple ignored ..
					default:
						printf_dbg("A3 GetTransceiveStatus err is %d\r\n", retCode);
						break;
					}
				}
			}
		}
		break;
	}
	//
	case RFAL_LM_STATE_CARDEMU_4A : {
		if(!isActivatedA) {
			// finish card activation sequence
			retCode = rfalIsoDepListenGetActivationStatus();
			if (retCode != ERR_BUSY) {
				//
				switch(retCode) {
				case ERR_LINK_LOSS:
					rfalListenStop();
					rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
					isActivatedA = false;
					isFirstA3_Frame = true;
					ul_cwrite_page_set = 0;
					printf_dbg("LLoss2\r\n");
					break;

				case ERR_NONE:
					isActivatedA = true;
					break;

				// all other error cases are simple ignored ..
				default:
					printf_dbg("GetActivationStatus err is %d\r\n", retCode);

					break;
				}
			}
		} else {
			// process command internal
			if(CARDEMULATION_PROCESS_INTERNAL == (emuMode & CARDEMULATION_PROCESS_INTERNAL)) {
				retCode = rfalIsoDepGetTransceiveStatus();
				switch (retCode) {
				case ERR_LINK_LOSS:
					rfalListenStop();
					rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
					rxReady = txReady = isActivatedA = false;
					break;

				case ERR_AGAIN:
					// handle RX chaining..
					// copy RX to my buffer
					// send via USB to Host -> blocking

					//rfalIsoDepStartTransceive(isoDepTxRxParam);
					break;

				case ERR_NONE:
					ceNfcaCardemu4A(emuMode, &isoDepTxRxParam);
					break;

				// all other error cases are simple ignored ..
				default:
					printf_dbg("1 GetTransceiveStatus err is %d\r\n", retCode);
					break;
				}
			}
		}
		break;
	}

	// ------------------------------------------------------------------
	// NFC F
	// ------------------------------------------------------------------
	//
	case RFAL_LM_STATE_READY_F: {
		if(dataFlag == true) {
			// data is already received, we can go on ..
			rfalListenSetState(RFAL_LM_STATE_CARDEMU_3);
			isFirstF_Frame = true;
		}
		break;
	}
	//
	case RFAL_LM_STATE_CARDEMU_3  : {
		if(isFirstF_Frame) {
			ceNfcfCardemu3(emuMode, &transceiveCtx);

			// NOTE: isFirstF_Frame is cleared after the first transmit is done.
			// For this mode this transmit is done within the rfalStartTransceive
			// function in ceNfcfCardemu3(..)

		} else {

			// IMPORTANT: We can not call rfalGetTransceiveStatus before we did the first
			// call rfalStartTransceive
			retCode = rfalGetTransceiveStatus();
			if (retCode != ERR_BUSY) {
				switch(retCode) {
				case ERR_LINK_LOSS:
					rfalListenStop();
					rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
					rxReady = txReady = false;
					break;

				case ERR_NONE:
					ceNfcfCardemu3(emuMode, &transceiveCtx);
					break;

				// all other error cases are simple ignored ..
				default:
					printf_dbg("2 GetTransceiveStatus err is %d\r\n", retCode);
					break;
				}
			}
		}
		break;
	}

//        case RFAL_LM_STATE_NOT_INIT   :
//        case RFAL_LM_STATE_POWER_OFF  :
//        case RFAL_LM_STATE_IDLE       :
//        case RFAL_LM_STATE_READY_A    :
//        case RFAL_LM_STATE_TARGET_A   :
//        case RFAL_LM_STATE_TARGET_F   :
//        case RFAL_LM_STATE_SLEEP_A    :
//        case RFAL_LM_STATE_SLEEP_B    :
//        case RFAL_LM_STATE_SLEEP_AF   :
//        case RFAL_LM_STATE_READY_Ax   :
	default:
		break;
	}
}

ReturnCode ceStart(const uint8_t *rxData, const uint16_t rxSize)
{
	//
	// rxData holds the emuMode and 4 TLV data sets (with TAG 4)
	//
	//   0         1              ..             ..             ..
	// | EmuMode | TLV Config A | TLV Config B | TLV Config F | ATS |

	//
	uint8_t *config[4];
	uint32_t maskValues[4] = {RFAL_LM_MASK_NFCA, RFAL_LM_MASK_NFCB, RFAL_LM_MASK_NFCF, 0};
	uint16_t offset = 0;
	configMask = 0;

	// get emu mode and increase offset to automatically
	// remove the emuMode byte when not needed anymore
	emuMode = *rxData;
	offset++;

	// extract config data
	int i;
	for (i = 0; i < 4; i++) {
		if (i < rxSize) {
			if (rxData[(i*2) + 1 + offset] > 0) {
				//
				config[i] = (uint8_t *)&rxData[(i*2) + 2 + offset];
				//
				configMask |= maskValues[i];
				offset += rxData[(i*2) + 1  + offset];
			} else {
				config[i] = NULL;
			}
		}
	}

	// store config's for later usage
	(config[0] != NULL) ? ST_MEMCPY(&configA, config[0], sizeof(configA)) : ST_MEMSET(&configA, 0, sizeof(configA));
	(config[1] != NULL) ? ST_MEMCPY(&configB, config[1], sizeof(configB)) : ST_MEMSET(&configB, 0, sizeof(configB));
	(config[2] != NULL) ? ST_MEMCPY(&configF, config[2], sizeof(configF)) : ST_MEMSET(&configF, 0, sizeof(configF));

	// finally check if ATS is received as well..
	if(config[3] != NULL) {
		atsParam.fsci = config[3][0];
		atsParam.fwi = config[3][1];
		atsParam.sfgi = config[3][2];
		atsParam.ta = config[3][3];
		atsParam.hbLen = config[3][4];
		atsParam.didSupport = false;
		ST_MEMCPY(histChar, &config[3][5], atsParam.hbLen);
		atsParam.hb = histChar;
	} else {
		ST_MEMSET(&atsParam, 0, sizeof(atsParam));
	}

	// .. and go ..
	ceEnabled = true;
	rxRcvdLen = 0;
	return rfalListenStart (configMask,
	                        (rfalLmConfPA *)config[0],
	                        (rfalLmConfPB *)config[1],
	                        (rfalLmConfPF *)config[2],
	                        rxBuf_ce,
	                        rfalConvBytesToBits(RX_BUF_LENGTH),
	                        &rxRcvdLen);
}

ReturnCode ceStop( void )
{
	rfalListenStop();
	ceInitalize();
	return ERR_NONE;
}

ReturnCode ceGetRx(const uint8_t cmd, uint8_t *txData, uint16_t *txSize)
{
	ReturnCode err = ERR_NOTFOUND;
	switch (cmd) {
	case CARDEMULATION_CMD_GET_RX_A:
		if(isActivatedA) {
			err = rfalIsoDepGetTransceiveStatus();

			switch (err) {
			case ERR_SLEEP_REQ:
			// TBC if ok going to idle state rather than to sleep state!
			case ERR_LINK_LOSS:
			default:
				rfalListenStop();
				rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
				rxReady = txReady = isActivatedA = false;
				isFirstA3_Frame = true;
				ul_cwrite_page_set = 0;
				printf_dbg("LLoss3 %d\r\n", err);
				break;

			case ERR_BUSY:           /* Transceive ongoing                              */
			case ERR_AGAIN:          /* Chaining packet received - handling to be added */
				break;

			case ERR_NONE:
				ST_MEMCPY(txData, isoDepTxRxParam.rxBuf->inf, *isoDepTxRxParam.rxLen);
				*txSize = *isoDepTxRxParam.rxLen;

				rxReady = false;
				txReady = true;
				break;
			}

		} else {
			// handle layer3 comms here
			if(isFirstA3_Frame) {
				// IMPORTANT: We can not call rfalGetTransceiveStatus before we did the first
				// call rfalStartTransceive

				err = ERR_NONE;

				if (rxReady) {
					ST_MEMCPY(txData, transceiveCtx.rxBuf, *transceiveCtx.rxRcvdLen);
					*txSize = rfalConvBitsToBytes(*transceiveCtx.rxRcvdLen);

					rxReady = false;
					txReady = true;
				} else {
					*txSize = 0;
					err = ERR_BUSY; // no data available yet
				}

				// NOTE: isFirstA3_Frame is cleared after the first transmit is done.
				// For this mode this transmit is done within the rfalStartTransceive
				// function in ceSetTx(..)

			} else {
				err = rfalGetTransceiveStatus();
				switch( err ) {

				case ERR_LINK_LOSS:
				default:
					rfalListenStop();
					rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
					rxReady = txReady = false;
					isFirstA3_Frame = true;
					ul_cwrite_page_set = 0;
					printf_dbg("LLoss4 %d\r\n", err);
					break;

				case ERR_BUSY:           /* Transceive ongoing   */
					break;

				case ERR_NONE:
					if (*transceiveCtx.rxRcvdLen > 0) {
						ST_MEMCPY(txData, transceiveCtx.rxBuf,
								*transceiveCtx.rxRcvdLen);
						*txSize = rfalConvBitsToBytes(*transceiveCtx.rxRcvdLen);

						rxReady = false;
						txReady = true;
					} else {
						err = ERR_BUSY; // no data available yet
					}
              	  break;
				}
			}
		}
		break;

	case CARDEMULATION_CMD_GET_RX_B:
		break;

	case CARDEMULATION_CMD_GET_RX_F:
		if(isFirstF_Frame) {
			// IMPORTANT: We can not call rfalGetTransceiveStatus before we did the first
			// call rfalStartTransceive

			err = ERR_NONE;
			ST_MEMCPY(txData, transceiveCtx.rxBuf, *transceiveCtx.rxRcvdLen);
			// this will transmit payload without CRC
			*txSize = txData[0];

			rxReady = false;
			txReady = true;

			// NOTE: isFirstF_Frame is cleared after the first transmit is done.
			// For this mode this transmit is done within the rfalStartTransceive
			// function in ceSetTx(..)

		} else {
			err = rfalGetTransceiveStatus();
			switch( err ) {

			case ERR_LINK_LOSS:
			default:
				rfalListenStop();
				rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
				rxReady = txReady = false;
				break;

			case ERR_BUSY:           /* Transceive ongoing   */
				break;

			case ERR_NONE:
				ST_MEMCPY(txData, transceiveCtx.rxBuf, *transceiveCtx.rxRcvdLen);
				// this will transmit payload without CRC
				*txSize = txData[0];

				rxReady = false;
				txReady = true;
				break;
			}
		}
		break;

	default:
		break;
	}

	return err;
}

ReturnCode ceSetTx(const uint8_t cmd, const uint8_t* rxData, const uint16_t rxSize, bool is_card_reset_needed)
{
	ReturnCode err = ERR_NOTFOUND;
	uint16_t rxSizeBytes = rfalConvBitsToBytes(rxSize);

	switch (cmd) {
	case CARDEMULATION_CMD_SET_TX_A:
		if(isActivatedA) {
			if (txReady) {
				ST_MEMCPY(isoDepTxRxParam.txBuf->inf, rxData, rxSizeBytes);
				isoDepTxRxParam.txBufLen = rxSizeBytes;
				*isoDepTxRxParam.rxLen = 0;
				err = rfalIsoDepStartTransceive(isoDepTxRxParam);

				if(err == ERR_NONE) {
					rxReady = txReady = false;
				}
			}
		} else {
			// handle layer3 comms here
			if (txReady) {
				ST_MEMCPY(transceiveCtx.txBuf, rxData, rxSizeBytes); // at least 1 byte for 1 bit
				transceiveCtx.txBufLen = rxSize; // in bits!
				*transceiveCtx.rxRcvdLen = 0;
				if (is_card_reset_needed) {
					// use blocking transmit if rxSize > 0 and then go to sleep
					if (rxSize > 0) {
						// options here are tailored for Mifare emulation where 4 bit responses are needed
						// todo see if there is a use case for several bytes tx and then sleep
						// and check if it works
            			transceiveCtx.flags = (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
            					(uint32_t)RFAL_TXRX_FLAGS_CRC_RX_REMV |		// RFAL_TXRX_FLAGS_CRC_RX_KEEP
								(uint32_t)RFAL_TXRX_FLAGS_NFCIP1_OFF |
								(uint32_t)RFAL_TXRX_FLAGS_AGC_ON |
								(uint32_t)RFAL_TXRX_FLAGS_PAR_RX_REMV |		// RFAL_TXRX_FLAGS_PAR_RX_KEEP
								(uint32_t)RFAL_TXRX_FLAGS_PAR_TX_NONE |
								(uint32_t)RFAL_TXRX_FLAGS_NFCV_FLAG_AUTO;

						// ReturnCode rfalTransceiveBlockingTx( uint8_t* txBuf, uint16_t txBufLen, uint8_t* rxBuf, uint16_t rxBufLen, uint16_t* actLen, uint32_t flags, uint32_t fwt )
						rfalStartTransceive(&transceiveCtx);
						do {
							rfalWorker();
							err = rfalGetTransceiveStatus();
						} while (rfalIsTransceiveInTx() && (err == ERR_BUSY));

						if (rfalIsTransceiveInRx()) {
							err = ERR_NONE;
						} else {
							printf_dbg("rfalGetTransceiveStatus %d\r\n", err);
						}
					}

					// sleep after NAK etc
					// todo distinguish between sleep and idle state here
					rfalListenSleepStart(RFAL_LM_STATE_SLEEP_A, rxBuf_ce, RX_BUF_LENGTH, &rxRcvdLen);
					//                                    rfalListenStop();
					//                                    rfalListenStart(configMask, &configA, &configB, &configF, rxBuf_ce, rfalConvBytesToBits(RX_BUF_LENGTH), &rxRcvdLen);
					rxReady = txReady = isActivatedA = false;
					isFirstA3_Frame = true;
					ul_cwrite_page_set = 0;
				} else {
					// card reset no needed

					if (rxSize < 8) {
						// options here are tailored for Mifare emulation where 4 bit responses are needed
						// todo check flags if rxSize == 0, so we don't need to tx but need to rx
						transceiveCtx.flags = (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL |
								(uint32_t)RFAL_TXRX_FLAGS_CRC_RX_REMV |		// RFAL_TXRX_FLAGS_CRC_RX_KEEP
								(uint32_t)RFAL_TXRX_FLAGS_NFCIP1_OFF |
								(uint32_t)RFAL_TXRX_FLAGS_AGC_ON |
								(uint32_t)RFAL_TXRX_FLAGS_PAR_RX_REMV |		// RFAL_TXRX_FLAGS_PAR_RX_KEEP
								(uint32_t)RFAL_TXRX_FLAGS_PAR_TX_NONE |
								(uint32_t)RFAL_TXRX_FLAGS_NFCV_FLAG_AUTO;
					} else {
						transceiveCtx.flags = RFAL_TXRX_FLAGS_DEFAULT;
					}

					err = rfalStartTransceive(&transceiveCtx);

					if (err == ERR_NONE) {
						// clear flag, after the first transmit (any other transceive is also not the first)
						isFirstA3_Frame = false;
						// clear rest of flags
						rxReady = txReady = false;
					}
				}

			}
		}
		break;

	case CARDEMULATION_CMD_SET_TX_B:
		break;

	case CARDEMULATION_CMD_SET_TX_F:
		if (txReady) {
			ST_MEMCPY(transceiveCtx.txBuf, rxData, rxSizeBytes);
			transceiveCtx.txBufLen = rfalConvBytesToBits(rxSizeBytes);
			*transceiveCtx.rxRcvdLen = 0;
			err = rfalStartTransceive(&transceiveCtx);

			if(err == ERR_NONE) {
				// clear flag, after the first transmit (any other transceive is also not the first)
				isFirstF_Frame = false;
				// clear rest of flags
				rxReady = txReady = false;
			}
		}
		break;
	default:
		break;
	}
	return err;
}

void ceNfcaCardemu4A(uint8_t mode, rfalIsoDepTxRxParam *pIsoDepTxRxParam)
{
	switch(mode) {
	case CARDEMULATION_MODE_REFLECT:
		// echo ..
		pIsoDepTxRxParam->txBuf = pIsoDepTxRxParam->rxBuf;
		pIsoDepTxRxParam->txBufLen = *(pIsoDepTxRxParam->rxLen);
		rfalIsoDepStartTransceive(*pIsoDepTxRxParam);
		break;

	case CARDEMULATION_MODE_NDEF:
	// Stub for internal NDEF processing

	default:
		break;
	}
}

void ceNfcfCardemu3(uint8_t mode, rfalTransceiveContext *ctx)
{
	switch(mode) {
	case CARDEMULATION_MODE_REFLECT:
		// echo ..
		ctx->txBuf = ctx->rxBuf;
		ctx->txBufLen = *ctx->rxRcvdLen;
		rfalStartTransceive(ctx);
		// clear flag, after the first transmit (any other transceive is also not the first)
		isFirstF_Frame = false;
		break;

	case CARDEMULATION_MODE_NDEF:
	// Stub for internal NDEF processing

	default:
		break;
	}
}

