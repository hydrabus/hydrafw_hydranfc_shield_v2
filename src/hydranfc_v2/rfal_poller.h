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

#ifndef RFAL_POLLER_H_
#define RFAL_POLLER_H_

#include "common.h" /* required for t_hydra_console */

typedef enum{
	NFC_ALL,
	NFC_A,
	NFC_B,
	NFC_ST25TB,
	NFC_V,
	NFC_F
} nfc_technology_t;

typedef struct {
	char str[8]; /* Conversion of nfc_technology_t to string ("A" for NFC_A ...) */
} nfc_technology_str_t;

void nfc_technology_to_str(nfc_technology_t nfc_tech, nfc_technology_str_t* str_tag);

void ScanTags(t_hydra_console *con, nfc_technology_t nfc_tech);

#endif /* RFAL_POLLER_H_ */
