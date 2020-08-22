/*
 * HydraBus/HydraNFC
 *
 * Copyright (C) 2020 Guillaume VINET
 * Copyright (C) 2014-2020 Benjamin VERNOUX
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
#include "tokenline.h"
#include "hydranfc_v2.h"
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
#include "hydranfc_v2_reader.h"
#include <string.h>

uint8_t tx_buffer[260];
uint8_t rx_buffer[260];
uint16_t tx_buffer_len, rx_buffer_len;
uint16_t tpdu_len;
uint32_t capdu_len;
uint8_t capdu_buffer[260];

uint16_t rapdu_len;
uint8_t rapdu_buffer[260];
bool verbose_mode = TRUE;
uint8_t fsd = 16;
rfalMode mode = RFAL_MODE_NONE;

void hydranfc_v2_reader_set_opt(t_hydra_console *con, int opt, int value){
	if(opt == T_CARD_CONNECT_AUTO_OPT_VERBOSITY){
		if(value != 0){
			verbose_mode = TRUE;
		}
		else{
			verbose_mode = FALSE;
		}
	} else if (opt == T_CARD_CONNECT_AUTO_OPT_ISO_14443_FRAME_SIZE){
		if( (0 < value) && (value <= 250)){
			fsd = value;
		}
		else{
			cprintf(con, "Error: value must be between 1 and 250.\r\n");
		}
	}
	else {
		cprintf(con, "Error: unknown parameter\r\n");
	}
}


#define EXIT_AND_DISPLAY_ON_ERR(con, r, f) \
    (r) = (f);            \
    if (ERR_NONE != (r))  \
    {                                 \
        cprintf(con, "Error %d\r\n", r);   \
    	return FALSE;                        \
    }

ReturnCode transceive_tx_rx(t_hydra_console *con,
							bool add_crc,
							bool verbose) {
	uint32_t flags;
	ReturnCode ret;

	if (verbose) {
		cprintf(con, "> ");
		pretty_print_hex_buf(con, tx_buffer, tx_buffer_len);
	}


	if (add_crc) {
		flags = RFAL_TXRX_FLAGS_DEFAULT;
	} else {
		flags = ((uint32_t) RFAL_TXRX_FLAGS_CRC_TX_MANUAL | (uint32_t) RFAL_TXRX_FLAGS_CRC_RX_KEEP);
	};
	// We try to detect a card...
	ret = rfalTransceiveBlockingTxRx(tx_buffer,
							   tx_buffer_len,
							   rx_buffer,
							   0xFF,
							   &rx_buffer_len,
							   flags,
							   216960U + 71680U + 71680U);

	if (verbose) {
		cprintf(con, "< ");
		pretty_print_hex_buf(con, rx_buffer, rx_buffer_len);
	}

	return ret;
}



typedef struct {
	uint8_t cid;
	uint8_t pcb;
	uint8_t nad;
	uint8_t len;
	uint8_t *inf;
} iso_14443_tpdu;

iso_14443_tpdu tpdu;

typedef struct {
	uint8_t block_size;
	uint8_t iblock_pcb_number;
	uint8_t pcb_block_number;
	uint8_t cid;
	bool add_cid;
	uint8_t nad;
	bool add_nad;

} iso_14443_session;
iso_14443_session session;

uint8_t get_and_update_iblock_pcb_number(void) {
	uint8_t pcb_number = session.iblock_pcb_number;
	session.iblock_pcb_number ^= 1;
	return pcb_number;
}

void get_iblock(uint8_t *inf, uint8_t len, bool chaining_block) {
	tpdu.pcb = get_and_update_iblock_pcb_number() + 0x02;

	if (session.add_cid) {
		tpdu.cid = session.cid;
		tpdu.pcb |= 0x08;
	}

	if (chaining_block) {
		tpdu.pcb |= 0x10;
	}

	if (session.add_nad) {
		tpdu.nad = session.nad;
		tpdu.pcb |= 0x04;
	}

	tpdu.inf = inf;
	tpdu.len = len;
}

void get_wtx_reply(void) {
	tpdu.len = 1;
	tpdu.inf[0] &= 0x3F;
}

void get_rblock(void) {
	tpdu.pcb = 0xB2;

	if (session.add_cid) {
		tpdu.pcb |= 0x08;
	}
	tpdu.pcb |= get_and_update_iblock_pcb_number();

	tpdu.len = 0;
}


void parse_r_tpdu(uint8_t *buffer, uint16_t len) {
	uint16_t index = 0;

	tpdu.pcb = buffer[index++];

	if (session.add_nad) {
		tpdu.nad = buffer[index++];
	}

	if (session.add_cid) {
		tpdu.cid = buffer[index++];
	}

	tpdu.len = (uint8_t)(len - index - 0);
	tpdu.inf = &(buffer[index]);
}

void send_tpdu(t_hydra_console *con) {

	tx_buffer_len = 0;

	tx_buffer[tx_buffer_len++] = tpdu.pcb;


	if (session.add_nad) {
		tx_buffer[tx_buffer_len++] = tpdu.nad;
	}

	if (session.add_cid) {
		tx_buffer[tx_buffer_len++] = tpdu.cid;
	}

	memcpy(&tx_buffer[tx_buffer_len], tpdu.inf, tpdu.len);

	tx_buffer_len += tpdu.len;

	transceive_tx_rx(con, TRUE, verbose_mode);

	parse_r_tpdu(rx_buffer, rx_buffer_len);
}

void init_iso_14443_session(void){
	session.block_size = fsd;
	session.add_nad = FALSE;
	session.add_cid = TRUE;
	session.iblock_pcb_number = 0;
	session.pcb_block_number = 0;
}

void send_apdu(t_hydra_console *con, uint8_t *apdu, uint32_t len) {
	uint8_t i;
	int r_len;

	init_iso_14443_session();

	i = 0;
	r_len = len;

	while (r_len > 0) {

		tpdu.inf = &(apdu[i]);
		tpdu.len = len < session.block_size ? len : session.block_size;
		r_len -= tpdu.len;

		get_iblock(&(apdu[i]),
				   tpdu.len,
				   (bool) (r_len > 0));
		len -= session.block_size;
		i += tpdu.len;
		send_tpdu(con);
	}

	if ((tpdu.pcb & 0xF0) == 0xF0) {
		get_wtx_reply();
		send_tpdu(con);
	}

	rapdu_len = 0;
	memcpy(&rapdu_buffer[rapdu_len], tpdu.inf, tpdu.len);
	rapdu_len += tpdu.len;

	while ((tpdu.pcb & 0x10) == 0x10) {
		get_rblock();
		send_tpdu(con);
		memcpy(&rapdu_buffer[rapdu_len], tpdu.inf, tpdu.len);
		rapdu_len += tpdu.len;
	}

	pretty_print_hex_buf(con, rapdu_buffer, rapdu_len);

}

bool connect_iso14443_a(t_hydra_console *con) {
	ReturnCode ret;

	rfalNfcaPollerInitialize();

	rfalFieldOff();
	rfalFieldOnAndStartGT();

	if (verbose_mode) {
		cprintf(con, "> 26\r\n");
	}
	EXIT_AND_DISPLAY_ON_ERR(con, ret, rfalNfcaPollerCheckPresence(0x26, (rfalNfcaSensRes *)rx_buffer));
	
	if (verbose_mode) {
		cprintf(con, "< ");
		pretty_print_hex_buf(con, rx_buffer, 2);
	}

	// 93 20
	tx_buffer[0] = 0x93;
	tx_buffer[1] = 0x20;
	tx_buffer_len = 2;

	// TODO understand why error 21 is raised
//	EXIT_AND_DISPLAY_ON_ERR(con, ret, transceive_tx_rx(con, FALSE, verbose_mode));
	transceive_tx_rx(con, FALSE, verbose_mode);
	if (rx_buffer_len == 0) {
		return FALSE;
	}

	tx_buffer[0] = 0x93;
	tx_buffer[1] = 0x70;
	memcpy(&tx_buffer[2], rx_buffer, 5);
	tx_buffer_len = 7;

	EXIT_AND_DISPLAY_ON_ERR(con, ret, transceive_tx_rx(con, TRUE, verbose_mode));

	tx_buffer[0] = 0xE0;
	tx_buffer[1] = 0x00;
	tx_buffer_len = 2;

	transceive_tx_rx(con, TRUE, verbose_mode);

	tx_buffer[0] = 0xD0;
	tx_buffer[1] = 0x01;
	tx_buffer_len = 2;

	transceive_tx_rx(con, TRUE, verbose_mode);

	return TRUE;

}

bool connect_iso14443_b(t_hydra_console *con) {
	ReturnCode ret;

	// We set set ISO 14443-B configuration
	rfalNfcbPollerInitialize();

	// We set off/on the field
	rfalFieldOff();
	rfalFieldOnAndStartGT();

	// We try to detect a card...
	tx_buffer[0] = 0x05;
	tx_buffer[1] = 0x00;
	tx_buffer[2] = 0x00;
	tx_buffer_len = 3;

	EXIT_AND_DISPLAY_ON_ERR(con, ret, transceive_tx_rx(con, TRUE, verbose_mode));

	tx_buffer[0] = 0x1D;
	memcpy(&tx_buffer[1], &rx_buffer[1], 4);
	tx_buffer[5] = 0x00;
	tx_buffer[6] = 0x00;
	tx_buffer[7] = 0x01;
	tx_buffer[8] = 0x00;

	tx_buffer_len = 9;

	EXIT_AND_DISPLAY_ON_ERR(con, ret, transceive_tx_rx(con, TRUE, verbose_mode));

	return TRUE;
}

void hydranfc_v2_reader_send(t_hydra_console *con, uint8_t *ascii_data) {

	buf_ascii2hex(ascii_data, capdu_buffer, &capdu_len);

	if( (mode == RFAL_MODE_POLL_NFCA) | (mode == RFAL_MODE_POLL_NFCB) ) {
		send_apdu(con, capdu_buffer, capdu_len);
	}
	else {
		cprintf(con, "Error no card detected with connect command.\r\n");
	}

}

void hydranfc_v2_reader_connect(t_hydra_console *con) {

	if (connect_iso14443_a(con)) {
		mode = RFAL_MODE_POLL_NFCA;
		cprintf(con, "ISO 14443-A card detected.\r\n");
	} else if (connect_iso14443_b(con)) {
		mode = RFAL_MODE_POLL_NFCB;
		cprintf(con, "ISO 14443-B card detected.\r\n");
	} else {
		mode = RFAL_MODE_NONE;
		cprintf(con, "No card detected.\r\n");
	}
}
