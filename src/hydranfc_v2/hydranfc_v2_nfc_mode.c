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
#include "ch.h"
#include "common.h"
#include "tokenline.h"
#include "hydrabus_mode.h"
#include "hydranfc_v2_nfc_mode.h"
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
#include "hydranfc_v2_ce.h"
#include "hydranfc_v2_reader.h"

extern void hydranfc_ce_common(t_hydra_console *con, bool quiet);
extern uint32_t user_uid_len;
extern uint8_t user_uid[8];
extern uint8_t user_sak;
extern uint8_t *user_uri;

static int exec(t_hydra_console *con, t_tokenline_parsed *p, int token_pos);
static int show(t_hydra_console *con, t_tokenline_parsed *p);

static thread_t *key_sniff_thread = NULL;
static volatile int irq_count;
volatile int irq;
volatile int irq_end_rx;
uint8_t globalCommProtectCnt;

static int nfc_obsv = 0; /* 0 = OFF, 1=ON */
static int nfc_obsv_tx = 0;
static int nfc_obsv_rx = 0;

static int nfc_aat_a = 0;
static int nfc_aat_b = 0;

/* Do not Enable DPO to have maximum performances/range */
//#define DPO_ENABLE true

#ifdef DPO_ENABLE
static rfalDpoEntry dpoSetup[] = {
	// new antenna board
	{.rfoRes=0, .inc=255, .dec=115},
	{.rfoRes=2, .inc=100, .dec=0x00}
};
#endif

static void (* st25r3916_irq_fn)(void) = NULL;

/*
 * Works only on positive numbers !!
 */
static int round_int(float x)
{
	return (int)(x + 0.5f);
}

/* Triggered when the Ext IRQ is pressed or released. */
static void extcb1(void * arg)
{
	(void) arg;

	if(st25r3916_irq_fn != NULL)
		st25r3916_irq_fn();

	irq_count++;
	irq = 1;
}

void rfalPreTransceiveCb(void)
{
	rfalDpoAdjust();
}

static ReturnCode hydranfc_v2_init_RFAL(t_hydra_console *con)
{
	ReturnCode err;
	/* RFAL initalisation */
	rfalAnalogConfigInitialize();
	err = rfalInitialize();
	if(err != ERR_NONE) {
		cprintf(con, "hydranfc_v2_init_RFAL rfalInitialize() error=%d\r\n", err);
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
	mode_con1.proto.config.spi.dev_gpio_pull = MODE_CONFIG_DEV_GPIO_NOPULL;
	//mode_con1.proto.config.spi.dev_speed = 5; /* 5 250 000 Hz */
	mode_con1.proto.config.spi.dev_speed = 6; /* 10 500 000 Hz */
	mode_con1.proto.config.spi.dev_phase = 1;
	mode_con1.proto.config.spi.dev_polarity = 0;
	mode_con1.proto.config.spi.dev_bit_lsb_msb = DEV_FIRSTBIT_MSB;
	mode_con1.proto.config.spi.dev_mode = DEV_MASTER;
	bsp_spi_init(BSP_DEV_SPI2, &mode_con1.proto);

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
	bsp_gpio_init(BSP_GPIO_PORTA, 5, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL);
	/* HydraBus SPI1 Slave MISO. Not used/Not connected */
	bsp_gpio_init(BSP_GPIO_PORTA, 6, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL);
	/* HydraBus SPI1 Slave MOSI. connected to ST25R3916 MOD Pin */
	bsp_gpio_init(BSP_GPIO_PORTA, 7, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL);

	/* Configure K1/2 Buttons as Input */
	bsp_gpio_init(BSP_GPIO_PORTB, 8, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL); /* K1 Button */
	bsp_gpio_init(BSP_GPIO_PORTB, 9, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL); /* K2 Button */

	/* Configure D1/2/3/4 LEDs as Output */
	D1_OFF;
	D2_OFF;
	D3_OFF;
	D4_OFF;

	/* LED_D1 or TST_PÏN */
	bsp_gpio_init(BSP_GPIO_PORTB, 0, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL, MODE_CONFIG_DEV_GPIO_NOPULL);

#ifndef MAKE_DEBUG
	// can't use LED on PB3 if using SWO
	bsp_gpio_init(BSP_GPIO_PORTB, 3, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL, MODE_CONFIG_DEV_GPIO_NOPULL);
#endif

	bsp_gpio_init(BSP_GPIO_PORTB, 4, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL, MODE_CONFIG_DEV_GPIO_NOPULL);
	bsp_gpio_init(BSP_GPIO_PORTB, 5, MODE_CONFIG_DEV_GPIO_OUT_PUSHPULL, MODE_CONFIG_DEV_GPIO_NOPULL);

	palDisablePadEvent(GPIOA, 1);
	/* ST25R3916 IRQ output / HydraBus PA1 input */
	palClearPad(GPIOA, 1);
	palSetPadMode(GPIOA, 1, PAL_MODE_INPUT | PAL_STM32_OSPEED_MID1);
	/* Activates the PAL driver callback */
	//palDisablePadEvent(GPIOA, 1);
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

	bsp_gpio_init(BSP_GPIO_PORTA, 5, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL);
	bsp_gpio_init(BSP_GPIO_PORTA, 6, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL);
	bsp_gpio_init(BSP_GPIO_PORTA, 7, MODE_CONFIG_DEV_GPIO_IN, MODE_CONFIG_DEV_GPIO_NOPULL);

	st25r3916_irq_fn = NULL;
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
// TODO hydranfc_v2_scan_iso14443A
#define MIFARE_UL_DATA (MIFARE_UL_DATA_MAX/4)
#define MIFARE_CL1_MAX (5)
#define MIFARE_CL2_MAX (5)
void hydranfc_v2_scan_iso14443A(t_hydranfc_v2_scan_iso14443A *data)
{
	uint8_t data_buf[MIFARE_DATA_MAX];
	uint8_t CL1_buf[MIFARE_CL1_MAX];
	uint8_t CL2_buf[MIFARE_CL2_MAX];

	uint8_t CL1_buf_size = 0;
	uint8_t CL2_buf_size = 0;

	uint8_t i;

	/* Clear data elements */
	memset(data, 0, sizeof(t_hydranfc_v2_scan_iso14443A));

	/* End Test delay */
	irq_count = 0;

	/* Test ISO14443-A/Mifare read UID */
	Trf797xInitialSettings();
	Trf797xResetFIFO();

	/*
	 * Write Modulator and SYS_CLK Control Register (0x09) (13.56Mhz SYS_CLK
	 * and default Clock 13.56Mhz))
	 */
	data_buf[0] = MODULATOR_CONTROL;
	data_buf[1] = 0x31;
	Trf797xWriteSingle(data_buf, 2);

	/*
	 * Configure Mode ISO Control Register (0x01) to 0x88 (ISO14443A RX bit
	 * rate, 106 kbps) and no RX CRC (CRC is not present in the response))
	 */
	data_buf[0] = ISO_CONTROL;
	data_buf[1] = 0x88;
	Trf797xWriteSingle(data_buf, 2);

	/*
		data_buf[0] = ISO_CONTROL;
		Trf797xReadSingle(data_buf, 1);
		if (data_buf[0] != 0x88)
			cprintf(con, "Error ISO Control Register read=0x%02lX (should be 0x88)\r\n",
				(uint32_t)data_buf[0]);
	*/

	/* Turn RF ON (Chip Status Control Register (0x00)) */
	Trf797xTurnRfOn();

	/* Send REQA (7 bits) and receive ATQA (2 bytes) */
	data_buf[0] = 0x26; /* REQA (7bits) */
	data->atqa_buf_nb_rx_data = Trf797x_transceive_bits(data_buf[0], 7, data->atqa_buf, MIFARE_ATQA_MAX,
	                            10, /* 10ms TX/RX Timeout */
	                            0); /* TX CRC disabled */
	/* Re-send REQA */
	if (data->atqa_buf_nb_rx_data == 0) {
		/* Send REQA (7 bits) and receive ATQA (2 bytes) */
		data_buf[0] = 0x26; /* REQA (7 bits) */
		data->atqa_buf_nb_rx_data = Trf797x_transceive_bits(data_buf[0], 7, data->atqa_buf, MIFARE_ATQA_MAX,
		                            10, /* 10ms TX/RX Timeout */
		                            0); /* TX CRC disabled */
	}
	if (data->atqa_buf_nb_rx_data > 0) {
		/* Send AntiColl Cascade Level1 (2 bytes) and receive CT+3 UID bytes+BCC (5 bytes) [tag 7 bytes UID]  or UID+BCC (5 bytes) [tag 4 bytes UID] */
		data_buf[0] = 0x93;
		data_buf[1] = 0x20;

		CL1_buf_size = Trf797x_transceive_bytes(data_buf, 2, CL1_buf, MIFARE_CL1_MAX,
		                                        10, /* 10ms TX/RX Timeout */
		                                        0); /* TX CRC disabled */

		/* Check tag 7 bytes UID */
		if (CL1_buf[0] == 0x88) {
			for (i = 0; i < 3; i++) {
				data->uid_buf[data->uid_buf_nb_rx_data] = CL1_buf[1 + i];
				data->uid_buf_nb_rx_data++;
			}

			/* Send AntiColl Cascade Level1 (2 bytes)+CT+3 UID bytes+BCC (5 bytes) and receive SAK1 (1 byte) */
			data_buf[0] = 0x93;
			data_buf[1] = 0x70;

			for (i = 0; i < CL1_buf_size; i++) {
				data_buf[2 + i] = CL1_buf[i];
			}

			data->sak1_buf_nb_rx_data = Trf797x_transceive_bytes(data_buf, (2 + CL1_buf_size), data->sak1_buf, MIFARE_SAK_MAX,
			                            20, /* 10ms TX/RX Timeout */
			                            1); /* TX CRC disabled */
			if(data->sak1_buf_nb_rx_data >= 3)
				data->sak1_buf_nb_rx_data -= 2; /* Remove 2 last bytes (CRC) */

			if (data->sak1_buf_nb_rx_data > 0) {

				/* Send AntiColl Cascade Level2 (2 bytes) and receive 4 UID bytes+BCC (5 bytes)*/
				data_buf[0] = 0x95;
				data_buf[1] = 0x20;

				CL2_buf_size = Trf797x_transceive_bytes(data_buf, 2, CL2_buf, MIFARE_CL2_MAX,
				                                        10, /* 10ms TX/RX Timeout */
				                                        0); /* TX CRC disabled */

				if (CL2_buf_size > 0) {
					for (i = 0; i < 4; i++) {
						data->uid_buf[data->uid_buf_nb_rx_data] = CL2_buf[i];
						data->uid_buf_nb_rx_data++;
					}

					/*
					data_buf[0] = RSSI_LEVELS;
					Trf797xReadSingle(data_buf, 1);
					if (data_buf[0] < 0x40)
						cprintf(con, "RSSI error: 0x%02lX (should be > 0x40)\r\n", (uint32_t)data_buf[0]);
					*/
					/*
					 * Select RX with CRC_A
					 * Configure Mode ISO Control Register (0x01) to 0x08
					 * (ISO14443A RX bit rate, 106 kbps) and RX CRC (CRC
					 * is present in the response)
					 */
					data_buf[0] = ISO_CONTROL;
					data_buf[1] = 0x08;
					Trf797xWriteSingle(data_buf, 2);

					/* Send AntiColl Cascade Level2 (2 bytes)+4 UID bytes(4 bytes) and receive SAK2 (1 byte) */
					data_buf[0] = 0x95;
					data_buf[1] = 0x70;

					for (i = 0; i < CL2_buf_size; i++) {
						data_buf[2 + i] = CL2_buf[i];
					}

					data->sak2_buf_nb_rx_data = Trf797x_transceive_bytes(data_buf, (2 + CL2_buf_size), data->sak2_buf, MIFARE_SAK_MAX,
					                            20, /* 10ms TX/RX Timeout */
					                            1); /* TX CRC disabled */

					if (data->sak2_buf_nb_rx_data > 0) {
						/* Check if it is a Mifare Ultra Light */
						if( (data->atqa_buf[0] == 0x44) && (data->atqa_buf[1] == 0x00) &&
						    (data->sak1_buf[0] == 0x04) && (data->sak2_buf[1] == 0x00)
						  ) {
							for (i = 0; i < 16; i+=4) {
								/* Send Read 16 bytes Mifare UL (2Bytes+CRC) */
								data_buf[0] = 0x30;
								data_buf[1] = (uint8_t)i;
								data->mf_ul_data_nb_rx_data += Trf797x_transceive_bytes(data_buf, 2, &data->mf_ul_data[data->mf_ul_data_nb_rx_data], MIFARE_UL_DATA,
								                               20, /* 20ms TX/RX Timeout */
								                               1); /* TX CRC enabled */
							}
						}
					} else {
						/* Send HALT 2Bytes (CRC is added automatically) */
						data_buf[0] = 0x50;
						data_buf[1] = 0x00;
						data->halt_buf_nb_rx_data += Trf797x_transceive_bytes(data_buf, 2, data->halt_buf, MIFARE_HALT_MAX,
						                             20, /* 20ms TX/RX Timeout */
						                             1); /* TX CRC enabled */
					}
				}
			}
		}

		/* tag 4 bytes UID */
		else {
			data->uid_buf_nb_rx_data = Trf797x_transceive_bytes(data_buf, 2, data->uid_buf, MIFARE_UID_MAX,
			                           10, /* 10ms TX/RX Timeout */
			                           0); /* TX CRC disabled */
			if (data->uid_buf_nb_rx_data > 0) {
				/*
				data_buf[0] = RSSI_LEVELS;
				Trf797xReadSingle(data_buf, 1);
				if (data_buf[0] < 0x40)
					cprintf(con, "RSSI error: 0x%02lX (should be > 0x40)\r\n", (uint32_t)data_buf[0]);
				*/

				/*
				* Select RX with CRC_A
				* Configure Mode ISO Control Register (0x01) to 0x08
				* (ISO14443A RX bit rate, 106 kbps) and RX CRC (CRC
				* is present in the response)
				*/
				data_buf[0] = ISO_CONTROL;
				data_buf[1] = 0x08;
				Trf797xWriteSingle(data_buf, 2);

				/* Finish Select (6 bytes) and receive SAK1 (1 byte) */
				data_buf[0] = 0x93;
				data_buf[1] = 0x70;
				for (i = 0; i < data->uid_buf_nb_rx_data; i++) {
					data_buf[2 + i] = data->uid_buf[i];
				}
				data->sak1_buf_nb_rx_data = Trf797x_transceive_bytes(data_buf, (2 + data->uid_buf_nb_rx_data),  data->sak1_buf, MIFARE_SAK_MAX,
				                            20, /* 20ms TX/RX Timeout */
				                            1); /* TX CRC enabled */
				/* Send HALT 2Bytes (CRC is added automatically) */
				data_buf[0] = 0x50;
				data_buf[1] = 0x00;
				data->halt_buf_nb_rx_data = Trf797x_transceive_bytes(data_buf, 2, data->halt_buf, MIFARE_HALT_MAX,
				                            20, /* 20ms TX/RX Timeout */
				                            1); /* TX CRC enabled */
			}
		}
	}

	/* Turn RF OFF (Chip Status Control Register (0x00)) */
	Trf797xTurnRfOff();
}
#endif

#if 0
// TODO hydranfc_v2_scan_mifare
void hydranfc_v2_scan_mifare(t_hydra_console *con)
{
	int i;
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
	}
	/*
	cprintf(con, "irq_count: 0x%02ld\r\n", (uint32_t)irq_count);
	irq_count = 0;
	*/
}
#endif

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
	/*
	cprintf(con, "irq_count: 0x%02ld\r\n", (uint32_t)irq_count);
	irq_count = 0;
	*/
}
#endif

#if 0
// TODO hydranfc_v2_scan_vicinity
void hydranfc_v2_scan_vicinity(t_hydra_console *con)
{
	static uint8_t data_buf[VICINITY_UID_MAX];
	uint8_t fifo_size;
	int i;

	/* End Test delay */
	irq_count = 0;

	/* Test ISO15693 read UID */
	Trf797xInitialSettings();
	Trf797xResetFIFO();

	/* Write Modulator and SYS_CLK Control Register (0x09) (13.56Mhz SYS_CLK and default Clock 13.56Mhz)) */
	data_buf[0] = MODULATOR_CONTROL;
	data_buf[1] = 0x31;
	Trf797xWriteSingle(data_buf, 2);

	/* Configure Mode ISO Control Register (0x01) to 0x02 (ISO15693 high bit rate, one subcarrier, 1 out of 4) */
	data_buf[0] = ISO_CONTROL;
	data_buf[1] = 0x02;
	Trf797xWriteSingle(data_buf, 2);

	/* Configure Test Settings 1 to BIT6/0x40 => MOD Pin becomes receiver subcarrier output (Digital Output for RX/TX) */
	/*
	data_buf[0] = TEST_SETTINGS_1;
	data_buf[1] = BIT6;
	Trf797xWriteSingle(data_buf, 2);

	data_buf[0] = TEST_SETTINGS_1;
	Trf797xReadSingle(data_buf, 1);
	if (data_buf[0] != 0x40)
	{
		cprintf(con, "Error Test Settings Register(0x1A) read=0x%02lX (shall be 0x40)\r\n", (uint32_t)data_buf[0]);
		err++;
	}
	*/

	/* Turn RF ON (Chip Status Control Register (0x00)) */
	Trf797xTurnRfOn();

	McuDelayMillisecond(10);

	/* Send Inventory(3B) and receive data + UID */
	data_buf[0] = 0x26; /* Request Flags */
	data_buf[1] = 0x01; /* Inventory Command */
	data_buf[2] = 0x00; /* Mask */

	fifo_size = Trf797x_transceive_bytes(data_buf, 3, data_buf, VICINITY_UID_MAX,
	                                     10, /* 10ms TX/RX Timeout (shall be less than 10ms (6ms) in High Speed) */
	                                     1); /* CRC enabled */
	if (fifo_size > 0) {
		/* fifo_size should be 10. */
		cprintf(con, "UID:");
		for (i = 0; i < fifo_size; i++)
			cprintf(con, " 0x%02lX", (uint32_t)data_buf[i]);
		cprintf(con, "\r\n");

		/* Read RSSI levels and oscillator status(0x0F/0x4F) */
		data_buf[0] = RSSI_LEVELS;
		Trf797xReadSingle(data_buf, 1);
		if (data_buf[0] < 0x40) {
			cprintf(con, "RSSI error: 0x%02lX (should be > 0x40)\r\n", (uint32_t)data_buf[0]);
		}
	}

	/* Turn RF OFF (Chip Status Control Register (0x00)) */
	Trf797xTurnRfOff();

	/*
	cprintf(con, "irq_count: 0x%02ld\r\n", (uint32_t)irq_count);
	irq_count = 0;
	*/
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

	/* Init st25r3916 IRQ function callback */
	st25r3916_irq_fn = st25r3916Isr;

	hydranfc_ce_common(con, FALSE);

	irq_count = 0;
	st25r3916_irq_fn = NULL;
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

	/* Stop & Start External IRQ */
	st25r3916_irq_fn = NULL;

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

			/* Init st25r3916 IRQ function callback */
			st25r3916_irq_fn = st25r3916Isr;

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

			irq_count = 0;
			st25r3916_irq_fn = NULL;
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
			/* Init st25r3916 IRQ function callback */
			st25r3916_irq_fn = st25r3916Isr;
			hydranfc_v2_reader_connect(con);

			irq_count = 0;
			st25r3916_irq_fn = NULL;

			break;
		}

		case T_CARD_SEND: {
			/* Init st25r3916 IRQ function callback */
			st25r3916_irq_fn = st25r3916Isr;
			hydranfc_v2_reader_send(con, (uint8_t *) p->buf);

			irq_count = 0;
			st25r3916_irq_fn = NULL;

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
		cprintf(con, "0x%02x\t: 0x%02x\r\n", i,	regDump.RsB[i]);
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

	if(init_gpio_spi_nfc(con) ==  FALSE) {
		deinit_gpio_spi_nfc(con);
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

	deinit_gpio_spi_nfc(con);
}

/** \brief Check if HydraNFC is detected
 *
 * \return bool: return TRUE if success or FALSE if failure
 *
 */
bool hydranfc_v2_is_detected(void)
{
	if(init_gpio_spi_nfc(NULL) ==  FALSE) {
		deinit_gpio_spi_nfc(NULL);
		return FALSE;
	}
	return TRUE;
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
