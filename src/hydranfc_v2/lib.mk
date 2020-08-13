# List of all the HydraNFC v2 lib related files.
HYDRANFC_V2_LIB_SRC = hydranfc_v2/lib/ndef/src/lib_wrapper.c \
                      hydranfc_v2/lib/ndef/src/tagtype5_wrapper.c \
                      hydranfc_v2/lib/ndef/src/lib_NDEF_URI.c \
                      hydranfc_v2/lib/ndef/src/lib_NDEF.c \
                      hydranfc_v2/lib/st25r/src/st25r.c

# Required include directories
HYDRANFC_V2_LIB_INC = ./hydranfc_v2/lib/common/inc \
                      ./hydranfc_v2/lib/ndef/inc \
                      ./hydranfc_v2/lib/st25r/inc
