/*
 * HydraBus/HydraNFC
 *
 * Copyright (C) 2020 Guillaume VINET
 * Copyright (C) 2014-2021 Benjamin VERNOUX
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
#include <string.h>
#include "common.h"
#include "tokenline.h"
#include "rfal_rf.h"
#include "hydrabus_bbio.h"
#include "hydranfc_v2_bbio_reader.h"
#include "st25r3916.h"
#include "st25r3916_irq.h"
#include "st25r3916_com.h"
#include "rfal_analogConfig.h"
#include "rfal_rf.h"
#include "rfal_nfca.h"
#include "rfal_nfcb.h"
#include "rfal_nfcv.h"

#include "hydranfc_v2_common.h"
#include "hydranfc_v2_nfc_mode.h"

extern t_mode_config mode_con1;

static void bbio_mode_id(t_hydra_console *con)
{
	cprint(con, BBIO_HYDRANFC_READER, 4);
}

void bbio_mode_hydranfc_v2_reader(t_hydra_console *con)
{
	uint8_t bbio_subcommand, rlen, clen, compute_crc;
	uint16_t rec_len;
	uint8_t *rx_data = (uint8_t *) g_sbuf + 4096;
	uint32_t flags;

	hydranfc_v2_init(con, st25r3916Isr);

	bbio_mode_id(con);

	while (!hydrabus_ubtn()) {

		if (chnRead(con->sdu, &bbio_subcommand, 1) == 1) {
			switch (bbio_subcommand) {
			case BBIO_NFC_SET_MODE_ISO_14443A: {
				rfalNfcaPollerInitialize();
				break;
			}
			case BBIO_NFC_SET_MODE_ISO_14443B: {
				rfalNfcbPollerInitialize();
				break;
			}
			case BBIO_NFC_SET_MODE_ISO_15693: {
				rfalNfcvPollerInitialize();
				break;
			}
			case BBIO_NFC_RF_OFF: {
				rfalFieldOff();
				break;
			}
			case BBIO_NFC_RF_ON: {
				rfalFieldOnAndStartGT(); /* Turns the Field On and starts GT timer */
				break;
			}

			case BBIO_NFC_ISO_14443_A_REQA: {

				rfalNfcaPollerCheckPresence(0x26, (rfalNfcaSensRes*)rx_data );

				rlen = 2;
				cprint(con, (char *) &rlen, 1);
				cprint(con, (char *) rx_data, rlen);
				break;
			}

			case BBIO_NFC_CMD_SEND_BITS: {
				// TODO
				break;
			}
			case BBIO_NFC_CMD_SEND_BYTES: {

				chnRead(con->sdu, &compute_crc, 1);
				chnRead(con->sdu, &clen, 1);
				chnRead(con->sdu, rx_data, clen);

				if( compute_crc) {
					flags = RFAL_TXRX_FLAGS_DEFAULT;
				} else {
					flags = ( (uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL | (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP );
				}
				rfalTransceiveBlockingTxRx( &rx_data[0],
				                            clen,
				                            rx_data,
				                            0xFF,
				                            &rec_len,
				                            flags,
				                            216960U + 71680U );
				rlen = (uint8_t) (rec_len & 0xFF);
				cprint(con, (char *) &rlen, 1);
				cprint(con, (char *) rx_data, rlen);
				break;
			}
			case BBIO_RESET: {
				hydranfc_v2_cleanup(con);
				return;
			}
			}
		}
	}
}
