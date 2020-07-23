/*
 * HydraBus/HydraNFC
 *
 * Copyright (C) 2020 Alexander ASYUNKIN
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

#include "ff.h"
#include "microsd.h"
#include "bsp_print_dbg.h"
#include <stdlib.h>
#include <stdio.h>

#define CONSOLE_CONFIG_FILENAME "console"
#define LINEBUF_SZE 256

bool console_read_sd_config(int *ptr_usart_no, uint32_t *ptr_speed, bool *ptr_usb1, bool *ptr_usb2)
{
	uint8_t result = 0;
	int usb_no, usb_status;
	int usart_status, usart_number;
	uint32_t usart_speed;
	// we all hate malloc, but main's stack is very small...
	FIL *fp = (FIL *)malloc(sizeof(FIL));
	if (fp==NULL) {
		printf_dbg("Cannot allocate FIL resources\n");
		return FALSE;
	}

	uint8_t *linebuf = (uint8_t *)malloc(LINEBUF_SZE);
	if (linebuf==NULL) {
		printf_dbg("Cannot allocate linebuf resources\n");
		free(fp);
		return FALSE;
	}

	if (!is_file_present(CONSOLE_CONFIG_FILENAME)) {
		printf_dbg("File %s is not present on SD card or SD not ready\n", CONSOLE_CONFIG_FILENAME);
		free(fp);
		free(linebuf);
		return FALSE;
	}

	if (!file_open(fp, CONSOLE_CONFIG_FILENAME, 'r')) {
		printf_dbg("Failed to open file %s\r\n", CONSOLE_CONFIG_FILENAME);
		free(fp);
		free(linebuf);
		return FALSE;
	}

	while(file_readline(fp, linebuf, LINEBUF_SZE)) {

		if(linebuf[0] == '#') {
			continue;
		}

		usb_no = usb_status = 0;
//		result = sscanf((char *)linebuf, "%*USB%1d=%1d", &usb_no, &usb_status);
		result = sscanf((char *)linebuf, "USB%d=%d", (int *)&usb_no, (int *)&usb_status);
		if (result == 2) {
			switch (usb_no)
			{
			case 1:
				*ptr_usb1 = (usb_status == 1) ? TRUE : FALSE;
				printf_dbg("Processed USB1 entry\r\n");
				break;
			case 2:
				*ptr_usb2 = (usb_status == 1) ? TRUE : FALSE;
				printf_dbg("Processed USB2 entry\r\n");
				break;
			default:
				break;

			}
		}

		usart_status = usart_number = usart_speed = 0;
//		result = sscanf((char *)linebuf, "%*USART=%1d", &usart_status, &usart_number, &usart_speed);
		result = sscanf((char *)linebuf, "USART=%d;%d;%ld", (int *)&usart_status, (int *)&usart_number, &usart_speed);
		if (result == 3) {

			*ptr_usart_no = (usart_status == 1) ? usart_number : 0;
			*ptr_speed = usart_speed;
			printf_dbg("Processed USART entry\r\n");
		}
	}
	file_close(fp);
	free(fp);
	free(linebuf);

	return TRUE;
}

