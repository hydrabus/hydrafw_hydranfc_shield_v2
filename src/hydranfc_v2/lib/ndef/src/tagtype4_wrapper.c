#include "lib_wrapper.h"
#include "lib_NDEF.h"

uint16_t NfcType4_GetLength(uint16_t* Length)
{
  uint8_t err = BSP_NFCTAG_ReadData(Length,0,2);
  *Length = (*Length & 0xFF) << 8 | (*Length & 0xFF00) >> 8;

  if(err != NFCTAG_OK)
  {
    return NDEF_ERROR;
  }
  return NDEF_OK;
}

uint16_t NfcType4_ReadNDEF( uint8_t* pData )
{
  uint16_t length;
  uint8_t err;
  uint16_t status = NfcType4_GetLength(&length);
  if(status != NDEF_OK)
  {
    return status;
  }
  err = BSP_NFCTAG_ReadData(pData,2,length);
  if(err != NFCTAG_OK)
  {
    return NDEF_ERROR;
  }
  return NDEF_OK;

}

uint16_t NfcType4_WriteNDEF(uint16_t Length, uint8_t* pData )
{
  uint8_t txLen[2];
  txLen[0] = Length >> 8;
  txLen[1] = Length & 0xFF;
  uint16_t status = BSP_NFCTAG_WriteData(txLen, 0, 2);
  if(status != NDEF_OK)
  {
    return status;
  }
  return BSP_NFCTAG_WriteData(pData, 2, Length);
}

uint16_t NfcType4_WriteProprietary(uint16_t Length, uint8_t* pData )
{
  return NDEF_ERROR;
}
