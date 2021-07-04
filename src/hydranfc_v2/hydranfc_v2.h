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

#ifndef _HYDRANFC_V2_H_
#define _HYDRANFC_V2_H_

#include "common.h"

/* TODO macro in HAL... */
/* USer Button K1/2 Configured as Input */
#ifdef HYDRANFC_V2_NO_BTNS
#define K1_BUTTON FALSE
#define K2_BUTTON FALSE
#else
#define K1_BUTTON (palReadPad(GPIOB, 8))
#define K2_BUTTON (palReadPad(GPIOB, 9))
#endif

/* LEDs D1/D2/D3/D4 Configured as Output */
#define D1_ON  (palSetPad(GPIOB, 0))
#define D1_OFF (palClearPad(GPIOB, 0))
#define D1_TOGGLE (palTogglePad(GPIOB, 0))

#define D2_ON  (palSetPad(GPIOB, 3))
#define D2_OFF (palClearPad(GPIOB, 3))

#define D3_ON  (palSetPad(GPIOB, 4))
#define D3_OFF (palClearPad(GPIOB, 4))

#define D4_ON  (palSetPad(GPIOB, 5))
#define D4_OFF (palClearPad(GPIOB, 5))

typedef void (*irq_callback_t)(void);

bool hydranfc_v2_is_detected(void);

bool hydranfc_v2_init(t_hydra_console *con, irq_callback_t st25r3916_irq_callback);
void hydranfc_v2_set_irq(irq_callback_t st25r3916_irq_callback);

void hydranfc_v2_cleanup(t_hydra_console *con);

#endif /* _HYDRANFC_V2_H_ */

