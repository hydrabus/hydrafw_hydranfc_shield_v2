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

#include "hydrabus_mode_dnfc.h"
#include "hydranfc_v2.h"
#include "bsp_spi.h"
#include "common.h"
#include <string.h>

#include "platform.h"
#include "logger.h"
#include "usart.h"
#include "spi.h"
#include "led.h"

#include "usbd_custom_hid_if.h"
//#include "ce.h"

#include "stream_dispatcher.h"
#include "dispatcher.h"
#include "rfal_analogConfig.h"
#include "rfal_rf.h"
#include "rfal_dpo.h"
#include "rfal_chip.h"
#include "st25r3916.h"
#include "st25r3916_irq.h"
#include "st25r3916_com.h"

#include "rfal_poller.h"

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos);
static int show(t_hydra_console *con, t_tokenline_parsed *p);

static const char* str_pins_spi2= {
	"CS:   PC1 (SW)\r\nSCK:  PB10\r\nMISO: PC2\r\nMOSI: PC3\r\nIRQ: PA1\r\n"
};
static const char* str_prompt_dnfc2= { "dnfc2" PROMPT };

static const char* str_bsp_init_err= { "bsp_spi_init() error %d\r\n" };

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

static void (* st25r3916_irq_fn)(void) = NULL;

/* Triggered when the Ext IRQ is pressed or released. */
static void extcb1(void * arg)
{
	(void) arg;

	if(st25r3916_irq_fn != NULL)
		st25r3916_irq_fn();

	irq_count++;
	irq = 1;
}

static void show_params(t_hydra_console *con)
{
	uint8_t i, cnt;
	mode_config_proto_t* proto = &con->mode->proto;

	cprintf(con, "Device: SPI2\r\nGPIO resistor: %s\r\nMode: %s\r\n"
		"Frequency: ",
		proto->config.spi.dev_gpio_pull == MODE_CONFIG_DEV_GPIO_PULLUP ? "pull-up" :
		proto->config.spi.dev_gpio_pull == MODE_CONFIG_DEV_GPIO_PULLDOWN ? "pull-down" :
		"floating",
		proto->config.spi.dev_mode == DEV_MASTER ? "master" : "slave");

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
		proto->config.spi.dev_bit_lsb_msb == DEV_FIRSTBIT_MSB ? "MSB" : "LSB");
}

static ReturnCode hydranfc_v2_init_RFAL(t_hydra_console *con)
{
	ReturnCode err;
	/* RFAL initalisation */
	rfalAnalogConfigInitialize();
	err = rfalInitialize();
	if(err != ERR_NONE)
	{
		cprintf(con, "hydranfc_v2_init_RFAL rfalInitialize() error=%d\r\n", err);
		return err;
	}

	/* DPO setup */
#ifdef DPO_ENABLE
	rfalDpoInitialize();
	rfalDpoSetMeasureCallback( rfalChipMeasureAmplitude );
	err = rfalDpoTableWrite(dpoSetup,sizeof(dpoSetup)/sizeof(rfalDpoEntry));
	if(err != ERR_NONE)
	{
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
	palSetPadMode(GPIOA, 5, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_MID1);
	/* HydraBus SPI1 Slave MISO. Not used/Not connected */
	palSetPadMode(GPIOA, 6, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_MID1);
	/* HydraBus SPI1 Slave MOSI. connected to ST25R3916 MOD Pin */
	palSetPadMode(GPIOA, 7, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_MID1);

	/* Configure K1/2 Buttons as Input */
	palSetPadMode(GPIOB, 8, PAL_MODE_INPUT); /* K1 Button */
	palSetPadMode(GPIOB, 9, PAL_MODE_INPUT); /* K2 Button */

	/* Configure D1/2/3/4 LEDs as Output */
	D1_OFF;
	D2_OFF;
	D3_OFF;
	D4_OFF;

	palSetPadMode(GPIOB, 0, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_MID1);

#ifndef MAKE_DEBUG
	// can't use LED on PB3 if using SWO
	palSetPadMode(GPIOB, 3, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_MID1);
#endif

	palSetPadMode(GPIOB, 4, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_MID1);
	palSetPadMode(GPIOB, 5, PAL_MODE_OUTPUT_PUSHPULL | PAL_STM32_OSPEED_MID1);

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
	if (hydranfc_v2_init_RFAL(con) != ERR_NONE)
	{
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

	palSetPadMode(GPIOA, 5, PAL_MODE_INPUT);
	palSetPadMode(GPIOA, 6, PAL_MODE_INPUT);
	palSetPadMode(GPIOA, 7, PAL_MODE_INPUT);

#if 0
	/* Configure K1/2 Buttons as Input */
	palSetPadMode(GPIOB, 8, PAL_MODE_INPUT); /* K1 Button */
	palSetPadMode(GPIOB, 9, PAL_MODE_INPUT); /* K2 Button */

	/* Configure D1/2/3/4 LEDs as Input */
	palSetPadMode(GPIOB, 0, PAL_MODE_INPUT);
	palSetPadMode(GPIOB, 3, PAL_MODE_INPUT);
	palSetPadMode(GPIOB, 4, PAL_MODE_INPUT);
	palSetPadMode(GPIOB, 5, PAL_MODE_INPUT);
#endif
	st25r3916_irq_fn = NULL;
}

static int init(t_hydra_console *con, t_tokenline_parsed *p)
{
	int tokens_used = 0;
/*
	if(con != NULL)
	{
		proto = &con->mode->proto;
		proto->config.hydranfc.nfc_technology = NFC_ALL;
	}
*/
	if(init_gpio_spi_nfc(con) ==  FALSE) {
		deinit_gpio_spi_nfc(con);
		return tokens_used;
	}
	cprintf(con, "HydraNFC Shield v2 initialized with success.\r\n");

	/* Process cmdline arguments, skipping "dnfc". */
	if(p != NULL) {
		if(con != NULL)
		{
			tokens_used = 1 + exec(con, p, 1);
		}
	}

	show_params(con);

	return tokens_used;
}

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos)
{
	mode_config_proto_t* proto = &con->mode->proto;
	float arg_float;
	int t, i;
	bsp_status_t bsp_status;
	int action;
	int nfc_mode,nfc_mode_tx_bitrate, nfc_mode_rx_bitrate;

	action = nfc_mode =  nfc_mode_tx_bitrate = nfc_mode_rx_bitrate = 0;
	for (t = token_pos; p->tokens[t]; t++) {
		switch (p->tokens[t]) {
		case T_SHOW:
			t += show(con, p);
			break;

		case T_FREQUENCY:
			t += 2;
			memcpy(&arg_float, p->buf + p->tokens[t], sizeof(float));
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
			if( bsp_status != BSP_OK) {
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
			memcpy(&nfc_mode_tx_bitrate, p->buf + p->tokens[t], sizeof(int));
			break;

		case T_NFC_MODE_RX_BITRATE:
			t += 2;
			memcpy(&nfc_mode_rx_bitrate, p->buf + p->tokens[t], sizeof(int));
			break;

		case T_SET_NFC_MODE:
		case T_GET_NFC_MODE:
		case T_NFC_TRANSPARENT:
		case T_NFC_STREAM:
			action = p->tokens[t];
			break;

		default:
			return t - token_pos;
		}
	}

	switch(action) {
		case T_SET_NFC_MODE:
			{
				ReturnCode err;
				cprintf(con, "T_NFC_MODE = %d\r\n", nfc_mode);
				cprintf(con, "T_NFC_MODE_TX_BITRATE = %d\r\n", nfc_mode_tx_bitrate);
				cprintf(con, "T_NFC_MODE_RX_BITRATE = %d\r\n", nfc_mode_rx_bitrate);
				err = rfalSetMode(nfc_mode, nfc_mode_tx_bitrate, nfc_mode_rx_bitrate);
				if(err == ERR_NONE)	{
					cprintf(con, "rfalSetMode OK\r\n");
				} else {
					cprintf(con, "rfalSetMode Error %d\r\n", err);
				}
			}
			break;

		case T_GET_NFC_MODE:
			{
				rfalMode mode;
				mode = rfalGetMode();
				cprintf(con, "nfc-mode = %d\r\n", mode);
			}
			break;

		case T_NFC_TRANSPARENT:
			cprintf(con, "ST25R3916_CMD_TRANSPARENT_MODE executed\r\n");
			st25r3916_irq_fn = NULL; /* Disable IRQ */
			// st25r3916DisableInterrupts(ST25R3916_IRQ_MASK_ALL);
			st25r3916ExecuteCommand(ST25R3916_CMD_TRANSPARENT_MODE);
			break;

		case T_NFC_STREAM:
			cprintf(con, "T_NFC_STREAM not implemented\r\n");
			break;
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
			cprintf(con, hydrabus_mode_str_write_one_u8, tx_data[0]);
		} else if (nb_data > 1) {
			/* Write n data */
			cprintf(con, hydrabus_mode_str_mul_write);
			for(i = 0; i < nb_data; i++) {
				cprintf(con, hydrabus_mode_str_mul_value_u8, tx_data[i]);
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
			for(i = 0; i < nb_data; i++) {
				cprintf(con, hydrabus_mode_str_mul_value_u8, rx_data[i]);
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

static uint32_t write_read(t_hydra_console *con, uint8_t *tx_data, uint8_t *rx_data, uint8_t nb_data)
{
	int i;
	uint32_t status;

	status = bsp_spi_write_read_u8(BSP_DEV_SPI2, tx_data, rx_data, nb_data);
	if (status == BSP_OK) {
		if (nb_data == 1) {
			/* Write & Read 1 data */
			cprintf(con, hydrabus_mode_str_write_read_u8, tx_data[0], rx_data[0]);
		} else if (nb_data > 1) {
			/* Write & Read n data */
			for(i = 0; i < nb_data; i++) {
				cprintf(con, hydrabus_mode_str_write_read_u8, tx_data[i], rx_data[i]);
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
	if(err != ERR_NONE)
	{
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
		cprintf(con, "0x%02x\t: 0x%02x\r\n", i,	regDump.RsB[i]);
	}
}

static int show(t_hydra_console *con, t_tokenline_parsed *p)
{
	mode_config_proto_t* proto = &con->mode->proto;
	int tokens_used;
	nfc_technology_str_t tag_tech_str;

	tokens_used = 0;
	if (p->tokens[1] == T_PINS) {
		tokens_used++;
		cprint(con, str_pins_spi2, strlen(str_pins_spi2));
	} else if (p->tokens[1] == T_REGISTERS) {
		tokens_used++;
		show_registers(con);
	} else {
		nfc_technology_to_str(proto->config.hydranfc.nfc_technology, &tag_tech_str);
		cprintf(con, "Selected technology: NFC-%s\r\n", tag_tech_str.str);
		show_params(con);
	}
	return tokens_used;
}

static const char *get_prompt(t_hydra_console *con)
{
	(void)con;
	return str_prompt_dnfc2;
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
	.cleanup = &cleanup,
	.get_prompt = &get_prompt,
};
