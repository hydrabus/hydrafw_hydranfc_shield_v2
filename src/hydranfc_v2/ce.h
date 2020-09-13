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

#ifndef CARDEMULATION_H
#define CARDEMULATION_H

#include <stdint.h>
#include "st_errno.h"

enum cardEmulationMode {
	CARDEMULATION_MODE_NDEF                     = 0x01,
	CARDEMULATION_MODE_REFLECT                  = 0x02,
	CARDEMULATION_PROCESS_INTERNAL              = 0x80,
};

enum cardEmulationCommand {
	CARDEMULATION_CMD_START                     = 0x01, /*!< start listen mode. */
	CARDEMULATION_CMD_STOP                      = 0x02, /*!< stop listen mode. */

	CARDEMULATION_CMD_GET_RX_A                  = 0x11,
	CARDEMULATION_CMD_SET_TX_A                  = 0x12,
	CARDEMULATION_CMD_GET_RX_B                  = 0x13,
	CARDEMULATION_CMD_SET_TX_B                  = 0x14,
	CARDEMULATION_CMD_GET_RX_F                  = 0x15,
	CARDEMULATION_CMD_SET_TX_F                  = 0x16,

	CARDEMULATION_CMD_GET_LISTEN_STATE          = 0x21,
};
//
//enum cardEmulationState
//{
//    CARDEMULATION_STATE_OFF                     = 0x00, /*!<  */
//    CARDEMULATION_STATE_IDLE                    = 0x01, /*!<  */
//    CARDEMULATION_STATE_DOWNLOAD                = 0x02, /*!<  */
//    CARDEMULATION_STATE_UPLOAD                  = 0x03, /*!<  */
//};

extern void ceInitalize( void );
extern void ceHandler( void );

extern ReturnCode ceStart(const uint8_t *rxData, const uint16_t rxSize);
extern ReturnCode ceStop( void );

extern ReturnCode ceGetRx(const uint8_t cmd, uint8_t *txData, uint16_t *txSize);
extern ReturnCode ceSetTx(const uint8_t cmd, const uint8_t* rxData, const uint16_t rxSize, bool is_card_reset_needed);

extern ReturnCode ceGetListenState(uint8_t *txData, uint16_t *txSize);

//extern void mem_printf_add(const char *fmt, ...);
//extern void mem_printf_dump(t_hydra_console *con);

#endif /* CARDEMULATION_H */
