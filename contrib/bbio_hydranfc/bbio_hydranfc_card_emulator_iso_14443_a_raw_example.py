#!/usr/bin/env python3
#
# Low level script to show how to emulate an ISO 14443-A card
# You shall use https://github.com/hydrabus/pyHydrabus or https://github.com/gvinet/pynfcreader to have a user-friendly high level API
#
# Author: Guillaume VINET <guillaume.vinet@gmail.com>
# License: GPLv3 (https://choosealicense.com/licenses/gpl-3.0/)
#
import serial

port = "/dev/ttyACM1"

ser = serial.Serial(port, timeout=None)


# We enter BBIO1 MODE
def enter_bbio(ser):
    ser.timeout = 0.01
    for _ in range(20):
        ser.write(b"\x00")
        if b"BBIO1" in ser.read(5):
            ser.reset_input_buffer()
            ser.timeout = None

            # We enter reader mode
            ser.write(b"\x17")
            if ser.read(4) != b"NCE2":
                raise Exception("Cannot enter BBIO Card Emulator mode")
            return
    raise Exception("Cannot enter BBIO mode.")


enter_bbio(ser)


uid = bytes.fromhex("DEADBEEF")
print(f"Set UID to {uid.hex()}")
ser.write(b"\x04" + b"\x04" + uid)
resp = ser.read(1)
assert resp[0] == 0x01

sak = b"\x21"
print(f"Set SAK to {sak.hex()}")
ser.write(b"\x05" + b"\x01" + sak)
resp = ser.read(1)
assert resp[0] == 0x01

ats_hist_bytes = bytes("HydraNFC", "ascii")
print(f"Set ATS Historical bytes to {ats_hist_bytes.hex()}")
ser.write(b"\x0B" + bytes([len(ats_hist_bytes)]) + ats_hist_bytes)
resp = ser.read(1)
assert resp[0] == 0x01

print("Start Card emulation")
ser.write(b"\x0A")

BBIO_NFC_CE_CARD_ACTIVATION = 7
BBIO_NFC_CE_CARD_CMD = 8
BBIO_NFC_CE_END_EMULATION = 9

while 1:
    # We get the command tag
    cmd_tag = int.from_bytes(ser.read(1), byteorder="little")
    # print(f"Tag: {cmd_tag:02X}")

    if cmd_tag == BBIO_NFC_CE_END_EMULATION:
        print("Emulation stopped")
        break
    elif cmd_tag == BBIO_NFC_CE_CARD_ACTIVATION:
        # A card was activated
        print("Card activated")
    elif cmd_tag == BBIO_NFC_CE_CARD_CMD:

        # We get data length
        cmd_len = int.from_bytes(ser.read(2), byteorder="little")
        print(f"Len: {cmd_len}")

        # We get data
        cmd = ser.read(cmd_len)
        print(f"Cmd {cmd.hex()}")

        # We build the response
        resp = bytes.fromhex("CAFEBABE9000")
        resp_len = len(resp)
        ser.write(resp_len.to_bytes(2, byteorder="little"))
        ser.write(resp)
