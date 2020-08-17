# List of all the HydraNFC v2 hal related files.
HYDRANFC_V2_HAL_SRC = hydranfc_v2/hal/src/led.c \
                      hydranfc_v2/hal/src/logger.c \
                      hydranfc_v2/hal/src/spi.c \
                      hydranfc_v2/hal/src/timer.c \
                      hydranfc_v2/hal/src/rfal_analogConfigCustomTbl.c

#                      hydranfc_v2/hal/src/stream_dispatcher.c \
#                      hydranfc_v2/hal/src/stm32_usb_device_library_core/src/usbd_core.c \
#                      hydranfc_v2/hal/src/stm32_usb_device_library_core/src/usbd_ctlreq.c \
#                      hydranfc_v2/hal/src/stm32_usb_device_library_core/src/usbd_ioreq.c \
#                      hydranfc_v2/hal/src/stm32_usb_device_library_class_customhid/src/usbd_customhid.c \

# Required include directories
HYDRANFC_V2_HAL_INC = ./hydranfc_v2/hal/inc

#                      ./hydranfc_v2/hal/src/stm32_usb_device_library_core/inc \
#                      ./hydranfc_v2/hal/src/stm32_usb_device_library_class_customhid/inc
