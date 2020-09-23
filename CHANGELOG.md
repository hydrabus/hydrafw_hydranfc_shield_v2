# CHANGELOG of 'hydrafw' for HydraBus v1 & HydraNFC Shield v2
----------------------

#### 12.09.2020 - [HydraFW v0.2 Beta](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/releases/tag/v0.2-beta)

##### Generic:
 * Update ChibiOS 18.2.0 (git version)
 * Plug & Play USB Serial Driver using standard driver for windows 8.1 & 10 (Thanks to Guigz2000)
 * HydraNFC v2 HAL fix platformProtectST25RComm() / platformUnprotectST25RComm() to mask/unmask the PA1 IRQ connected to ST25R3916 IRQ 

##### MCU Debug
 * Debug SWO/ITM Trace and added console UART (Thanks AAsyunkin-se for the pull request/contributions)

##### NFC BBIO mode
 * BBIO Reader supporting different technology NFC-A/B/V (Thanks gvinet for the pull request/contributions)
 * BBIO Card Emulator iso14443 a (Thanks gvinet for the pull request/contributions)
 * See [examples python3 scripts using the BBIO Reader / Card Emulator](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/tree/master/contrib/bbio_hydranfc)

#### NFC Shield v2 `nfc` mode
 * Commands `set-nfc-obsv` and `get-nfc-obsv` added
   *  It is for debug mode see ST25R3916 datasheet "Analog test and observation register 1" for more details
 * Add options to select technology NFC-A/B/ST25TB/V/F or ALL (NFC-A/B/V/F)
   * `nfc-all` Select technology NFC-A/B/V/F
   * `nfc-a` Select technology NFC-A(ISO14443A)
   * `nfc-b` Select technology NFC-B(ISO14443B)
   * `nfc-st25tb` Select technology NFC-B(ISO14443B ST25TB)
   * `nfc-v` Select technology NFC-V Vicinity(ISO15693)
   * `nfc-f` Select technology NFC-F FeliCa
 * Command `scan` add options scan once(by default) or `continuous` with `period` of scan (in ms) 
 * Command `scan` add more details
   * NFC-A/B/V/F add tag tx/rx bitrate in kbit/s
   * NFC-A add Tag Type
   * NFC-A add ATQA, SAK and ATS(on tag supporting it) details
   * See [HydraFW-HydraNFC-v2-guide#read-uid-of-any-nfc-tag-iso14443a-iso14443b-iso15693vicinity-felicanfc-abvf](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide#read-uid-of-any-nfc-tag-iso14443a-iso14443b-iso15693vicinity-felicanfc-abvf) for more details
 * Command `ce` added (Thanks AAsyunkin-se for the pull request/contributions)
   * Set Tag properties for Card Emulation (UID, SAK...) those parameters are common to `emul-3a` and `emul-t4t` commands
   * sub-parameter `uid' Set UID to be used for Card Emulation; 4 or 7 bytes. Hex string with no spaces or prefixes, e.g. f1e2d3c4
     * This parameter is required by `emul-3a` and/or `emul-t4t` commands
   * sub-parameter `sak' Set (final) SAK to be used for Card Emulation. 1 byte hex with no spaces or prefixes, e.g. 20
     * This parameter is required by `emul-3a` and/or `emul-t4t` commands
   * sub-parameter `uri' Set URI to be used for T4T Card Emulation
     * This parameter is required by `emul-t4t` command
   * See [HydraFW-HydraNFC-v2-guide#nfc-emulation-iso-14443a](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide#nfc-emulation-iso-14443a) for more details/examples
 * Command `emul-3a` added (Thanks AAsyunkin-se for the pull request/contributions)
   * Emul Tag ISO14443A using `ce` => `uid' and `sak' parameters
   * See [HydraFW-HydraNFC-v2-guide#nfc-emulation-iso-14443a](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide#nfc-emulation-iso-14443a) for more details/examples
 * Command `emul-t4t` added (Thanks AAsyunkin-se for the pull request/contributions)
   * Emul Tag ISO14443A T4T using `ce` => `uid', `sak' and `uri` parameters
   * See [HydraFW-HydraNFC-v2-guide#emulation-iso-14443a-tag-uid-4-or-7-bytes-sakconfigurable-ndef-with-data-uri](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide#emulation-iso-14443a-tag-uid-4-or-7-bytes-sakconfigurable-ndef-with-data-uri) for more details/examples
 * Commands `connect` and `send` added (Thanks gvinet for the pull request/contributions)
   * Add command `connect` Connect to a smartcard (ISO 14443 A & B) or ISO 15693/Vicinity card
   * Add command `send` Send APDU data to a card initialized with the connect command (ISO 14443 A & B tags), or data with automatic CRC computation for ISO 15693/Vicinity cards.
   * See [HydraFW-HydraNFC-v2-guide#connect-send-apdu-data](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide#connect-send-apdu-data) for more details/examples
 * Command `connect-opt` added (Thanks gvinet for the pull request/contributions)
   * sub-parameter `verbosity` Set verbosity. 0: print only APDU. 8: print all exchanges
   * sub-parameter `fsd` Frame size between 1 and 250 (only for ISO 14443 cards)

#### NFC Shield v2 `dnfc` mode (Debug/Developer mode)
 * See https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-mode
 * Add `dnfc` mode with Init of HydraNFC Shield v2 when entering in this mode
 * Protocol configuration/information `frequency` (to change SPI frequency), `show`, `show pins`, `show registers`
   * See [HydraFW-HydraNFC-v2-guide-dnfc-mode](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-mode) for more details/examples
 * Get/Set NFC Mode (NFCA/B/V/F/Custom)
   * Add command `set-nfc-mode` Set the NFC mode using following sub-parameter
     * sub-parameter `nfc-mode` Set NFC Mode to one of this value: POLL_NFCA=1, POLL_NFCA_T1T=2, POLL_NFCB=3, POLL_B_PRIME=4, POLL_B_CTS=5, POLL_NFCF=6, POLL_NFCV=7, POLL_PICOPASS=8, POLL_ACTIVE_P2P=9, LISTEN_NFCA=10, LISTEN_NFCB=11, LISTEN_NFCF=12, LISTEN_ACTIVE_P2P=13
     * sub-parameter `nfc-mode-tx_br` Set TX BitRate to one of this value: BR_106=0, BR_212=1, BR_424=2, BR_848=3, BR_52p97=235, BR_26p48=236, BR_1p66=237
     * sub-parameter `nfc-mode-rx_br` Set RX BitRate to one of this value: BR_106=0, BR_212=1, BR_424=2, BR_848=3, BR_52p97=235, BR_26p48=236, BR_1p66=237
   * Add command `get-nfc-mode` Get NFC Mode and parameters values
   * See [HydraFW-HydraNFC-v2-guide-dnfc-mode#debugdeveloper-mode-for-nfc-shield-v2-with-dnfc-mode-using-nfc-mode-and-doing-manual-anti-collision-on-nfc-a](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-mode#debugdeveloper-mode-for-nfc-shield-v2-with-dnfc-mode-using-nfc-mode-and-doing-manual-anti-collision-on-nfc-a) for more details/examples

 * Add commands to enable nfc field and send various data to tag (Thanks gvinet for the pull request/contributions)
   * Add command `nfc-off-on` to set RF field off/on 
   * Add command `nfc-reqa` to Send ISO 14443-A REQA
   * Add command `nfc-wupa` to Send ISO 14443-A WUPA
   * Add command `send` to Send bytes according to the selected mode
   * Add command `send-auto` to Send bytes according to the selected mode and add CRC
   * See [HydraFW-HydraNFC-v2-guide-dnfc-mode#debugdeveloper-mode-for-nfc-shield-v2-with-dnfc-mode-using-nfc-mode-and-doing-manual-anti-collision-on-nfc-a](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-mode#debugdeveloper-mode-for-nfc-shield-v2-with-dnfc-mode-using-nfc-mode-and-doing-manual-anti-collision-on-nfc-a) for more details/examples
 * Add Transparent test mode see command `nfc-transp` (Enter NFC Transparent Test Mode)
 * Add Stream test mode see command `nfc-stream` (Enter NFC Stream Test Mode)
 * Add Sniffer test mode see command `sniff` (Enter NFC Sniff Test Mode)
 * See [HydraFW-HydraNFC-v2-guide-dnfc-SPI](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-SPI)
 * See [HydraFW-HydraNFC-v2-guide-dnfc-Bus-interaction-commands](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-Bus-interaction-commands)

#### Documentation / Wiki
 * Wiki Getting Started with HydraBus and STM32CubeIDE (Include also advanced debugging with SWO/ITM Traces) see https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/Getting-Started-with-HydraBus-and-STM32CubeIDE 
 * [HydraFW-HydraNFC-v2-guide](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide)
 * [HydraFW-HydraNFC-v2 Dev/Debug dnfc guide](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/wiki/HydraFW-HydraNFC-v2-guide-dnfc-mode)
 * [Read & Fuzz contactless smart cards with HydraNFC v2 (Part 1)](https://hydrabus.com/2020/08/29/read-fuzz-contactless-smart-cards-with-hydranfc-v2)
 * [Read & Fuzz contactless smart cards with HydraNFC v2 (Part 2)](https://hydrabus.com/2020/08/30/read-fuzz-contactless-smart-cards-with-hydranfc-v2-part-2)

#### HydraNFC Shield v2 st25r3916 NFC chipset spi decoder/dumps for sigrok/PulseView
HydraNFC Shield v2 / ST25R39xx chipset NFC decoder available in [Sigrok/PulseView nigthly builds](https://sigrok.org/wiki/Downloads)
 * [st25r39xx Decoder available in official sigrokproject repository](https://github.com/sigrokproject/libsigrokdecode/tree/master/decoders/st25r39xx_spi)
 * [st25r39xx Dumps available in official sigrokproject repository](https://github.com/sigrokproject/sigrok-dumps/tree/master/nfc/st25r39xx)
 * See also [original repository](https://github.com/hydrabus/st25r3916_decoder_dumps_sigrok)

#### 08.07.2020 - [HydraFW v0.1 Beta](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/releases/tag/v0.1-beta)
First version (0.1 Beta) for HydraBus v1 & HydraNFC Shield v2
 * Support of STMicroelectronics ST RFAL V2.2.0 / 22-May-2020 see https://www.st.com/en/embedded-software/stsw-st25rfal002.html
   * Type `nfc` to initialize and check HydraNFC Shield v2 (ST25R3916) then display prompt `>NFCv2` if all is ok
 * `show registers` Show all ST25R3916 registers (Space A & B)
 * `scan` Do full anti-collision (manage multi tags at same time) and display Tag(s) UID for Tags Type NFC-A/B/V/F
