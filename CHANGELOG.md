# CHANGELOG of 'hydrafw' fro HydraBus v1 & HydraNFC Shield v2
----------------------

#### 08.07.2020 - [HydraFW v0.1 Beta](https://github.com/hydrabus/hydrafw_hydranfc_shield_v2/releases/tag/v0.1-beta)
First version (0.1 Beta) for HydraBus v1 & HydraNFC Shield v2
 * Support of STMicroelectronics ST RFAL V2.2.0 / 22-May-2020 see https://www.st.com/en/embedded-software/stsw-st25rfal002.html
   * Type `nfc` to initialize and check HydraNFC Shield v2 (ST25R3916) then display prompt `>NFCv2` if all is ok
 * `show registers` Show all ST25R3916 registers (Space A & B)
 * `scan` Do full anti-collision (manage multi tags at same time) and display Tag(s) UID for Tags Type NFC-A/B/V/F
