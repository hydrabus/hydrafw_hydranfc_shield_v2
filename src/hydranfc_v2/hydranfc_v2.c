/*
 * HydraBus/HydraNFC v2
 *
 * Copyright (C) 2021 Benjamin VERNOUX
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

#include "bsp_gpio.h"

#include "rfal_analogConfig.h"
#include "rfal_rf.h"
#include "rfal_dpo.h"
#include "rfal_chip.h"
#include "st25r3916.h"
#include "st25r3916_irq.h"
#include "st25r3916_aat.h"

#include "hydranfc_v2.h"

/*
static volatile int irq_count;
static volatile int irq;
*/
static void (*st25r3916_irq_fn)(void) = NULL;

/* Do not Enable DPO to have maximum performances/range */
//#define DPO_ENABLE true

#ifdef DPO_ENABLE
static rfalDpoEntry dpoSetup[] = {
	// new antenna board
	{.rfoRes=0, .inc=255, .dec=115},
	{.rfoRes=2, .inc=100, .dec=0x00}
};
#endif

extern t_mode_config mode_con1;

void rfalPreTransceiveCb(void)
{
	rfalDpoAdjust();
}

/* Triggered when the Ext IRQ (ST25R3916 IRQ) event appears. */
static void extcb1(void *arg) {
	(void) arg;

	if (st25r3916_irq_fn != NULL)
		st25r3916_irq_fn();
/*
	irq_count++;
	irq = 1;
*/
}

static ReturnCode init_RFAL(t_hydra_console *con)
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

static bool init_gpio_spi_nfc(t_hydra_console *con, irq_callback_t st25r3916_irq_callback)
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
	st25r3916_irq_fn = st25r3916_irq_callback;
	hal_st25r3916_spiInit(ST25R391X_SPI_DEVICE);
	if (init_RFAL(con) != ERR_NONE) {
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

/** \brief Initialize and Check if HydraNFC v2 is detected
 *
 * \return bool: return TRUE if success or FALSE if failure
 *
 */
bool hydranfc_v2_is_detected(void)
{
	if(init_gpio_spi_nfc(NULL, NULL) ==  FALSE) {
		deinit_gpio_spi_nfc(NULL);
		return FALSE;
	}
	return TRUE;
}

/** \brief Initialize HydraNFC v2
 *
 * \param con t_hydra_console*: hydra console (optional can be NULL if unused)
 * \return bool: return TRUE if success or FALSE if failure
 *
 */
bool hydranfc_v2_init(t_hydra_console *con, irq_callback_t st25r3916_irq_callback)
{
	if(init_gpio_spi_nfc(con, st25r3916_irq_callback) ==  FALSE) {
		deinit_gpio_spi_nfc(con);
		return FALSE;
	}
	return TRUE;
}

/** \brief DeInit/Cleanup HydraNFC v2 function
 *
 * \param con t_hydra_console*: hydra console (optional can be NULL if unused)
 * \return void
 *
 */
void hydranfc_v2_cleanup(t_hydra_console *con)
{
	deinit_gpio_spi_nfc(con);
}

void hydranfc_v2_set_irq(irq_callback_t st25r3916_irq_callback)
{
	st25r3916_irq_fn = st25r3916_irq_callback;
}
