/*
 * HydraBus/HydraNFC v2
 *
 * Copyright (C) 2020 Guillaume VINET
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

#define BBIO_HYDRANFC_READER	"NFC2"

void bbio_mode_hydranfc_v2_reader(t_hydra_console *con);
void my_connect(t_hydra_console *con);
void my_card_send(t_hydra_console *con, uint8_t * ascii_data);
