#!/usr/bin/env python3
#
# Low level script to show how to use BBIO NFC reader mode
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
            ser.write(b"\x0E")
            if ser.read(4) != b"NFC2":
                raise Exception("Cannot enter BBIO Reader mode")
            return
    raise Exception("Cannot enter BBIO mode.")


enter_bbio(ser)

print("set mode ISO 14443-B")
ser.write(b"\x09")

print("set field off")
ser.write(b"\x02")

print("set field on")
ser.write(b"\x03")


def write_bytes(ser, data, crc):
    ser.write(b"\x05")
    ser.write(crc.to_bytes(1, byteorder="big"))
    ser.write(len(data).to_bytes(1, byteorder="big"))
    ser.write(data)

    rx_len = int.from_bytes(ser.read(1), byteorder="little")

    return ser.read(rx_len)


# Send REQ-B
r = write_bytes(ser, b'\x05\x00\x00', 1).hex()
print(r)
assert r == "5001fc900400000000808171"

# Send ATTRIB-B
r = write_bytes(ser, bytes.fromhex("1D 01fc9004 00 00 01 00 "), 1).hex()
print(r)
assert r == "00"

# Send APDU
r = write_bytes(ser, bytes.fromhex("0A 00 00 A4 04 00 0E 32   50 41 59 2E 53 59 53 2E 44 44 46 30 31 00"), 1).hex()
print(r)
assert r == "1a006f5b840e325041592e535953"
