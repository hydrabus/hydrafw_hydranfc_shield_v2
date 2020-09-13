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

#ifndef HYDRANFC_V2_CE_H
#define HYDRANFC_V2_CE_H

typedef enum {
	T4T_MODE_NOT_SET = 0,
	T4T_MODE_URI,
	T4T_MODE_EMAIL
} eT4TEmulationMode;

typedef struct {
	uint32_t uid_len;
	uint8_t uid[8];
	uint32_t sak_len; // 0 or 1
	uint8_t sak[1];
	uint8_t *uri;
	bool level4_enabled;
	eT4TEmulationMode t4tEmulationMode;
} sUserTagProperties;

extern sUserTagProperties user_tag_properties;
extern uint8_t ul_cwrite_page_set;

void hydranfc_ce_common(t_hydra_console *con);

#endif

