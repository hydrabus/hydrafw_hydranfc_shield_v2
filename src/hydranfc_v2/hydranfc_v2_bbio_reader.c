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

#include <string.h>

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

extern t_mode_config mode_con1;

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
	//palDisablePadEvent(GPIOA, 1);
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

static void bbio_mode_id(t_hydra_console *con) {
	cprint(con, BBIO_HYDRANFC_READER, 4);
}

void bbio_mode_hydranfc_v2_reader(t_hydra_console *con) {
	uint8_t bbio_subcommand, rlen, clen, compute_crc;
	uint16_t rec_len;
	uint8_t *rx_data = (uint8_t *) g_sbuf + 4096;
	uint32_t flags;

	init_gpio_spi_nfc(con);

	bbio_mode_id(con);

	while (!hydrabus_ubtn()) {

		if (chnRead(con->sdu, &bbio_subcommand, 1) == 1) {
			switch (bbio_subcommand) {
				case BBIO_NFC_SET_MODE_ISO_14443A: {
					rfalNfcaPollerInitialize(); /* Initialize RFAL for NFC-A */
					break;
				}
				case BBIO_NFC_SET_MODE_ISO_15693: {
					// TODO
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

				case BBIO_NFC_CMD_SEND_BITS: {
					chnRead(con->sdu, rx_data, 2);

					rfalNfcaPollerCheckPresence(0x26, rx_data );

					rlen = 2;
					cprint(con, (char *) &rlen, 1);
					cprint(con, (char *) rx_data, rlen);
					break;
				}
				case BBIO_NFC_CMD_SEND_BYTES: {

					chnRead(con->sdu, &compute_crc, 1);
					chnRead(con->sdu, &clen, 1);
					chnRead(con->sdu, rx_data, clen);

					if( compute_crc){
						flags = RFAL_TXRX_FLAGS_DEFAULT;
					}
					else{
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
					deinit_gpio_spi_nfc(con);
					return;
				}
			}
		}
	}
}
