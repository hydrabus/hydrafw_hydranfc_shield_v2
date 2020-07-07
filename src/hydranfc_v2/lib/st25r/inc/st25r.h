
#include "stdint.h"
#include "rfal_nfca.h"
#include "rfal_nfcb.h"
#include "rfal_nfcf.h"
#include "rfal_nfcv.h"
#include "rfal_st25tb.h"
#include "rfal_isoDep.h"
#include "rfal_nfcDep.h"

#ifndef _ST25R_
#define _ST25R_

/** Type for Callback for long waiting time */
typedef uint8_t (BSP_NFCTAG_WaitingCallback_t)(void);
/** Callback for NFCA long waiting time */
extern BSP_NFCTAG_WaitingCallback_t *BSP_NFCTAG_WaitingCb;

/** Type tag definition */
typedef enum {
  BSP_NFCTAG_NFCA = 0,      /** NFCA type tag */
  BSP_NFCTAG_NFCB,          /** NFCB type tag */
  BSP_NFCTAG_ST25TB,        /** ST25TB type tag */
  BSP_NFCTAG_NFCF,          /** NFCF type tag */
  BSP_NFCTAG_NFCV           /** NFCV type tag */
} BSP_NFCTAG_Protocol_Id_t;

/** Device information structure - based on RFAL but adapted for NDEF support */
typedef struct {
  BSP_NFCTAG_Protocol_Id_t type;                    /* Device's type                */
  union {
    rfalNfcaListenDevice nfca;                      /* NFC-A Listen Device instance */
    rfalNfcbListenDevice nfcb;                      /* NFC-B Listen Device instance */
    rfalNfcfListenDevice nfcf;                      /* NFC-F Listen Device instance */
    rfalNfcvListenDevice nfcv;                      /* NFC-V Listen Device instance */
    rfalSt25tbListenDevice st25tb;                  /* ST25TB Listen Device instance */
  } dev;
  union {
      rfalIsoDepDevice isoDep;                        /* ISO-DEP instance             */
      rfalNfcDepDevice nfcDep;                        /* NFC-DEP instance             */
  }proto;                                             /* Device's protocol            */
  uint8_t NdefSupport;                                /* The device supports Ndef */
} BSP_NFCTAG_Device_t;

/** Porotcol API structure */
typedef struct {
uint8_t (*activate)(void);                              /** Activation phase API */
uint8_t (*read_data)(uint8_t* , uint32_t , uint32_t );  /** Read method */
uint8_t (*write_data)(uint8_t* , uint32_t , uint32_t ); /** Write method */
} BSP_NFCTAG_Protocol_t;

uint8_t BSP_NFCTAG_Activate(BSP_NFCTAG_Device_t * device);
uint8_t BSP_NFCTAG_ReadData(uint8_t* buffer, uint32_t offset, uint32_t length);
uint8_t BSP_NFCTAG_WriteData(uint8_t* buffer, uint32_t offset, uint32_t length);
uint16_t BSP_NFCTAG_GetByteSize(void);
uint8_t BSP_NFCTAG_T4_SelectFile(uint16_t fileId);
uint8_t BSP_NFCTAG_CheckVicinity(void);

#endif // _ST25R_
