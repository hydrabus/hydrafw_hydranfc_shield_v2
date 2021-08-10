/*
 * HydraBus/HydraNFC v2
 *
 * Copyright (C) 2020/2021 Benjamin VERNOUX
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
#include "ch.h"
#include "common.h"
#include "tokenline.h"
#include "hydrabus_mode.h"
#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "ff.h"
#include "microsd.h"
#include "hydrabus_sd.h"
#include <string.h>
#include "bsp_uart.h"

#include "platform.h"
#include "logger.h"
#include "usart.h"
#include "spi.h"
#include "led.h"

//#include "usbd_custom_hid_if.h"
//#include "ce.h"
//#include "stream_dispatcher.h"
//#include "dispatcher.h"

#include "rfal_analogConfig.h"
#include "rfal_rf.h"
#include "rfal_dpo.h"
#include "rfal_chip.h"
#include "st25r3916.h"
#include "st25r3916_irq.h"
#include "st25r3916_aat.h"

#include "rfal_poller.h"

#include "hydranfc_v2.h"
#include "hydranfc_v2_ce.h"
#include "hydranfc_v2_reader.h"

#define MIFARE_DATA_MAX     20
/* Does not managed UID > 4+BCC to be done later ... */
#define MIFARE_UID_MAX      11
#define VICINITY_UID_MAX    16

#define MIFARE_ATQA_MAX (4)
#define MIFARE_SAK_MAX (4)
#define MIFARE_HALT_MAX (4)
#define MIFARE_UL_DATA_MAX (64)

#define NFC_TX_RAWDATA_BUF_SIZE (64)
extern unsigned char nfc_tx_rawdata_buf[NFC_TX_RAWDATA_BUF_SIZE+1];

/* Structure used & filled by hydranfc_scan_iso14443A() */
typedef struct {
	uint8_t atqa_buf_nb_rx_data;
	uint8_t atqa_buf[MIFARE_ATQA_MAX];

	uint8_t uid_buf_nb_rx_data;
	uint8_t uid_buf[MIFARE_UID_MAX];

	uint8_t sak1_buf_nb_rx_data;
	uint8_t sak1_buf[MIFARE_SAK_MAX];

	uint8_t sak2_buf_nb_rx_data;
	uint8_t sak2_buf[MIFARE_SAK_MAX];

	uint8_t halt_buf_nb_rx_data;
	uint8_t halt_buf[MIFARE_HALT_MAX];

	uint8_t mf_ul_data_nb_rx_data;
	uint8_t mf_ul_data[MIFARE_UL_DATA_MAX];
} t_hydranfc_scan_iso14443A;

bool hydranfc_init(t_hydra_console *con);
void hydranfc_cleanup(t_hydra_console *con);

void hydranfc_show_registers(t_hydra_console *con);

void hydranfc_scan_iso14443A(t_hydranfc_scan_iso14443A *data);

void hydranfc_scan_mifare(t_hydra_console *con);
void hydranfc_scan_vicinity(t_hydra_console *con);

void hydranfc_sniff_14443A(t_hydra_console *con, bool start_of_frame, bool end_of_frame, bool sniff_trace_uart1);
void hydranfc_sniff_14443A_bin(t_hydra_console *con, bool start_of_frame, bool end_of_frame, bool parity);
void hydranfc_sniff_14443AB_bin_raw(t_hydra_console *con, bool start_of_frame, bool end_of_frame);

void hydranfc_emul_mifare(t_hydra_console *con, uint32_t mifare_uid);

void hydranfc_emul_iso14443a(t_hydra_console *con);

void hydranfc_emul_mf_ultralight(t_hydra_console *con);
int hydranfc_emul_mf_ultralight_file(t_hydra_console *con, char* filename);

extern void hydranfc_ce_common(t_hydra_console *con, bool quiet);
extern uint32_t user_uid_len;
extern uint8_t user_uid[8];
extern uint8_t user_sak;
extern uint8_t *user_uri;

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos);
static int show(t_hydra_console *con, t_tokenline_parsed *p);

static thread_t *key_sniff_thread = NULL;
uint8_t globalCommProtectCnt;

static int nfc_obsv = 0; /* 0 = OFF, 1=ON */
static int nfc_obsv_tx = 0;
static int nfc_obsv_rx = 0;

static int nfc_aat_a = 0;
static int nfc_aat_b = 0;

/*
 * Works only on positive numbers !!
 */
static int round_int(float x)
{
	return (int)(x + 0.5f);
}

THD_FUNCTION(key_sniff, arg)
{
	int i;

	(void)arg;

	chRegSetThreadName("HydraNFC_v2 key-sniff");

	while (TRUE) {
		/* If K1_BUTTON is pressed */
		if (K1_BUTTON) {
			/* Wait Until K1_BUTTON is released */
			while(K1_BUTTON) {
				D1_ON;
				D2_OFF;
				D3_ON;
				D4_OFF;
				chThdSleepMilliseconds(100);

				D1_OFF;
				D2_OFF;
				D3_OFF;
				D4_OFF;
				chThdSleepMilliseconds(100);
			}

			/* Blink Fast */
			for(i = 0; i < 4; i++) {
				D1_ON;
				chThdSleepMilliseconds(25);
				D1_OFF;
				chThdSleepMilliseconds(25);
			}

			D1_ON;
			// TODO hydranfc_sniff_14443A
			//hydranfc_sniff_14443A(NULL, TRUE, FALSE, FALSE);
			D1_OFF;
		}

		/* If K2_BUTTON is pressed */
		if (K2_BUTTON) {
			/* Wait Until K2_BUTTON is released */
			while(K2_BUTTON) {
				D1_OFF;
				D2_ON;
				D3_OFF;
				D4_ON;
				chThdSleepMilliseconds(100);

				D1_OFF;
				D2_OFF;
				D3_OFF;
				D4_OFF;
				chThdSleepMilliseconds(100);
			}

			/* Blink Fast */
			for(i = 0; i < 4; i++) {
				D2_ON;
				chThdSleepMilliseconds(25);
				D2_OFF;
				chThdSleepMilliseconds(25);
			}
		}

		if (chThdShouldTerminateX()) {
			chThdExit((msg_t)1);
		}
		chThdSleepMilliseconds(100);
	}
}

#if 0
// TODO hydranfc_v2_read_mifare_ul
/* Return TRUE if success or FALSE if error */
int hydranfc_v2_read_mifare_ul(t_hydra_console *con, char* filename)
{
	int i;
	FRESULT err;
	FIL fp;
	uint8_t bcc;
	t_hydranfc_v2_scan_iso14443A* data;
	t_hydranfc_v2_scan_iso14443A data_buf;

	data = &data_buf;
	hydranfc_v2_scan_iso14443A(data);

	if(data->atqa_buf_nb_rx_data > 0) {
		cprintf(con, "ATQA:");
		for (i = 0; i < data->atqa_buf_nb_rx_data; i++)
			cprintf(con, " %02X", data->atqa_buf[i]);
		cprintf(con, "\r\n");
	}

	if(data->sak1_buf_nb_rx_data > 0) {
		cprintf(con, "SAK1:");
		for (i = 0; i < data->sak1_buf_nb_rx_data; i++)
			cprintf(con, " %02X", data->sak1_buf[i]);
		cprintf(con, "\r\n");
	}

	if(data->sak2_buf_nb_rx_data > 0) {
		cprintf(con, "SAK2:");
		for (i = 0; i < data->sak2_buf_nb_rx_data; i++)
			cprintf(con, " %02X", data->sak2_buf[i]);
		cprintf(con, "\r\n");
	}

	if(data->uid_buf_nb_rx_data > 0) {
		if(data->uid_buf_nb_rx_data >= 7) {
			cprintf(con, "UID:");
			for (i = 0; i < data->uid_buf_nb_rx_data ; i++) {
				cprintf(con, " %02X", data->uid_buf[i]);
			}
			cprintf(con, "\r\n");
		} else {
			cprintf(con, "UID:");
			bcc = 0;
			for (i = 0; i < data->uid_buf_nb_rx_data - 1; i++) {
				cprintf(con, " %02X", data->uid_buf[i]);
				bcc ^= data->uid_buf[i];
			}
			cprintf(con, " (BCC %02X %s)\r\n", data->uid_buf[i],
			        bcc == data->uid_buf[i] ? "ok" : "NOT OK");
		}
	}

	if (data->mf_ul_data_nb_rx_data > 0) {
#define ISO14443A_SEL_L1_CT 0x88 /* TX CT for 1st Byte */
		uint8_t expected_uid_bcc0;
		uint8_t obtained_uid_bcc0;
		uint8_t expected_uid_bcc1;
		uint8_t obtained_uid_bcc1;

		cprintf(con, "DATA:");
		for (i = 0; i < data->mf_ul_data_nb_rx_data; i++) {
			if(i % 16 == 0)
				cprintf(con, "\r\n");

			cprintf(con, " %02X", data->mf_ul_data[i]);
		}
		cprintf(con, "\r\n");

		/* Check Data UID with BCC */
		cprintf(con, "DATA UID:");
		for (i = 0; i < 3; i++)
			cprintf(con, " %02X", data->mf_ul_data[i]);
		for (i = 4; i < 8; i++)
			cprintf(con, " %02X", data->mf_ul_data[i]);
		cprintf(con, "\r\n");

		expected_uid_bcc0 = (ISO14443A_SEL_L1_CT ^ data->mf_ul_data[0] ^ data->mf_ul_data[1] ^ data->mf_ul_data[2]); // BCC1
		obtained_uid_bcc0 = data->mf_ul_data[3];
		cprintf(con, " (DATA BCC0 %02X %s)\r\n", expected_uid_bcc0,
		        expected_uid_bcc0 == obtained_uid_bcc0 ? "ok" : "NOT OK");

		expected_uid_bcc1 = (data->mf_ul_data[4] ^ data->mf_ul_data[5] ^ data->mf_ul_data[6] ^ data->mf_ul_data[7]); // BCC2
		obtained_uid_bcc1 = data->mf_ul_data[8];
		cprintf(con, " (DATA BCC1 %02X %s)\r\n", expected_uid_bcc1,
		        expected_uid_bcc1 == obtained_uid_bcc1 ? "ok" : "NOT OK");

		if( (expected_uid_bcc0 == obtained_uid_bcc0) && (expected_uid_bcc1 == obtained_uid_bcc1) ) {
			if (!is_fs_ready()) {
				err = mount();
				if(err) {
					cprintf(con, "Mount failed: error %d\r\n", err);
					return FALSE;
				}
			}

			if (!file_open(&fp, filename, 'w')) {
				cprintf(con, "Failed to open file %s\r\n", filename);
				return FALSE;
			}
			if(file_append(&fp, data->mf_ul_data, data->mf_ul_data_nb_rx_data)) {
				cprintf(con, "Failed to write file %s\r\n", filename);
				file_close(&fp);
				return FALSE;
			}
			if (!file_close(&fp)) {
				cprintf(con, "Failed to close file %s\r\n", filename);
				return FALSE;
			}
			cprintf(con, "write file %s with success\r\n", filename);
			return TRUE;
		} else {
			cprintf(con, "Error invalid BCC0/BCC1, file %s not written\r\n", filename);
			return FALSE;
		}
	} else {
		cprintf(con, "Error no data, file %s not written\r\n", filename);
		return FALSE;
	}
}
#endif

static void scan(t_hydra_console *con, nfc_technology_t nfc_tech)
{
	ScanTags(con, nfc_tech);
}

static void hydranfc_card_emul_iso14443a(t_hydra_console *con)
{
	uint8_t ascii_buf[48];

	// check if we have good uid and sak
	if (user_tag_properties.uid_len == 0) {
		cprintf(con, "Card Emulation failed: UID not set\r\n");
		return;
	}
	if (user_tag_properties.sak_len == 0) {
		cprintf(con, "Card Emulation failed: SAK not set\r\n");
		return;
	}

	if (user_tag_properties.level4_enabled) {
		switch (user_tag_properties.t4tEmulationMode) {
		case T4T_MODE_URI:
			if (user_tag_properties.uri == NULL) {
				cprintf(con, "Card Emulation failed: URI not set\r\n");
				return;
			}
			break;
		default:
			cprintf(con, "Card Emulation failed: unknown t4t mode %d\r\n", user_tag_properties.t4tEmulationMode);
			return;
		}
	}

	cprintf(con, "Card Emulation started using:\r\n");
	buf_hex2ascii(ascii_buf, user_tag_properties.uid, user_tag_properties.uid_len, "0x", " ");
	cprintf(con, "UID %s\r\n", ascii_buf);
	buf_hex2ascii(ascii_buf, user_tag_properties.sak, user_tag_properties.sak_len, "0x", " ");
	cprintf(con, "SAK %s\r\n", ascii_buf);
	if (user_tag_properties.t4tEmulationMode == T4T_MODE_URI && user_tag_properties.uri != NULL) {
		cprintf(con, "URI %s\r\n", user_tag_properties.uri);
	}

	/* Set st25r3916 IRQ function callback */
	hydranfc_v2_set_irq(st25r3916Isr);

	hydranfc_ce_common(con, FALSE);

	/* Reset st25r3916 IRQ function callback */
	hydranfc_v2_set_irq(NULL);
}

static int phase_degree(uint8_t phase_raw)
{
	return round_int( 17.0f + (((1.0f - ((float)(phase_raw)) / 255.0f)) * 146.0f) );
}

static int amplitude_mVpp(uint8_t amplitude_raw)
{
	return round_int(13.02f * (float)(amplitude_raw));
}

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos)
{
#define EXEC_CMD_LIST_MAX (5)
	int nfc_tech;
	mode_config_proto_t* proto = &con->mode->proto;
	int period, t;
	bool continuous;
	unsigned int mifare_uid = 0;
	filename_t sd_file;
	int str_offset;
	int action;
	int exec_cmd_list[EXEC_CMD_LIST_MAX] = { 0 };
	int exec_cmd_list_nb = 0;
	int exec_cmd_idx = 0;
	/*
		bool sniff_trace_uart1;
		bool sniff_raw;
		bool sniff_bin;
		bool sniff_frame_time;
		bool sniff_parity;
		sniff_trace_uart1 = FALSE;
		sniff_raw = FALSE;
		sniff_bin = FALSE;
		sniff_frame_time = FALSE;
		sniff_parity = FALSE;
	*/
	if(p->tokens[token_pos] == T_SD) {
		t = cmd_sd(con, p);
		return t;
	}

	/* Reset st25r3916 IRQ function callback */
	hydranfc_v2_set_irq(NULL);

	action = 0;
	period = 1000;
	continuous = FALSE;
	sd_file.filename[0] = 0;
	/* Parse all commands with parameters */
	for (t = token_pos; p->tokens[t]; t++) {
		switch (p->tokens[t]) {
		case T_SHOW:
			t += show(con, p);
			break;

		case T_NFC_OBSV: {
			int tmp_obsv_val = 0;
			t += 2;
			memcpy(&tmp_obsv_val, p->buf + p->tokens[t], sizeof(int));
			if( (tmp_obsv_val < 0) || (tmp_obsv_val > 1) ) {
				cprintf(con, "Invalid nfc-obsv value shall be 0(OFF) or 1(ON)\r\n");
				return t;
			}
			nfc_obsv = tmp_obsv_val;
			break;
		}

		case T_NFC_OBSV_TX: {
			int tmp_obsv_val = 0;
			t += 2;
			memcpy(&tmp_obsv_val, p->buf + p->tokens[t], sizeof(int));
			if( (tmp_obsv_val < 0) || (tmp_obsv_val > 255) ) {
				cprintf(con, "Invalid nfc-obsv-tx value (shall be between 0 and 255)\r\n");
				return t;
			}
			nfc_obsv_tx = tmp_obsv_val;
			break;
		}

		case T_NFC_OBSV_RX: {
			int tmp_obsv_val = 0;
			t += 2;
			memcpy(&tmp_obsv_val, p->buf + p->tokens[t], sizeof(int));
			if( (tmp_obsv_val < 0) || (tmp_obsv_val > 255) ) {
				cprintf(con, "Invalid nfc-obsv-rx value (shall be between 0 and 255)\r\n");
				return t;
			}
			nfc_obsv_rx = tmp_obsv_val;
			break;
		}

		case T_NFC_AAT_A: {
			int tmp_aat_val = 0;
			t += 2;
			memcpy(&tmp_aat_val, p->buf + p->tokens[t], sizeof(int));
			if( (tmp_aat_val < 0) || (tmp_aat_val > 255) ) {
				cprintf(con, "Invalid nfc-aat-a value (shall be between 0 and 255)\r\n");
				return t;
			}
			nfc_aat_a = tmp_aat_val;
			break;
		}

		case T_NFC_AAT_B: {
			int tmp_aat_val = 0;
			t += 2;
			memcpy(&tmp_aat_val, p->buf + p->tokens[t], sizeof(int));
			if( (tmp_aat_val < 0) || (tmp_aat_val > 255) ) {
				cprintf(con, "Invalid nfc-aat-b value (shall be between 0 and 255)\r\n");
				return t;
			}
			nfc_aat_b = tmp_aat_val;
			break;
		}

		case T_NFC_ALL:
			proto->config.hydranfc.nfc_technology = NFC_ALL;
			break;

		case T_NFC_A:
			proto->config.hydranfc.nfc_technology = NFC_A;
			break;

		case T_NFC_B:
			proto->config.hydranfc.nfc_technology = NFC_B;
			break;

		case T_NFC_ST25TB:
			proto->config.hydranfc.nfc_technology = NFC_ST25TB;
			break;

		case T_NFC_V:
			proto->config.hydranfc.nfc_technology = NFC_V;
			break;

		case T_NFC_F:
			proto->config.hydranfc.nfc_technology = NFC_F;
			break;

		case T_PERIOD:
			t += 2;
			memcpy(&period, p->buf + p->tokens[t], sizeof(int));
			break;

		case T_CONTINUOUS:
			continuous = TRUE;
			break;

		case T_FILE:
			/* Filename specified */
			memcpy(&str_offset, &p->tokens[t+3], sizeof(int));
			snprintf(sd_file.filename, FILENAME_SIZE, "0:%s", p->buf + str_offset);
			break;

		case T_SCAN:
			action = p->tokens[t];
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		case T_READ_MF_ULTRALIGHT:
			action = p->tokens[t];
			if (p->tokens[t+1] != T_ARG_STRING || p->tokens[t+3] != 0)
				return FALSE;
			memcpy(&str_offset, &p->tokens[t+2], sizeof(int));
			snprintf(sd_file.filename, FILENAME_SIZE, "0:%s", p->buf + str_offset);
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		case T_EMUL_MF_ULTRALIGHT:
		case T_CLONE_MF_ULTRALIGHT:
			action = p->tokens[t];
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		/*
				case T_TRACE_UART1:
					sniff_trace_uart1 = TRUE;
					break;

				case T_FRAME_TIME:
					sniff_frame_time = TRUE;
					break;

				case T_BIN:
					sniff_bin = TRUE;
					break;

				case T_PARITY:
					sniff_parity = TRUE;
					break;

				case T_RAW:
					sniff_raw = TRUE;
					break;
		*/
		case T_SNIFF:
			action = p->tokens[t];
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		case T_EMUL_MIFARE:
			action = p->tokens[t];
			t += 2;
			memcpy(&mifare_uid, p->buf + p->tokens[t], sizeof(int));
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		case T_EMUL_ISO14443A:
		case T_SET_EMUL_TAG_PROPERTIES:
			action = p->tokens[t];
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		case T_EMUL_T4T:
			action = p->tokens[t];
			if (p->tokens[t+1] == 0 || p->tokens[t+2] != 0) {
				cprintf(con, "Invalid parameter(s).\r\n");
				return t;
			}
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;

		case T_EMUL_TAG_PROPERTY_UID:
			//			printf_dbg("token 0=%d, 1=%d, 2=%d\n", p->tokens[t+0],p->tokens[t+1],p->tokens[t+2]);
			t += 2;
			if (p->tokens[t-1] == T_ARG_STRING) {
				memcpy(&str_offset, &p->tokens[t], sizeof(int));
				if (!buf_ascii2hex((uint8_t *)(p->buf + str_offset), user_tag_properties.uid, &user_tag_properties.uid_len)) {
					cprintf(con, "CE Properties: can't interpret UID\r\n");
					return t;
				} else if (!(user_tag_properties.uid_len == 4 || user_tag_properties.uid_len == 7)) {
					cprintf(con, "CE Properties: wrong UID length (need 4 or 7)\r\n");
					return t;
				}
			} else {
				cprintf(con, "CE Properties: Invalid UID\r\n");
				return t;
			}
			break;
		case T_EMUL_TAG_PROPERTY_SAK:
			t += 2;
			if (p->tokens[t-1] == T_ARG_STRING) {
				memcpy(&str_offset, &p->tokens[t], sizeof(int));
				if (!buf_ascii2hex((uint8_t *)(p->buf + str_offset), user_tag_properties.sak, &user_tag_properties.sak_len)) {
					cprintf(con, "CE Properties: can't interpret SAK\r\n");
					return t;
				} else if (user_tag_properties.sak_len != 1) {
					cprintf(con, "CE Properties: wrong SAK length (need 1)\r\n");
					return t;
				}
			} else {
				cprintf(con, "CE Properties: Invalid SAK\r\n");
				return t;
			}
			break;
		case T_EMUL_TAG_PROPERTY_URI:
			//			printf_dbg("uri: token 0=%d, 1=%d, 2=%d\n", p->tokens[t+0],p->tokens[t+1],p->tokens[t+2]);
			if (p->tokens[t+1] == T_ARG_STRING) {
				memcpy(&str_offset, &p->tokens[t+2], sizeof(int));
				hydranfc_ce_set_uri((uint8_t *)(p->buf + str_offset));
				t += 2;
			} else {
				user_tag_properties.t4tEmulationMode = T4T_MODE_URI;
			}
			break;

		case T_CARD_CONNECT_AUTO:
		case T_CARD_CONNECT_AUTO_OPT:
		case T_CARD_SEND:
		case T_SET_NFC_OBSV:
		case T_GET_NFC_OBSV:
		case T_NFC_TUNE_AUTO:
		case T_SET_NFC_TUNE:
		case T_GET_NFC_TUNE: {
			action = p->tokens[t];
			if(exec_cmd_list_nb < EXEC_CMD_LIST_MAX) {
				exec_cmd_list[exec_cmd_list_nb] = action;
				exec_cmd_list_nb++;
			}
			break;
		}

		case T_CARD_CONNECT_AUTO_OPT_VERBOSITY:
		case T_CARD_CONNECT_AUTO_OPT_ISO_14443_FRAME_SIZE: {
			t += 2;
			if (p->tokens[t-1] == T_ARG_UINT) {
				int value;
				memcpy(&value, p->buf + p->tokens[t], sizeof(int));
				hydranfc_v2_reader_set_opt(con, p->tokens[t-2], value);
			} else {
				cprintf(con, "Incorrect value\r\n");
				return t;
			}
			break;
		}

		break;
		}
	}

	/* Execute commands */
	for(exec_cmd_idx = 0; exec_cmd_idx < exec_cmd_list_nb; exec_cmd_idx++) {
		switch(exec_cmd_list[exec_cmd_idx]) {

		case T_SET_NFC_OBSV: {
			cprintf(con, "nfc-obsv = %d\r\n", nfc_obsv);
			cprintf(con, "nfc-obsv-tx = %d / 0x%02X\r\n", nfc_obsv_tx, nfc_obsv_tx);
			cprintf(con, "nfc-obsv-rx = %d / 0x%02X\r\n", nfc_obsv_rx, nfc_obsv_rx);
			if(0 == nfc_obsv) {
				rfalDisableObsvMode();
				cprintf(con, "NFC Observation mode disabled\r\n");
			}

			if(1 == nfc_obsv) {
				rfalSetObsvMode(nfc_obsv_tx, nfc_obsv_rx);
				cprintf(con, "NFC Observation mode enabled\r\n");
			}
		}
		break;

		case T_GET_NFC_OBSV: {
			uint8_t rfal_obsv_tx;
			uint8_t rfal_obsv_rx;

			cprintf(con, "nfc-obsv = %d\r\n", nfc_obsv);
			cprintf(con, "nfc-obsv-tx = %d / 0x%02X\r\n", nfc_obsv_tx, nfc_obsv_tx);
			cprintf(con, "nfc-obsv-rx = %d / 0x%02X\r\n", nfc_obsv_rx, nfc_obsv_rx);

			rfalGetObsvMode(&rfal_obsv_tx, &rfal_obsv_rx);
			cprintf(con, "rfal_obsv_tx = %d / 0x%02X\r\n", rfal_obsv_tx, rfal_obsv_tx);
			cprintf(con, "rfal_obsv_rx = %d / 0x%02X\r\n", rfal_obsv_rx, rfal_obsv_rx);
		}
		break;

		case T_NFC_TUNE_AUTO: {
			struct st25r3916AatTuneResult tuningStatus;
			ReturnCode err;
			uint8_t aat_a;  /*!< serial cap */
			uint8_t aat_b;  /*!< parallel cap */
			uint8_t amplitude;
			uint8_t phase;
			amplitude = 0;
			phase = 0;

			err = st25r3916AatTune(NULL, &tuningStatus);
			if (ERR_NONE == err) {
				/* Get amplitude and phase */
				err = rfalChipMeasureAmplitude(&amplitude);
				if (ERR_NONE == err) {
					err = rfalChipMeasurePhase(&phase);
					if (ERR_NONE == err) {
						st25r3916ReadRegister(ST25R3916_REG_ANT_TUNE_A, &aat_a);
						st25r3916ReadRegister(ST25R3916_REG_ANT_TUNE_B, &aat_b);

						cprintf(con, "nfc-tune-auto results after tuning:\r\n");
						cprintf(con, "aat_a = %d / 0x%02X\r\n", aat_a, aat_a);
						cprintf(con, "aat_b = %d / 0x%02X\r\n", aat_b, aat_b);
						cprintf(con, "phase = %d° (%d / 0x%02X)\r\n", phase_degree(phase), phase, phase);
						cprintf(con, "amplitude = %dmVpp (%d / 0x%02X)\r\n", amplitude_mVpp(amplitude), amplitude, amplitude);
						cprintf(con, "measureCnt = %d\r\n", tuningStatus.measureCnt);
					} else {
						cprintf(con, "rfalChipMeasurePhase Error %d\r\n", err);
					}
				} else {
					cprintf(con, "rfalChipMeasureAmplitude Error %d\r\n", err);
				}
			} else {
				cprintf(con, "st25r3916AatTune Error %d\r\n", err);
			}
		}
		break;

		case T_SET_NFC_TUNE: {
#define ST25R3916_AAT_CAP_DELAY_MAX 10  /*!< Max Variable Capacitor settle delay */
			ReturnCode err;
			uint8_t aat_a;  /*!< serial cap */
			uint8_t aat_b;  /*!< parallel cap */
			uint8_t amplitude;
			uint8_t phase;
			amplitude = 0;
			phase = 0;

			st25r3916WriteRegister(ST25R3916_REG_ANT_TUNE_A, nfc_aat_a);
			st25r3916WriteRegister(ST25R3916_REG_ANT_TUNE_B, nfc_aat_b);

			/* Wait till caps have settled.. */
			platformDelay(ST25R3916_AAT_CAP_DELAY_MAX);

			/* Get amplitude and phase */
			err = rfalChipMeasureAmplitude(&amplitude);
			if (ERR_NONE == err) {
				err = rfalChipMeasurePhase(&phase);
				if (ERR_NONE == err) {
					st25r3916ReadRegister(ST25R3916_REG_ANT_TUNE_A, &aat_a);
					st25r3916ReadRegister(ST25R3916_REG_ANT_TUNE_B, &aat_b);

					cprintf(con, "set-nfc-tune variables:\r\n");
					cprintf(con, "nfc-aat-a = %d / 0x%02X\r\n", nfc_aat_a, nfc_aat_a);
					cprintf(con, "nfc-aat-a = %d / 0x%02X\r\n", nfc_aat_b, nfc_aat_b);
					cprintf(con, "set-nfc-tune results after tuning:\r\n");
					cprintf(con, "aat_a = %d / 0x%02X\r\n", aat_a, aat_a);
					cprintf(con, "aat_b = %d / 0x%02X\r\n", aat_b, aat_b);
					cprintf(con, "phase = %d° (%d / 0x%02X)\r\n", phase_degree(phase), phase, phase);
					cprintf(con, "amplitude = %dmVpp (%d / 0x%02X)\r\n", amplitude_mVpp(amplitude), amplitude, amplitude);
				} else {
					cprintf(con, "rfalChipMeasurePhase Error %d\r\n", err);
				}
			} else {
				cprintf(con, "rfalChipMeasureAmplitude Error %d\r\n", err);
			}
		}
		break;

		case T_GET_NFC_TUNE: {
			ReturnCode err;
			uint8_t aat_a;  /*!< serial cap */
			uint8_t aat_b;  /*!< parallel cap */
			uint8_t amplitude;
			uint8_t phase;
			amplitude = 0;
			phase = 0;

			/* Get amplitude and phase */
			err = rfalChipMeasureAmplitude(&amplitude);
			if (ERR_NONE == err) {
				err = rfalChipMeasurePhase(&phase);
				if (ERR_NONE == err) {
					st25r3916ReadRegister(ST25R3916_REG_ANT_TUNE_A, &aat_a);
					st25r3916ReadRegister(ST25R3916_REG_ANT_TUNE_B, &aat_b);

					cprintf(con, "get-nfc-tune variables:\r\n");
					cprintf(con, "nfc-aat-a = %d / 0x%02X\r\n", nfc_aat_a, nfc_aat_a);
					cprintf(con, "nfc-aat-a = %d / 0x%02X\r\n", nfc_aat_b, nfc_aat_b);
					cprintf(con, "get-nfc-tune registers:\r\n");
					cprintf(con, "aat_a = %d / 0x%02X\r\n", aat_a, aat_a);
					cprintf(con, "aat_b = %d / 0x%02X\r\n", aat_b, aat_b);
					cprintf(con, "phase = %d° (%d / 0x%02X)\r\n", phase_degree(phase), phase, phase);
					cprintf(con, "amplitude = %dmVpp (%d / 0x%02X)\r\n", amplitude_mVpp(amplitude), amplitude, amplitude);
				} else {
					cprintf(con, "rfalChipMeasurePhase Error %d\r\n", err);
				}
			} else {
				cprintf(con, "rfalChipMeasureAmplitude Error %d\r\n", err);
			}
		}
		break;

		case T_SCAN: {
			nfc_technology_str_t tag_tech_str;
			nfc_tech = proto->config.hydranfc.nfc_technology;

			/* Set st25r3916 IRQ function callback */
			hydranfc_v2_set_irq(st25r3916Isr);

			nfc_technology_to_str(nfc_tech, &tag_tech_str);
			if (continuous) {
				cprintf(con, "Scanning NFC-%s ", tag_tech_str.str);
				cprintf(con, "with %dms period. Press user button to stop.\r\n", period);
				while (!hydrabus_ubtn()) {
					scan(con, nfc_tech);
					chThdSleepMilliseconds(period);
				}
			} else {
				scan(con, nfc_tech);
			}

			/* Reset st25r3916 IRQ function callback */
			hydranfc_v2_set_irq(NULL);
			break;
		}

		case T_READ_MF_ULTRALIGHT:
			cprintf(con, "T_READ_MF_ULTRALIGHT not implemented.\r\n");
			// TODO T_READ_MF_ULTRALIGHT
			//hydranfc_v2_read_mifare_ul(con, sd_file.filename);
			break;

		case T_EMUL_MF_ULTRALIGHT:
			cprintf(con, "Mifare Ultralight card emulation.\r\n");
			if(sd_file.filename[0] != 0)
			{
				cprintf(con, "Using image file %s.\r\n", sd_file.filename);
				user_tag_properties.ce_image_filename = sd_file.filename;
			}else
			{
				cprintf(con, "Using 'factory' image file.\r\n");
				user_tag_properties.ce_image_filename = NULL;
			}
			user_tag_properties.l3EmulationMode = L3_MODE_MIF_UL;
			hydranfc_card_emul_iso14443a(con);
			user_tag_properties.l3EmulationMode = L3_MODE_NOT_SET;
			user_tag_properties.ce_image_filename = NULL;
			break;

		case T_CLONE_MF_ULTRALIGHT:
			cprintf(con, "T_CLONE_MF_ULTRALIGHT not implemented.\r\n");
			break;

		case T_SNIFF:
			// TODO T_SNIFF
			cprintf(con, "T_SNIFF not implemented.\r\n");
#if 0
			if(sniff_bin) {
				if(sniff_raw) {
					/* Sniffer Binary RAW UART1 only */
					hydranfc_sniff_14443AB_bin_raw(con, sniff_frame_time, sniff_frame_time);
				} else {
					/* Sniffer Binary UART1 only */
					hydranfc_sniff_14443A_bin(con, sniff_frame_time, sniff_frame_time, sniff_parity);
				}
			} else {
				if(sniff_raw) {
					/* Sniffer Binary RAW UART1 only */
					hydranfc_sniff_14443AB_bin_raw(con, sniff_frame_time, sniff_frame_time);
				} else {
					/* Sniffer ASCII */
					if(sniff_trace_uart1) {
						if(sniff_frame_time)
							cprintf(con, "frame-time disabled for trace-uart1 in ASCII\r\n");
						hydranfc_sniff_14443A(con, FALSE, FALSE, TRUE);
					} else {
						hydranfc_sniff_14443A(con, sniff_frame_time, sniff_frame_time, FALSE);
					}
				}
			}
#endif
			break;

		case T_EMUL_MIFARE:
			cprintf(con, "T_EMUL_MIFARE not implemented.\r\n");
			// TODO T_EMUL_MIFARE
			//hydranfc_emul_mifare(con, mifare_uid);
			break;

		case T_EMUL_ISO14443A:
			cprintf(con, "ISO14443A card emulation.\r\n");
			hydranfc_card_emul_iso14443a(con);
			break;

		case T_EMUL_T4T:
			cprintf(con, "Type 4 Tag emulation.\r\n");
			user_tag_properties.level4_enabled = true;
			hydranfc_card_emul_iso14443a(con);
			user_tag_properties.level4_enabled = false;
			break;

		case T_CARD_CONNECT_AUTO: {
			/* Set st25r3916 IRQ function callback */
			hydranfc_v2_set_irq(st25r3916Isr);

			hydranfc_v2_reader_connect(con);

			/* Reset st25r3916 IRQ function callback */
			hydranfc_v2_set_irq(NULL);

			break;
		}

		case T_CARD_SEND: {
			/* Set st25r3916 IRQ function callback */
			hydranfc_v2_set_irq(st25r3916Isr);

			hydranfc_v2_reader_send(con, (uint8_t *) p->buf);

			/* Reset st25r3916 IRQ function callback */
			hydranfc_v2_set_irq(NULL);
			break;
		}

		default:
			break;
		}
	}

	return t - token_pos;
}

static void show_registers(t_hydra_console *con)
{
	ReturnCode err;
	unsigned int i;
	t_st25r3916Regs regDump;
	/* Translation of Reg Space B index to real register address */
	const uint8_t reg_space_b_idx_to_addr[ST25R3916_SPACE_B_REG_LEN] = 
	{
		0x05,	// Index 0 
		0x06,	// Index 1 
		0x0B,	// Index 2 
		0x0C,	// Index 3 
		0x0D,	// Index 4 
		0x0F,	// Index 5 
		0x15,	// Index 6 
		0x28,	// Index 7 
		0x29,	// Index 8 
		0x2A,	// Index 9 
		0x2B,	// Index 10
		0x2C,	// Index 11
		0x30,	// Index 12
		0x31,	// Index 13
		0x32,	// Index 14
		0x33	// Index 15
	};

	err = st25r3916GetRegsDump(&regDump);
	if(err != ERR_NONE) {
		cprintf(con, "st25r3916GetRegsDump() error=%d\r\n", err);
	}

	/* ST25R3916 space A */
	cprintf(con, "ST25R3916 Registers space A:\r\n");
	for (i = 0; i < ST25R3916_REG_IC_IDENTITY+1; i++) {
		cprintf(con, "0x%02x\t: 0x%02x\r\n", i,	regDump.RsA[i]);
	}
	cprintf(con, "\r\n");
	/* ST25R3916 space B */
	cprintf(con, "ST25R3916 Registers space B:\r\n");
	for (i = 0; i < ST25R3916_SPACE_B_REG_LEN; i++) {
		cprintf(con, "0x%02x\t: 0x%02x\r\n", reg_space_b_idx_to_addr[i], regDump.RsB[i]);
	}
}

static int show(t_hydra_console *con, t_tokenline_parsed *p)
{
	mode_config_proto_t* proto = &con->mode->proto;
	int tokens_used;
	nfc_technology_str_t tag_tech_str;

	tokens_used = 0;
	if (p->tokens[1] == T_REGISTERS) {
		tokens_used++;
		show_registers(con);
	} else {
		nfc_technology_to_str(proto->config.hydranfc.nfc_technology, &tag_tech_str);
		cprintf(con, "Selected technology: NFC-%s\r\n", tag_tech_str.str);
	}

	return tokens_used;
}

static const char *get_prompt(t_hydra_console *con)
{
	(void)con;
	return "NFCv2" PROMPT;
}

static int init(t_hydra_console *con, t_tokenline_parsed *p)
{
	mode_config_proto_t* proto;
	int tokens_used = 0;

	memset(&user_tag_properties, 0, sizeof(user_tag_properties));

	if(con != NULL) {
		proto = &con->mode->proto;
		proto->config.hydranfc.nfc_technology = NFC_ALL;
	}

	if(hydranfc_v2_init(con, st25r3916Isr) ==  FALSE) {
		return tokens_used;
	}

	if(key_sniff_thread == NULL) {
		/*
				key_sniff_thread = chThdCreateFromHeap(NULL,
								      8192,
								      "key_sniff",
								      NORMALPRIO,
								      key_sniff,
								      NULL);
		*/
	}

	/* Process cmdline arguments, skipping "nfc". */
	if(p != NULL) {
		if(con != NULL) {
			tokens_used = 1 + exec(con, p, 1);
		}
	}

	return tokens_used;
}


/** \brief DeInit/Cleanup HydraNFC functions
 *
 * \param con t_hydra_console*: hydra console (optional can be NULL if unused)
 * \return void
 *
 */
void hydranfc_cleanup(t_hydra_console *con)
{
	if(key_sniff_thread != NULL) {
		chThdTerminate(key_sniff_thread);
		chThdWait(key_sniff_thread);
		key_sniff_thread = NULL;
	}

	hydranfc_v2_cleanup(con);
}

/** \brief Init HydraNFC functions
 *
 * \param con t_hydra_console*: hydra console (optional can be NULL if unused)
 * \return bool: return TRUE if success or FALSE if failure
 *
 */
bool hydranfc_init(t_hydra_console *con)
{
	if(con != NULL) {
		/* Defaults */
		/*
		mode_config_proto_t* proto = &con->mode->proto;
		proto->dev_num = 0;
		proto->config.uart.dev_speed = 115200;
		proto->config.uart.dev_parity = 0;
		proto->config.uart.dev_stop_bit = 1;
		proto->config.uart.bus_mode = BSP_UART_MODE_UART;
		bsp_uart_init(proto->dev_num, proto);
		*/
	}
	init(con, NULL);

	return TRUE;
}

const mode_exec_t mode_nfc_exec = {
	.init = &init,
	.exec = &exec,
	.cleanup = &hydranfc_cleanup,
	.get_prompt = &get_prompt,
};
