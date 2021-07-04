/*
 * HydraBus/HydraNFC
 *
 * Copyright (C) 2014-2020 Benjamin VERNOUX
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

#include "hydranfc_v2_nfc_mode.h"
#include "hydranfc_v2_dnfc_mode.h"

#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "common.h"
#include <string.h>

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
#include "st25r3916_com.h"

#include "rfal_poller.h"
#include "rfal_nfca.h"

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos);
static int show(t_hydra_console *con, t_tokenline_parsed *p);

static const char *str_pins_spi2 = {
	"CS:   PC1 (SW)\r\nSCK:  PB10\r\nMISO: PC2\r\nMOSI: PC3\r\nIRQ: PA1\r\n"
};
static const char *str_prompt_dnfc2 = {"dnfc2" PROMPT};

static const char *str_bsp_init_err = {"bsp_spi_init() error %d\r\n"};

#define MODE_DEV_NB_ARGC ((int)ARRAY_SIZE(mode_dev_arg)) /* Number of arguments/parameters for this mode */

#define SPEED_NB (8)
static uint32_t speeds[SPEED_NB] = {
	/* HydraNFC Shield v2 use SPI2 */
	160000,
	320000,
	650000,
	1310000,
	2620000,
	5250000,
	10500000,
	21000000,
};

static volatile int irq_count;
static volatile int irq;
static volatile int irq_end_rx;

static void (*st25r3916_irq_fn)(void) = NULL;

static int nfc_mode = RFAL_MODE_NONE;
static int nfc_mode_tx_br = RFAL_BR_106;
static int nfc_mode_rx_br = RFAL_BR_106;

static int nfc_obsv = 0; /* 0 = OFF, 1=ON */
static int nfc_obsv_tx = 0;
static int nfc_obsv_rx = 0;

/* Triggered when the Ext IRQ is pressed or released. */
static void extcb1(void *arg)
{
	(void)arg;

	if (st25r3916_irq_fn != NULL)
		st25r3916_irq_fn();

	irq_count++;
	irq = 1;
}

static void show_params(t_hydra_console *con)
{
	uint8_t i, cnt;
	mode_config_proto_t *proto = &con->mode->proto;

	cprintf(con, "Device: SPI2\r\nGPIO resistor: %s\r\nMode: %s\r\n"
	        "Frequency: ",
	        proto->config.spi.dev_gpio_pull
	        == MODE_CONFIG_DEV_GPIO_PULLUP ?
	        "pull-up" :
	        proto->config.spi.dev_gpio_pull
	        == MODE_CONFIG_DEV_GPIO_PULLDOWN ?
	        "pull-down" :
	        "floating",
	        proto->config.spi.dev_mode == DEV_MASTER ?
	        "master" : "slave");

	print_freq(con, speeds[proto->config.spi.dev_speed]);
	cprintf(con, " (");
	for (i = 0, cnt = 0; i < SPEED_NB; i++) {
		if (proto->config.spi.dev_speed == i)
			continue;
		if (cnt++)
			cprintf(con, ", ");
		print_freq(con, speeds[i]);
	}
	cprintf(con, ")\r\n");
	cprintf(con, "Polarity: %d\r\nPhase: %d\r\nBit order: %s first\r\n",
	        proto->config.spi.dev_polarity,
	        proto->config.spi.dev_phase,
	        proto->config.spi.dev_bit_lsb_msb == DEV_FIRSTBIT_MSB ?
	        "MSB" : "LSB");
}

static ReturnCode hydranfc_v2_init_RFAL(t_hydra_console *con)
{
	ReturnCode err;
	/* RFAL initalisation */
	rfalAnalogConfigInitialize();
	err = rfalInitialize();
	if (err != ERR_NONE) {
		cprintf(con,
		        "hydranfc_v2_init_RFAL rfalInitialize() error=%d\r\n",
		        err);
		return err;
	}

	/* DPO setup */
#ifdef DPO_ENABLE
	rfalDpoInitialize();
	rfalDpoSetMeasureCallback( rfalChipMeasureAmplitude );
	err = rfalDpoTableWrite(dpoSetup,sizeof(dpoSetup)/sizeof(rfalDpoEntry));
	if(err != ERR_NONE) {
		cprintf(con, "hydranfc_v2_init_RFAL rfalDpoTableWrite() error=%d\r\n", err);
		return err;
	}
	rfalDpoSetEnabled(true);
	rfalSetPreTxRxCallback(&rfalPreTransceiveCb);
#endif
	return err;
}

extern t_mode_config mode_con1;

static bool init_gpio_spi_nfc(t_hydra_console *con)
{
	/*
	 * Initializes the SPI driver 2. The SPI2 signals are routed as follow:
	 * ST25R3916 IO4_CS SPI mode / HydraBus PC1 - NSS
	 * ST25R3916 DATA_CLK SPI mode / HydraBus PB10 - SCK
	 * ST25R3916 IO6_MISO SPI mode / HydraBus PC2 - MISO
	 * ST25R3916 IO7_MOSI SPI mode / HydraBus PC3 - MOSI
	 * Used for communication with ST25R3916 in SPI mode with NSS.
	 */
	con->mode->proto.config.spi.dev_gpio_pull = MODE_CONFIG_DEV_GPIO_NOPULL;
	con->mode->proto.config.spi.dev_speed = 6; /* 10 500 000 Hz */
	con->mode->proto.config.spi.dev_phase = 1;
	con->mode->proto.config.spi.dev_polarity = 0;
	con->mode->proto.config.spi.dev_bit_lsb_msb = DEV_FIRSTBIT_MSB;
	con->mode->proto.config.spi.dev_mode = DEV_MASTER;
	bsp_spi_init(BSP_DEV_SPI2, &con->mode->proto);

	/*
	 * Initializes the SPI driver 1. The SPI1 signals are routed as follows:
	 * Shall be configured as SPI Slave for ST25R3916 NFC data sampling on MOD pin.
	 * NSS. (Not used use Software).
	 * ST25R3916 MCU_CLK pin28 output / HydraBus PA5 - SCK.(AF5) => SPI Slave CLK input (Sniffer mode/RX Transparent Mode)
	 * ST25R3916 ST25R3916 MOSI SPI pin31 (IN) / HydraBus PA6 - MISO.(AF5) (MCU TX Transparent Mode)
	 * ST25R3916 ST25R3916 MISO_SDA pin32 (OUT) / HydraBus PA7 - MOSI.(AF5) => SPI Slave MOSI input (Sniffer mode/RX Transparent Mode)
	 */
	/* spiStart() is done in sniffer see sniffer.c */
	/* HydraBus SPI1 Slave CLK input */
	bsp_gpio_init(BSP_GPIO_PORTA, 5, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL);
	/* HydraBus SPI1 Slave MISO. Not used/Not connected */
	bsp_gpio_init(BSP_GPIO_PORTA, 6, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL);
	/* HydraBus SPI1 Slave MOSI. connected to ST25R3916 MOD Pin */
	bsp_gpio_init(BSP_GPIO_PORTA, 7, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL);

	/* Configure K1/2 Buttons as Input */
	bsp_gpio_init(BSP_GPIO_PORTB, 8, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL); /* K1 Button */
	bsp_gpio_init(BSP_GPIO_PORTB, 9, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL); /* K2 Button */

	/* Configure D1/2/3/4 LEDs as Output */
	D1_OFF;
	D2_OFF;
	D3_OFF;
	D4_OFF;

	/* LED_D1 or TST_PÏN */
	bsp_gpio_init(BSP_GPIO_PORTB, 0, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL,
	              MODE_CONFIG_DEV_GPIO_NOPULL);

#ifndef MAKE_DEBUG
	// can't use LED on PB3 if using SWO
	bsp_gpio_init(BSP_GPIO_PORTB, 3, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL,
	              MODE_CONFIG_DEV_GPIO_NOPULL);
#endif

	bsp_gpio_init(BSP_GPIO_PORTB, 4, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL,
	              MODE_CONFIG_DEV_GPIO_NOPULL);
	bsp_gpio_init(BSP_GPIO_PORTB, 5, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL,
	              MODE_CONFIG_DEV_GPIO_NOPULL);

	palDisablePadEvent(GPIOA, 1);
	/* ST25R3916 IRQ output / HydraBus PA1 input */
	palClearPad(GPIOA, 1);
	palSetPadMode(GPIOA, 1, PAL_MODE_INPUT | PAL_STM32_OSPEED_MID1);
	/* Activates the PAL driver callback */
	palEnablePadEvent(GPIOA, 1, PAL_EVENT_MODE_RISING_EDGE);
	palSetPadCallback(GPIOA, 1, &extcb1, NULL);

	/* Init st25r3916 IRQ function callback */
	st25r3916_irq_fn = st25r3916Isr;
	hal_st25r3916_spiInit(ST25R391X_SPI_DEVICE);
	if (hydranfc_v2_init_RFAL(con) != ERR_NONE) {
		cprintf(con, "HydraNFC v2 not found.\r\n");
		return FALSE;
	}
	return TRUE;
}

static void deinit_gpio_spi_nfc(t_hydra_console *con)
{
	(void)(con);
	palClearPad(GPIOA, 1);
	palSetPadMode(GPIOA, 1, PAL_MODE_INPUT);
	palDisablePadEvent(GPIOA, 1);

	bsp_spi_deinit(BSP_DEV_SPI2);

	bsp_gpio_init(BSP_GPIO_PORTA, 5, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL);
	bsp_gpio_init(BSP_GPIO_PORTA, 6, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL);
	bsp_gpio_init(BSP_GPIO_PORTA, 7, MODE_CONFIG_DEV_GPIO_IN,
	              MODE_CONFIG_DEV_GPIO_NOPULL);

	st25r3916_irq_fn = NULL;
}

static int init(t_hydra_console *con, t_tokenline_parsed *p)
{
	int tokens_used = 0;

	if (init_gpio_spi_nfc(con) == FALSE) {
		deinit_gpio_spi_nfc(con);
		return tokens_used;
	}
	cprintf(con, "HydraNFC Shield v2 initialized with success.\r\n");

	/* Process cmdline arguments, skipping "dnfc". */
	if (p != NULL) {
		if (con != NULL) {
			tokens_used = 1 + exec(con, p, 1);
		}
	}

	show_params(con);

	return tokens_used;
}

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos)
{
	mode_config_proto_t *proto = &con->mode->proto;
	float arg_float;
	int t, i;
	bsp_status_t bsp_status;
	int action;

	action = 0;
	for (t = token_pos; p->tokens[t]; t++) {
		switch (p->tokens[t]) {
		case T_SHOW:
			t += show(con, p);
			break;

		case T_FREQUENCY:
			t += 2;
			memcpy(&arg_float, p->buf + p->tokens[t],
			       sizeof(float));
			for (i = 0; i < SPEED_NB; i++) {
				if (arg_float == speeds[i]) {
					proto->config.spi.dev_speed = i;
					break;
				}
			}
			if (i == 8) {
				cprintf(con, "Invalid frequency.\r\n");
				return t;
			}
			bsp_status = bsp_spi_init(BSP_DEV_SPI2, proto);
			if (bsp_status != BSP_OK) {
				cprintf(con, str_bsp_init_err, bsp_status);
				return t;
			}
			break;

		case T_NFC_MODE:
			t += 2;
			memcpy(&nfc_mode, p->buf + p->tokens[t], sizeof(int));
			break;

		case T_NFC_MODE_TX_BITRATE:
			t += 2;
			memcpy(&nfc_mode_tx_br, p->buf + p->tokens[t],
			       sizeof(int));
			break;

		case T_NFC_MODE_RX_BITRATE:
			t += 2;
			memcpy(&nfc_mode_rx_br, p->buf + p->tokens[t],
			       sizeof(int));
			break;

		case T_NFC_OBSV: {
			int tmp_obsv_val = 0;
			t += 2;
			memcpy(&tmp_obsv_val, p->buf + p->tokens[t],
			       sizeof(int));
			if ((tmp_obsv_val < 0) || (tmp_obsv_val > 1)) {
				cprintf(con,
				        "Invalid nfc-obsv value shall be 0(OFF) or 1(ON)\r\n");
				return t;
			}
			nfc_obsv = tmp_obsv_val;
			break;
		}

		case T_NFC_OBSV_TX: {
			int tmp_obsv_val = 0;
			t += 2;
			memcpy(&tmp_obsv_val, p->buf + p->tokens[t],
			       sizeof(int));
			if ((tmp_obsv_val < 0) || (tmp_obsv_val > 255)) {
				cprintf(con,
				        "Invalid nfc-obsv-tx value (shall be between 0 and 255)\r\n");
				return t;
			}
			nfc_obsv_tx = tmp_obsv_val;
			break;
		}

		case T_NFC_OBSV_RX: {
			int tmp_obsv_val = 0;
			t += 2;
			memcpy(&tmp_obsv_val, p->buf + p->tokens[t],
			       sizeof(int));
			if ((tmp_obsv_val < 0) || (tmp_obsv_val > 255)) {
				cprintf(con,
				        "Invalid nfc-obsv-rx value (shall be between 0 and 255)\r\n");
				return t;
			}
			nfc_obsv_rx = tmp_obsv_val;
			break;
		}

		case T_SET_NFC_MODE:
		case T_GET_NFC_MODE:
		case T_SET_NFC_OBSV:
		case T_GET_NFC_OBSV:
		case T_NFC_TRANSPARENT:
		case T_NFC_STREAM:
		case T_SNIFF:
		case T_NFC_RF_OFF_ON:
		case T_NFC_ISO_14443_REQA:
		case T_NFC_ISO_14443_WUPA: {
			action = p->tokens[t];
			break;
		}

		case T_NFC_SEND_BYTES:
		case T_NFC_SEND_BYTES_AND_COMPUTE_CRC: {
			action = p->tokens[t];
			t += 2;
			break;
		}

		default:
			return t - token_pos;
		}
	}

	switch (action) {
	case T_SET_NFC_MODE: {
		ReturnCode err;
		cprintf(con, "nfc-mode = %d\r\n", nfc_mode);
		cprintf(con, "nfc-mode-tx_br = %d\r\n", nfc_mode_tx_br);
		cprintf(con, "nfc-mode-rx_br = %d\r\n", nfc_mode_rx_br);
		err = rfalSetMode(nfc_mode, nfc_mode_tx_br, nfc_mode_rx_br);
		if (err == ERR_NONE) {
			cprintf(con, "rfalSetMode OK\r\n");
		} else {
			cprintf(con, "rfalSetMode Error %d\r\n", err);
		}
	}
	break;

	case T_GET_NFC_MODE: {
		ReturnCode err;
		rfalMode mode;
		rfalBitRate txBR, rxBR;

		mode = rfalGetMode();
		cprintf(con, "nfc-mode = %d\r\n", mode);

		err = rfalGetBitRate(&txBR, &rxBR);
		if (err == ERR_NONE) {
			cprintf(con, "nfc-mode-tx_br = %d\r\n", txBR);
			cprintf(con, "nfc-mode-rx_br = %d\r\n", rxBR);
		} else {
			cprintf(con, "rfalGetBitRate Error %d\r\n", err);
		}
	}
	break;

	case T_SET_NFC_OBSV: {
		cprintf(con, "nfc-obsv = %d\r\n", nfc_obsv);
		cprintf(con, "nfc-obsv-tx = %d / 0x%02X\r\n", nfc_obsv_tx,
		        nfc_obsv_tx);
		cprintf(con, "nfc-obsv-rx = %d / 0x%02X\r\n", nfc_obsv_rx,
		        nfc_obsv_rx);
		if (nfc_obsv == 0) {
			rfalDisableObsvMode();
			cprintf(con, "NFC Observation mode disabled\r\n");
		}

		if (nfc_obsv == 1) {
			rfalSetObsvMode(nfc_obsv_tx, nfc_obsv_rx);
			cprintf(con, "NFC Observation mode enabled\r\n");
		}
	}
	break;

	case T_GET_NFC_OBSV: {
		uint8_t rfal_obsv_tx;
		uint8_t rfal_obsv_rx;

		cprintf(con, "nfc-obsv = %d\r\n", nfc_obsv);
		cprintf(con, "nfc-obsv-tx = %d / 0x%02X\r\n", nfc_obsv_tx,
		        nfc_obsv_tx);
		cprintf(con, "nfc-obsv-rx = %d / 0x%02X\r\n", nfc_obsv_rx,
		        nfc_obsv_rx);

		rfalGetObsvMode(&rfal_obsv_tx, &rfal_obsv_rx);
		cprintf(con, "rfal_obsv_tx = %d / 0x%02X\r\n", rfal_obsv_tx,
		        rfal_obsv_tx);
		cprintf(con, "rfal_obsv_rx = %d / 0x%02X\r\n", rfal_obsv_rx,
		        rfal_obsv_rx);
	}
	break;

	case T_NFC_TRANSPARENT: {
		ReturnCode err;
		cprintf(con, "ST25R3916_CMD_TRANSPARENT_MODE executed\r\n");

		err = rfalSetMode(RFAL_MODE_LISTEN_NFCA, RFAL_BR_106,
		                  RFAL_BR_106);
		if (err != ERR_NONE) {
			cprintf(con, "rfalSetMode Error %d\r\n", err);
		}
		st25r3916SetRegisterBits(ST25R3916_REG_OP_CONTROL,
		                         ST25R3916_REG_OP_CONTROL_rx_en
		                         | ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);
		st25r3916WriteRegister(ST25R3916_REG_AUX,
		                       ST25R3916_REG_AUX_dis_corr);
		st25r3916WriteRegister(ST25R3916_REG_RX_CONF2, 0x48); // Receiver configuration register 2
		st25r3916WriteRegister(ST25R3916_REG_RX_CONF3, 0xFC); // Receiver configuration register 3 => Boost +5.5 dB AM/PM
		st25r3916WriteRegister(ST25R3916_REG_IO_CONF1, 0x00); // IO configuration register 1 => Set MCU_CLK/PA5 to 3.39MHz
		st25r3916WriteTestRegister(0x01, 0x46); // Pin CSO => Tag demodulator ASK digital out
		//st25r3916WriteTestRegister(0x01, 0x06); // Pin CSO => Tag demodulator ASK digital out MCU_CLK*2

		st25r3916_irq_fn = NULL; /* Disable IRQ */
		// st25r3916DisableInterrupts(ST25R3916_IRQ_MASK_ALL);
		st25r3916ExecuteCommand(ST25R3916_CMD_TRANSPARENT_MODE);

		/* Reconfigure SPI2 PIN as Input PullUp for Transparent mode test */
		/*
		 * ST25R3916 IO4_CS SPI mode / HydraBus PC1 - NSS (Do not change it to keep Transparent mode when CS is High)
		 * ST25R3916 DATA_CLK SPI mode / HydraBus PB10 - SCK
		 * ST25R3916 IO6_MISO SPI mode / HydraBus PC2 - MISO
		 * ST25R3916 IO7_MOSI SPI mode / HydraBus PC3 - MOSI
		 */
		bsp_gpio_init(BSP_GPIO_PORTB, 10, MODE_CONFIG_DEV_GPIO_IN,
		              MODE_CONFIG_DEV_GPIO_PULLUP); /* SCK must be set to Input with Pull Up for Transparent mode */

		// MISO => RX decoding In Transparent mode the framing and FIFO are bypassed. The digitized subcarrier signal directly drives the MISO pin.
		bsp_gpio_init(BSP_GPIO_PORTC, 2, MODE_CONFIG_DEV_GPIO_IN,
		              MODE_CONFIG_DEV_GPIO_PULLUP); /* MISO must be set to Input with Pull Up for Transparent mode */

		// MOSI => TX encoding In Transparent mode, the framing and FIFO are bypassed, and the MOSI pin directly drives the modulation of the transmitter
		bsp_gpio_init(BSP_GPIO_PORTC, 3,
		              MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL,
		              MODE_CONFIG_DEV_GPIO_NOPULL);

		/* Reconfigure SPI1 PIN as Input for Transparent mode test */
		bsp_gpio_init(BSP_GPIO_PORTA, 5, MODE_CONFIG_DEV_GPIO_IN,
		              MODE_CONFIG_DEV_GPIO_NOPULL);
		bsp_gpio_init(BSP_GPIO_PORTA, 6, MODE_CONFIG_DEV_GPIO_IN,
		              MODE_CONFIG_DEV_GPIO_NOPULL);
		bsp_gpio_init(BSP_GPIO_PORTA, 7, MODE_CONFIG_DEV_GPIO_IN,
		              MODE_CONFIG_DEV_GPIO_NOPULL);

		cprintf(con,
		        "SPI2/ST25R3916 communication is disabled\r\n\tIt shall be restored by console command 'frequency 10.5m'\r\n\tor by exiting 'dnfc' mode and entering again 'dnfc' mode\r\n");
	}
	break;

	case T_NFC_STREAM: {
		//ReturnCode err;
		cprintf(con, "T_NFC_STREAM not implemented\r\n");
		//st25r3916StreamConfigure();
	}
	break;

	case T_SNIFF: {
		ReturnCode err;
		cprintf(con,
		        "T_SNIFF (Test special sniffer ST25R3916 MCU_CLK(PA5) and CSI CSO)\r\n");

		err = rfalSetMode(RFAL_MODE_LISTEN_NFCA, RFAL_BR_106,
		                  RFAL_BR_106);
		if (err != ERR_NONE) {
			cprintf(con, "rfalSetMode Error %d\r\n", err);
		}
		st25r3916SetRegisterBits(ST25R3916_REG_OP_CONTROL,
		                         ST25R3916_REG_OP_CONTROL_rx_en
		                         | ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);
		st25r3916WriteRegister(ST25R3916_REG_PASSIVE_TARGET,
		                       (ST25R3916_REG_PASSIVE_TARGET_d_106_ac_a |
		                        ST25R3916_REG_PASSIVE_TARGET_rfu |
		                        ST25R3916_REG_PASSIVE_TARGET_d_212_424_1r |
		                        ST25R3916_REG_PASSIVE_TARGET_d_ac_ap2p)); /* Disable Automatic Response in Passive Target Mode */
		st25r3916WriteRegister(ST25R3916_REG_AUX,
		                       ST25R3916_REG_AUX_dis_corr);
		st25r3916WriteRegister(ST25R3916_REG_RX_CONF2, 0x48); // Receiver configuration register 2
		st25r3916WriteRegister(ST25R3916_REG_RX_CONF3, 0xFC); // Receiver configuration register 3 => Boost +5.5 dB AM/PM
		st25r3916WriteRegister(ST25R3916_REG_IO_CONF1, 0x00); // IO configuration register 1 => Set MCU_CLK/PA5 to 3.39MHz
		st25r3916WriteTestRegister(0x01, 0x46); // Pin CSO => Tag demodulator ASK digital out
		//st25r3916WriteTestRegister(0x01, 0x06); // Pin CSO => Tag demodulator ASK digital out MCU_CLK*2
	}
	break;

	case T_NFC_RF_OFF_ON: {
		rfalBitRate txBR, rxBR;
		rfalMode mode = rfalGetMode();

		if (mode == RFAL_MODE_POLL_NFCA) {
			rfalSetGT( RFAL_GT_NFCA);
			rfalSetFDTListen( RFAL_FDT_LISTEN_NFCA_POLLER);
			rfalSetFDTPoll( RFAL_FDT_POLL_NFCA_POLLER);
		} else if (mode == RFAL_MODE_POLL_NFCB) {
			rfalSetGT( RFAL_GT_NFCB);
			rfalSetFDTListen( RFAL_FDT_LISTEN_NFCB_POLLER);
			rfalSetFDTPoll( RFAL_FDT_POLL_NFCB_POLLER);
		} else if (mode == RFAL_MODE_POLL_NFCV) {
			rfalSetGT( RFAL_GT_NFCV);
			rfalSetFDTListen( RFAL_FDT_LISTEN_NFCV_POLLER);
			rfalSetFDTPoll( RFAL_FDT_POLL_NFCV_POLLER);
		} else {
			cprintf(con,
			        "Error. Only RFAL_MODE_POLL_NFCA (1), RFAL_MODE_POLL_NFCB (3), RFAL_MODE_POLL_NFCV (7) supported.\r\n");
			cprintf(con,
			        "Update parameters with set-nfc-mode nfc-mode XXX command.\r\n");
			break;
		}

		rfalGetBitRate(&txBR, &rxBR);

		if (mode != RFAL_MODE_POLL_NFCV) {
			if ((txBR != RFAL_BR_106) & (rxBR != RFAL_BR_106)) {
				cprintf(con,
				        "Error. Only RFAL_BR_106 (0) for Rx & Tx (0) supported.\r\n");
				cprintf(con,
				        "Update parameters with set-nfc-mode nfc-mode-tx_br XXX or set-nfc-mode nfc-mode-rx_br XXX commands.\r\n");
				break;
			}
		}

		rfalSetErrorHandling(RFAL_ERRORHANDLING_EMD);

		rfalFieldOff();
		rfalFieldOnAndStartGT();
		break;
	}

	case T_NFC_ISO_14443_REQA:
	case T_NFC_ISO_14443_WUPA: {
		ReturnCode err;
		if (action == T_NFC_ISO_14443_REQA) {
			err = rfalNfcaPollerCheckPresence(
			          RFAL_14443A_SHORTFRAME_CMD_REQA,
			          (rfalNfcaSensRes*)g_sbuf);
		} else {
			err = rfalNfcaPollerCheckPresence(
			          RFAL_14443A_SHORTFRAME_CMD_WUPA,
			          (rfalNfcaSensRes*)g_sbuf);
		}

		if (err != ERR_NONE) {
			cprintf(con, "Error %d\r\n", err);
		} else {
			cprintf(con, "%02X %02X\r\n", g_sbuf[0], g_sbuf[1]);
		}
		break;
	}

	case T_NFC_SEND_BYTES:
	case T_NFC_SEND_BYTES_AND_COMPUTE_CRC: {
		uint32_t len;
		uint16_t rlen;
		ReturnCode err;
		uint32_t flags = ((uint32_t)RFAL_TXRX_FLAGS_CRC_TX_MANUAL
		                  | (uint32_t)RFAL_TXRX_FLAGS_CRC_RX_KEEP);
		if (action == T_NFC_SEND_BYTES_AND_COMPUTE_CRC) {
			flags = RFAL_TXRX_FLAGS_DEFAULT;
		}
		buf_ascii2hex((uint8_t*)p->buf, g_sbuf, &len);

		err = rfalTransceiveBlockingTxRx(g_sbuf,
		                                 len,
		                                 g_sbuf,
		                                 0xFF,
		                                 &rlen,
		                                 flags,
		                                 216960U + 71680U);

		// TODO. Understand why in ISO14443-A, 9320 sends the correct answer, but err is set to 21 (ERR_CRC)
		if ((err != ERR_NONE) & (err != ERR_CRC)) {
			cprintf(con, "Error %d\r\n", err);
		} else {
			pretty_print_hex_buf(con, g_sbuf, rlen);
		}

		break;
	}
	}

	return t - token_pos;
}

static void start(t_hydra_console *con)
{
	bsp_spi_select(BSP_DEV_SPI2);
	cprintf(con, hydrabus_mode_str_cs_enabled);
}

static void stop(t_hydra_console *con)
{
	bsp_spi_unselect(BSP_DEV_SPI2);
	cprintf(con, hydrabus_mode_str_cs_disabled);
}

static uint32_t write(t_hydra_console *con, uint8_t *tx_data, uint8_t nb_data)
{
	int i;
	uint32_t status;

	status = bsp_spi_write_u8(BSP_DEV_SPI2, tx_data, nb_data);
	if (status == BSP_OK) {
		if (nb_data == 1) {
			/* Write 1 data */
			cprintf(con, hydrabus_mode_str_write_one_u8,
			        tx_data[0]);
		} else if (nb_data > 1) {
			/* Write n data */
			cprintf(con, hydrabus_mode_str_mul_write);
			for (i = 0; i < nb_data; i++) {
				cprintf(con, hydrabus_mode_str_mul_value_u8,
				        tx_data[i]);
			}
			cprintf(con, hydrabus_mode_str_mul_br);
		}
	}
	return status;
}

static uint32_t read(t_hydra_console *con, uint8_t *rx_data, uint8_t nb_data)
{
	int i;
	uint32_t status;

	status = bsp_spi_read_u8(BSP_DEV_SPI2, rx_data, nb_data);
	if (status == BSP_OK) {
		if (nb_data == 1) {
			/* Read 1 data */
			cprintf(con, hydrabus_mode_str_read_one_u8, rx_data[0]);
		} else if (nb_data > 1) {
			/* Read n data */
			cprintf(con, hydrabus_mode_str_mul_read);
			for (i = 0; i < nb_data; i++) {
				cprintf(con, hydrabus_mode_str_mul_value_u8,
				        rx_data[i]);
			}
			cprintf(con, hydrabus_mode_str_mul_br);
		}
	}
	return status;
}

static uint32_t dump(t_hydra_console *con, uint8_t *rx_data, uint8_t nb_data)
{
	(void)con;
	uint32_t status;

	status = bsp_spi_read_u8(BSP_DEV_SPI2, rx_data, nb_data);
	return status;
}

static uint32_t write_read(t_hydra_console *con, uint8_t *tx_data,
                           uint8_t *rx_data, uint8_t nb_data)
{
	int i;
	uint32_t status;

	status = bsp_spi_write_read_u8(BSP_DEV_SPI2, tx_data, rx_data, nb_data);
	if (status == BSP_OK) {
		if (nb_data == 1) {
			/* Write & Read 1 data */
			cprintf(con, hydrabus_mode_str_write_read_u8,
			        tx_data[0], rx_data[0]);
		} else if (nb_data > 1) {
			/* Write & Read n data */
			for (i = 0; i < nb_data; i++) {
				cprintf(con, hydrabus_mode_str_write_read_u8,
				        tx_data[i], rx_data[i]);
			}
		}
	}
	return status;
}

static void cleanup(t_hydra_console *con)
{
	deinit_gpio_spi_nfc(con);
}

static void show_registers(t_hydra_console *con)
{
	ReturnCode err;
	unsigned int i;
	t_st25r3916Regs regDump;

	err = st25r3916GetRegsDump(&regDump);
	if (err != ERR_NONE) {
		cprintf(con, "st25r3916GetRegsDump() error=%d\r\n", err);
	}

	/* ST25R3916 space A */
	cprintf(con, "ST25R3916 Registers space A:\r\n");
	for (i = 0; i < ST25R3916_REG_IC_IDENTITY + 1; i++) {
		cprintf(con, "0x%02x\t: 0x%02x\r\n", i, regDump.RsA[i]);
	}
	cprintf(con, "\r\n");
	/* ST25R3916 space B */
	cprintf(con, "ST25R3916 Registers space B:\r\n");
	for (i = 0; i < ST25R3916_SPACE_B_REG_LEN; i++) {
		cprintf(con, "0x%02x\t: 0x%02x\r\n", i, regDump.RsB[i]);
	}
}

static int show(t_hydra_console *con, t_tokenline_parsed *p)
{
	int tokens_used;

	tokens_used = 0;
	if (p->tokens[1] == T_PINS) {
		tokens_used++;
		cprint(con, str_pins_spi2, strlen(str_pins_spi2));
	} else if (p->tokens[1] == T_REGISTERS) {
		tokens_used++;
		show_registers(con);
	} else {
		show_params(con);
	}
	return tokens_used;
}

static const char* get_prompt(t_hydra_console *con)
{
	(void)con;
	return str_prompt_dnfc2;
}

void pc3_high(t_hydra_console *con)
{
	(void)con;
	bsp_gpio_set(BSP_GPIO_PORTC, 3);
}

void pc3_low(t_hydra_console *con)
{
	(void)con;
	bsp_gpio_clr(BSP_GPIO_PORTC, 3);
}

const mode_exec_t mode_dnfc_exec = {
	.init = &init,
	.exec = &exec,
	.start = &start,
	.stop = &stop,
	.write = &write,
	.read = &read,
	.dump = &dump,
	.write_read = &write_read,
	.dath = &pc3_high,
	.datl = &pc3_low,
	.cleanup = &cleanup,
	.get_prompt = &get_prompt,
};
