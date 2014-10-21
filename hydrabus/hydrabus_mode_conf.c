/*
    HydraBus/HydraNFC - Copyright (C) 2012-2014 Benjamin VERNOUX

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "hydrabus_mode_conf.h"

#include "hydrabus_mode_hiz.h"
#include "hydrabus_mode_spi.h"

const mode_exec_t* hydrabus_mode_conf[HYDRABUS_MODE_NB_CONF] =
{
 /* 0 */ &mode_hiz_exec,
 /* 1 */ &mode_spi_exec,
 /* 2 */ &mode_hiz_exec,
 /* 3 */ &mode_hiz_exec,
 /* 4 */ &mode_hiz_exec,
 /* 5 */ &mode_hiz_exec,
 /* 6 */ &mode_hiz_exec,
 /* 7 */ &mode_hiz_exec
};
