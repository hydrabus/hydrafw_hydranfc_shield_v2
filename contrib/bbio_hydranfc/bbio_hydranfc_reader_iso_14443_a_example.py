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

print("set mode ISO 14443-A")
ser.write(b"\x06")

print("set field off")
ser.write(b"\x02")

print("set field on")
ser.write(b"\x03")

def req_a(ser):
    print("Request REQ A")
    ser.write(b"\x08")
    l = int.from_bytes(ser.read(1), byteorder="little")
    r = ser.read(l)
    print(f"\t{r.hex()}")
    return r

req_a(ser)

def write_bytes(ser, data, crc):

    ser.write(b"\x05")
    ser.write(crc.to_bytes(1, byteorder="big"))
    ser.write(len(data).to_bytes(1, byteorder="big"))
    ser.write(data)

    rx_len = int.from_bytes(ser.read(1), byteorder="little")

    return ser.read(rx_len)

# Perform anti-collision sequence
r = write_bytes(ser, b'\x93\x20', 0).hex()
print(r)
assert r == "b9cc137117"

r = write_bytes(ser, bytes.fromhex("93 70 B9 CC 13 71 17"), 1).hex()
print(r)
assert r == "20"

# Send RATS
r = write_bytes(ser, bytes.fromhex("E0 00"), 1).hex()
print(r)
assert r == "0a788082022063cba3a0"

# Send PPS
r = write_bytes(ser, bytes.fromhex("D0 01"), 1).hex()
print(r)
assert r == "d0"

# Send APDU
r = write_bytes(ser, bytes.fromhex("0A 00 00 A4 04 00 0E 32   50 41 59 2E 53 59 53 2E 44 44 46 30 31 00"), 1).hex()
print(r)
assert r == "1a006f57840e325041592e535953"