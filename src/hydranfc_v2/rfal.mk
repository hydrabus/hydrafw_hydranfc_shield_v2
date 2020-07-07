# List of all the HydraNFC v2 RFAL files
HYDRANFC_V2_RFAL_SRC = hydranfc_v2/rfal/src/rfal_analogConfig.c \
                       hydranfc_v2/rfal/src/rfal_crc.c \
                       hydranfc_v2/rfal/src/rfal_dpo.c \
                       hydranfc_v2/rfal/src/rfal_iso15693_2.c \
                       hydranfc_v2/rfal/src/rfal_isoDep.c \
                       hydranfc_v2/rfal/src/rfal_nfc.c \
                       hydranfc_v2/rfal/src/rfal_nfca.c \
                       hydranfc_v2/rfal/src/rfal_nfcb.c \
                       hydranfc_v2/rfal/src/rfal_nfcDep.c \
                       hydranfc_v2/rfal/src/rfal_nfcf.c \
                       hydranfc_v2/rfal/src/rfal_nfcv.c \
                       hydranfc_v2/rfal/src/rfal_st25tb.c \
                       hydranfc_v2/rfal/src/rfal_st25xv.c \
                       hydranfc_v2/rfal/src/rfal_t1t.c \
                       hydranfc_v2/rfal/src/rfal_t2t.c \
                       hydranfc_v2/rfal/src/rfal_t4t.c \
                       hydranfc_v2/rfal/src/st25r3916/rfal_rfst25r3916.c \
                       hydranfc_v2/rfal/src/st25r3916/st25r3916.c \
                       hydranfc_v2/rfal/src/st25r3916/st25r3916_aat.c \
                       hydranfc_v2/rfal/src/st25r3916/st25r3916_com.c \
                       hydranfc_v2/rfal/src/st25r3916/st25r3916_irq.c \
                       hydranfc_v2/rfal/src/st25r3916/st25r3916_led.c

# Required include directories
HYDRANFC_V2_RFAL_INC = ./hydranfc_v2/rfal/src \
                       ./hydranfc_v2/rfal/inc \
                       ./hydranfc_v2/rfal/src/st25r3916

