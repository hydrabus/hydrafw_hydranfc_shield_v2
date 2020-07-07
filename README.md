HydraFW official firmware for HydraBus v1/HydraNFC Shield v2
========

HydraFW is a native C (and asm) open source firmware for HydraBus board with support of HydraNFC Shield v2.
This specific firmware for HydraBus v1/HydraNFC Shield v2 is a fork of https://github.com/hydrabus/hydrafw commit [1cce35acc76758828e4d3a16139fb7899804acbb](https://github.com/hydrabus/hydrafw/commit/1cce35acc76758828e4d3a16139fb7899804acbb) 

You can Buy HydraBus v1/HydraNFC Shield v2 Online: http://hydrabus.com/buy-online

![HydraBus+HydraNFC Shield V2 boards](HydraBus_v1_HydraNFC_Shield_v2.jpg)

HydraNFC Shield v2 is based on [STMicroelectronics ST25R3916 NFC chipset](https://www.st.com/en/nfc/st25r3916.html) which is today the most powerful and versatile NFC chipset available on the market.
HydraNFC Shield v2 is for anyone interested in Learning/Developping/Debugging/Hacking/Penetration Testing for basic or advanced NFC communications.

**Key Features of HydraNFC Shield V2 hardware (with HydraNFC v2 Antenna):**
* Operating modes
  * Reader/writer
  * Card emulation (Type A and Type F Tags are supported natively, other tags can be supported with Special stream and Transparent modes)
  * Active and passive peer to peer 
* RF communication
  * EMVCo™ 3.0 analog and digital compliant
  * NFC-A / ISO14443A up to 848 kbit/s
  * NFC-B / ISO14443B up to 848 kbit/s
  * NFC-F / FeliCa™ up to 424 kbit/s
  * NFC-V / ISO15693 up to 53 kb/s
  * NFC-A / ISO14443A and NFC-F / FeliCa™ card emulation
  * Active and passive peer to peer initiator and target modes, up to 424 kbit/s
  * Low level modes (Special stream and Transparent modes) to implement MIFARE Classic® compliant or other custom protocols 
* Key features
  * 1.6W output power at 5V with Differential Antenna and Variable capacitors for NFC Automatic antenna tuning (AAT)
    * Reader/writer mode range is more than 12cm for NFC-A (ISO14443A) Tags(Credit Card format)
    * Reader/writer mode range is more than 16cm for NFC-V (ISO15693) Tags(ST25 Tag 5cm x 5cm)
  * Dynamic power output (DPO) controls the field strength to stay within given limits
  * Active wave shaping (AWS) reduces over-and under-shoots
  * Noise suppression receiver (NSR) allows reception in noisy environment
  * Integrated EMVCo 3.0 compliant EMD handling
  * Automatic gain control and squelch feature to maximize SNR
  * Low power capacitive and inductive card detection
  * Low power NFC active and passive target modes
  * Adjustable ASK modulation depth, from 5 to 40%
  * Integrated regulators to boost system PSRR
  * AM/PM and I/Q demodulator with baseband channel summation or automatic channel selection
  * Possibility to drive two independent single ended antennas (default use differential antenna)
  * Measurement of antenna voltage amplitude and phase, RSSI, on-chip supply and regulated voltages
* External communication interfaces
  * 512-byte FIFO
  * Serial peripheral interface (SPI) up to 10 Mbit/s (Tested with success on HydraBus v1 at 10.5Mbit/s)
* Electrical characteristics
  * Wide supply voltage and ambient temperature range (2.6 to 5.5 V from -40 °C to +105 °C, 2.4 to 5.5 V from -20 °C to +105 °C)
  * Wide peripheral communication supply range, from 1.65 to 5.5 V
  * Quartz oscillator operating with 27.12 MHz crystal with fast start-up 

**Key Features of HydraFW (for HydraBus v1/HydraNFC Shield v2 hardware) firmware using [STMicroelectronics RFAL for ST25R3916](https://www.st.com/content/st_com/en/products/embedded-software/st25-nfc-rfid-software/stsw-st25rfal002.html):**
This firmware is fully open source and based on HydraFW and is specific to HydraBus v1 with HydraNFC Shield v2 it use [STMicroelectronics RFAL for ST25R3916](https://www.st.com/content/st_com/en/products/embedded-software/st25-nfc-rfid-software/stsw-st25rfal002.html)
The RFAL simplifies the development of applications by encapsulating device and protocol details.
* Complete middleware to build NFC enabled applications based on the ST25R3916 device
* Supported modes:
  * Reader/Writer
  * P2P initiator (PCM and ACM)
  * P2P target (PCM and ACM)
  * Card emulation
* Support of major NFC and proprietary technologies:
  * NFC-A (ISO14443A)
  * NFC-B (ISO14443B)
  * NFC-F (FeliCa™)
  * NFC-V (ISO15693)
  * P2P (ISO18092)
  * ST25TB, Kovio, B’, iClass, Calypso®
* Supported protocols:
  * ISO-DEP (ISO data exchange protocol, ISO14443-4)
  * NFC-DEP (NFC data exchange protocol, ISO18092)
* Easy portability across different platforms (MCUs/RTOSs/OSs)
* Compliant with main RF/NFC standards:
  * NFC Forum
  * EMVCo™
  * ISO14443
  * ISO15693
  * ISO18092
* MISRA C:2012 compliant
* Free, user-friendly license terms
