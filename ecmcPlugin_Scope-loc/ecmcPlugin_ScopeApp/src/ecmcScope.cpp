/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcScope.cpp
*
*  Created on: Sept 22, 2020
*      Author: anderssandstrom
*      Credits to  https://github.com/sgreg/dynamic-loading 
*
\*************************************************************************/

// Needed to get headers in ecmc right...
#define ECMC_IS_PLUGIN

#define ECMC_PLUGIN_ASYN_PREFIX          "plugin.scope"
#define ECMC_PLUGIN_ASYN_ENABLE          "enable"
#define ECMC_PLUGIN_ASYN_RESULTDATA      "resultdata"
#define ECMC_PLUGIN_ASYN_SCOPE_SOURCE    "source"
#define ECMC_PLUGIN_ASYN_SCOPE_TRIGG     "trigg"
#define ECMC_PLUGIN_ASYN_SCOPE_NEXT_SYNC "nexttime"
#define ECMC_PLUGIN_ASYN_MISSED          "missed"
#define ECMC_PLUGIN_ASYN_TRIGG_COUNT     "count"

#define SCOPE_DBG_PRINT(str)  \
if(cfgDbgMode_) {             \
    printf(str);              \
}                             \

#define ECMC_MAX_32BIT 0xFFFFFFFF

#include <sstream>
#include "ecmcScope.h"
#include "ecmcPluginClient.h"
#include <limits>

/** ecmc Scope class
 * This object can throw: 
 *    - bad_alloc
 *    - invalid_argument
 *    - runtime_error
*/
ecmcScope::ecmcScope(int   scopeIndex,       // index of this object (if several is created)
                     char* configStr){
  SCOPE_DBG_PRINT("ecmcScope::ecmcScope()");
  cfgDataSourceStr_         = NULL;
  cfgDataNexttimeStr_       = NULL;
  cfgTriggStr_              = NULL;
  resultDataBuffer_         = NULL;  
  lastScanSourceDataBuffer_ = NULL;
  missedTriggs_             = 0;
  triggerCounter_           = 0;
  objectId_                 = scopeIndex;  
  triggOnce_                = 0;
  dataSourceLinked_         = 0;
  resultDataBufferBytes_    = 0;
  bytesInResultBuffer_      = 0;
  oldTriggTime_             = 0;
  triggTime_                = 0;
  sourceNexttime_           = 0;
  sourceSampleRateNS_       = 0;
  sourceElementsPerSample_  = 0;
  firstTrigg_               = 1; // Avoid first trigger (0 timestamp..)
  scopeState_               = ECMC_SCOPE_STATE_INVALID;
  ecmcSmapleTimeNS_         = (uint64_t)getEcmcSampleTimeMS()*1E6;
  
  // Asyn
  sourceStrParam_           = NULL;
  sourceNexttimeStrParam_   = NULL;
  triggStrParam_            = NULL;
  enbaleParam_              = NULL;
  resultParam_              = NULL;
  asynMissedTriggs_         = NULL;
  asynTriggerCounter_       = NULL;

  // ecmcDataItems
  sourceDataItem_           = NULL;
  sourceDataNexttimeItem_   = NULL;
  sourceTriggItem_          = NULL;

  sourceDataItemInfo_       = NULL;
  sourceDataNexttimeItemInfo_ = NULL;
  sourceTriggItemInfo_      = NULL;
  
  // Config defaults
  cfgDbgMode_               = 0;
  cfgBufferElementCount_    = ECMC_PLUGIN_DEFAULT_BUFFER_SIZE;
  cfgEnable_                = 1;   // start enabled (enable over asyn)
  
  parseConfigStr(configStr); // Assigns all configs
  
  // Check valid buffer size
  if(cfgBufferElementCount_ <= 0) {
    SCOPE_DBG_PRINT("ERROR: Configuration buffer size must be > 0.");
    throw std::out_of_range("ERROR: Configuration Buffer Size must be > 0.");
  }

  // Allocate buffers first at enter RT (since datatype is unknown here)
  resultDataBuffer_         = NULL;
  resultDataBufferBytes_    = 0;
}

ecmcScope::~ecmcScope() {
  
  if(resultDataBuffer_) {
    delete[] resultDataBuffer_;
  }

  if(lastScanSourceDataBuffer_) {
    delete[] lastScanSourceDataBuffer_;
  }

  if(cfgDataSourceStr_) {
    free(cfgDataSourceStr_);
  }
  if(cfgTriggStr_) {
    free(cfgTriggStr_);
  }
  if(cfgDataNexttimeStr_) {
    free(cfgDataNexttimeStr_);
  }  

}

void ecmcScope::parseConfigStr(char *configStr) {
  SCOPE_DBG_PRINT("ecmcScope::parseConfigStr()");
  // check config parameters
  if (configStr && configStr[0]) {    
    char *pOptions = strdup(configStr);
    char *pThisOption = pOptions;
    char *pNextOption = pOptions;
    
    while (pNextOption && pNextOption[0]) {
      pNextOption = strchr(pNextOption, ';');
      if (pNextOption) {
        *pNextOption = '\0'; /* Terminate */
        pNextOption++;       /* Jump to (possible) next */
      }
      
      // ECMC_PLUGIN_DBG_PRINT_OPTION_CMD (1/0)
      if (!strncmp(pThisOption, ECMC_PLUGIN_DBG_PRINT_OPTION_CMD, strlen(ECMC_PLUGIN_DBG_PRINT_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_DBG_PRINT_OPTION_CMD);
        cfgDbgMode_ = atoi(pThisOption);
      } 
      
      // ECMC_PLUGIN_SOURCE_OPTION_CMD (Source string)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_SOURCE_OPTION_CMD, strlen(ECMC_PLUGIN_SOURCE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_SOURCE_OPTION_CMD);
        cfgDataSourceStr_=strdup(pThisOption);
      }

      // ECMC_PLUGIN_RESULT_ELEMENTS_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_RESULT_ELEMENTS_OPTION_CMD, strlen(ECMC_PLUGIN_RESULT_ELEMENTS_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_RESULT_ELEMENTS_OPTION_CMD);
        cfgBufferElementCount_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_ENABLE_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_ENABLE_OPTION_CMD, strlen(ECMC_PLUGIN_ENABLE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_ENABLE_OPTION_CMD);
        cfgEnable_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_TRIGG_OPTION_CMD (string)     
      else if (!strncmp(pThisOption, ECMC_PLUGIN_TRIGG_OPTION_CMD, strlen(ECMC_PLUGIN_TRIGG_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_TRIGG_OPTION_CMD);
        cfgTriggStr_ = strdup(pThisOption);
      }

      // ECMC_PLUGIN_SOURCE_NEXTTIME_OPTION_CMD (string)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_SOURCE_NEXTTIME_OPTION_CMD, strlen(ECMC_PLUGIN_SOURCE_NEXTTIME_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_SOURCE_NEXTTIME_OPTION_CMD);
        cfgDataNexttimeStr_ = strdup(pThisOption);
      }


    //   ECMC_PLUGIN_MODE_OPTION_CMD CONT/TRIGG
    //   else if (!strncmp(pThisOption, ECMC_PLUGIN_MODE_OPTION_CMD, strlen(ECMC_PLUGIN_MODE_OPTION_CMD))) {
    //     pThisOption += strlen(ECMC_PLUGIN_MODE_OPTION_CMD);
    //     if(!strncmp(pThisOption, ECMC_PLUGIN_MODE_CONT_OPTION,strlen(ECMC_PLUGIN_MODE_CONT_OPTION))){
    //       cfgMode_ = CONT;
    //     }
    //     if(!strncmp(pThisOption, ECMC_PLUGIN_MODE_TRIGG_OPTION,strlen(ECMC_PLUGIN_MODE_TRIGG_OPTION))){
    //       cfgMode_ = TRIGG;
    //     }
    //   }

      pThisOption = pNextOption;
    }    
    free(pOptions);
  }

  // Data source must be defined...
  if(!cfgDataSourceStr_) { 
    SCOPE_DBG_PRINT("ERROR: Configuration Data source not defined.\n");
    throw std::invalid_argument( "ERROR: Data source not defined.");
  }
  if(!cfgTriggStr_) { 
    SCOPE_DBG_PRINT("ERROR: Configuration Trigger not defined.\n");
    throw std::invalid_argument( "ERROR: Configuration Trigger not defined.");
  }
  if(!cfgDataNexttimeStr_) { 
    SCOPE_DBG_PRINT("ERROR: Configuration  Nexttime not defined.\n");
    throw std::invalid_argument( "ERROR: Configuration Nexttime not defined.");
  }
}

void ecmcScope::connectToDataSources() {
  SCOPE_DBG_PRINT("ecmcScope::connectToDataSources()");
  /* Check if already linked (one call to connectToDataSources (enterRT) per loaded Scope lib (Scope object))
      But link should only happen once!!*/
  if( dataSourceLinked_ ) {
    return;
  }

  // Get source dataItem
  sourceDataItem_        = (ecmcDataItem*) getEcmcDataItem(cfgDataSourceStr_);
  if(!sourceDataItem_) {
    SCOPE_DBG_PRINT("ERROR: Source dataitem NULL.\n");
    throw std::runtime_error( "ERROR: Source dataitem NULL." );
  }
  sourceDataItemInfo_ = sourceDataItem_->getDataItemInfo();

  if(!sourceDataItemInfo_) {
    SCOPE_DBG_PRINT("ERROR: Source dataitem info NULL.\n");
    throw std::runtime_error( "ERROR: Source dataitem info NULL." );
  }

  // Allocate buffer for result
  resultDataBuffer_      = new uint8_t[cfgBufferElementCount_ * sourceDataItemInfo_->dataElementSize];
  memset(&resultDataBuffer_[0],0,cfgBufferElementCount_ * sourceDataItemInfo_->dataElementSize);
  resultDataBufferBytes_ = cfgBufferElementCount_ * sourceDataItemInfo_->dataElementSize;
  // Data for last scan cycle
  lastScanSourceDataBuffer_      = new uint8_t[sourceDataItemInfo_->dataSize];
  memset(&lastScanSourceDataBuffer_[0],0,sourceDataItemInfo_->dataSize);
  sourceElementsPerSample_ = sourceDataItemInfo_->dataSize / sourceDataItemInfo_->dataElementSize;
  sourceSampleRateNS_    = ecmcSmapleTimeNS_ / sourceElementsPerSample_;
  
  // Get source nexttime dataItem
  sourceDataNexttimeItem_        = (ecmcDataItem*) getEcmcDataItem(cfgDataNexttimeStr_);
  if(!sourceDataNexttimeItem_) {
    SCOPE_DBG_PRINT("ERROR: Source nexttime dataitem NULL.\n");
    throw std::runtime_error( "ERROR: Source nexttime dataitem NULL." );
  }
  sourceDataNexttimeItemInfo_ = sourceDataNexttimeItem_->getDataItemInfo();
  
  if(!sourceDataNexttimeItemInfo_) {
    SCOPE_DBG_PRINT("ERROR: Source nexttime dataitem info NULL.\n");
    throw std::runtime_error( "ERROR: Source nexttime dataitem info NULL." );
  }

  // Get trigg dataItem
  sourceTriggItem_        = (ecmcDataItem*) getEcmcDataItem(cfgTriggStr_);
  if(!sourceTriggItem_) {
    SCOPE_DBG_PRINT("ERROR: Trigg dataitem NULL.\n");
    throw std::runtime_error( "ERROR: Trigg dataitem NULL." );
  }
  
  sourceTriggItemInfo_ = sourceTriggItem_->getDataItemInfo();
  if(!sourceTriggItemInfo_) {
    SCOPE_DBG_PRINT("ERROR: Trigg dataitem info NULL.\n");
    throw std::runtime_error( "ERROR: Trigg dataitem info NULL." );
  }

  if( sourceTriggItem_->read((uint8_t*)(&oldTriggTime_),sourceTriggItemInfo_->dataElementSize)){
    SCOPE_DBG_PRINT("ERROR: Failed read trigg time.\n");
    throw std::runtime_error( "ERROR: Failed read trigg time." );
  }
  
  if(!sourceDataTypeSupported(sourceDataItem_->getEcmcDataType())) {
    SCOPE_DBG_PRINT("ERROR: Source data type not suppported.\n");
    throw std::runtime_error( "ERROR: Source data type not suppported.");
  }
  
  // Register asyn parameters
  initAsyn();

  dataSourceLinked_ = 1;
  scopeState_         = ECMC_SCOPE_STATE_WAIT_TRIGG;
}

bool ecmcScope::sourceDataTypeSupported(ecmcEcDataType dt) {
  
  SCOPE_DBG_PRINT("ecmcScope::sourceDataTypeSupported()");

  switch(dt) {
    case ECMC_EC_NONE:      
      return 0;
      break;
    case ECMC_EC_B1:
      return 0;
      break;
    case ECMC_EC_B2:
      return 0;
      break;
    case ECMC_EC_B3:
      return 0;
      break;
    case ECMC_EC_B4:
      return 0;
      break;
    default:
      return 1;
      break;
  }  
  return 1;
}

/**
 * Note: The code needs to handle both triggers in +- one ethercat cycle from the current cycle (also waiting for future analog values, if NEXT_TIME<TRIGG_TIME timestamps).
 * If trigger is more recent than NEXT_TIME timestamp then just wait for next cycle to see if ai timestamp catches up.
 * The analog samples from the prev cycles is always buffered to be able to also handle older timestamps (up to 2*NELM back in time)
 * Seems that the above behaviour depends on where the slaves are physically in the chain.
*/
void ecmcScope::execute() {

  size_t bytesToCp = 0;

  // Ensure ethercat bus is started
  if(getEcmcEpicsIOCState() < 15) {
    bytesInResultBuffer_ = 0;
    scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
    // Wait for new trigg
    setWaitForNextTrigg();
    return;
  }

  // Read trigg data
  if( sourceTriggItem_->read((uint8_t*)&triggTime_,sourceTriggItemInfo_->dataElementSize)){
    SCOPE_DBG_PRINT("ERROR: Failed read trigg time.\n");
    throw std::runtime_error( "ERROR: Failed read trigg time." );
  }

  // Read next sync timestamp
  if( sourceDataNexttimeItem_->read((uint8_t*)&sourceNexttime_,sourceDataNexttimeItemInfo_->dataElementSize)){
    SCOPE_DBG_PRINT("ERROR: Failed read ai nexttime.\n");
    throw std::runtime_error( "ERROR: Failed read nexttime." );
  }

  // Ensure enabled
  if(!cfgEnable_) {
    bytesInResultBuffer_ = 0;
    scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
    // Wait for new trigg
    setWaitForNextTrigg();
    return;
  }

  switch(scopeState_) {
    case ECMC_SCOPE_STATE_INVALID:
      SCOPE_DBG_PRINT("ERROR: Invalid state (state = ECMC_SCOPE_STATE_INVALID).");
      SCOPE_DBG_PRINT("INFO: Change state to ECMC_SCOPE_STATE_WAIT_TRIGG.\n");
      bytesInResultBuffer_ = 0;
      scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
      // Wait for new trigg
      setWaitForNextTrigg();
      return;
      break;
    
    case ECMC_SCOPE_STATE_WAIT_TRIGG:

    // New trigger then collect data (or wait )
    if(oldTriggTime_ != triggTime_ && !firstTrigg_) {      
      //printf("sourceNexttime_=%" PRIu64 " ,sourceDataNexttimeItemInfo_->dataSize = %zu\n",sourceNexttime_,sourceDataNexttimeItemInfo_->dataSize);

      // calculate how many samples ago trigger occured     
      int64_t samplesSinceLastTrigg = timeDiff() / sourceSampleRateNS_;
      
      if( samplesSinceLastTrigg > sourceElementsPerSample_ * 2) {
        SCOPE_DBG_PRINT("WARNING: Invalid trigger (occured more than two ethercat cycles ago)..");
        missedTriggs_++;
        asynMissedTriggs_->refreshParam(1);
        // Wait for new trigg (skip this trigger)
        setWaitForNextTrigg();
        return;
      }

      // Trigger is newer than ai next time. Wait for newer ai data to catch up (don't overwrite oldTriggTime_)
      if(samplesSinceLastTrigg < 0){

        printf("samplesSinceLastTrigg: %" PRId64 "\n",samplesSinceLastTrigg);
        return;
      }

      SCOPE_DBG_PRINT("INFO: New trigger detected.\n");      
      

      // Copy the the samples to result buffer (buffer allows to ethecat scans with value(s))      
      triggerCounter_++;
      asynTriggerCounter_->refreshParam(1);

      // Copy from last scan buffer if needed (if trigger occured during last scan)
      if(samplesSinceLastTrigg > sourceElementsPerSample_) {
        bytesToCp = (samplesSinceLastTrigg - sourceElementsPerSample_) * sourceDataItemInfo_->dataElementSize;
        if(resultDataBufferBytes_ < bytesToCp) {
          bytesToCp = resultDataBufferBytes_;
        }

        memcpy( &resultDataBuffer_[0], &lastScanSourceDataBuffer_[sourceElementsPerSample_*2-samplesSinceLastTrigg], bytesToCp);
        bytesInResultBuffer_ = bytesToCp;
      }

      // Copy from current scan if needed
      if(bytesInResultBuffer_ < resultDataBufferBytes_) {              
        bytesToCp = sourceDataItemInfo_->dataSize;
        // Ensure not to much data is copied
        if(bytesToCp > (resultDataBufferBytes_ - bytesInResultBuffer_)) {
          bytesToCp = resultDataBufferBytes_ - bytesInResultBuffer_;
        }
        
        // Write directtly into results buffer
        if( sourceDataItem_->read((uint8_t*)&resultDataBuffer_[bytesInResultBuffer_],bytesToCp)){
          SCOPE_DBG_PRINT("ERROR: Failed read data source.\n");
          throw std::runtime_error( "ERROR: Failed read data source." );
        }
        bytesInResultBuffer_ += bytesToCp;
      }

      // If more data is needed the go to collect state.
      if(bytesInResultBuffer_ < resultDataBufferBytes_) {
        // Fill more data from next scan
        scopeState_ = ECMC_SCOPE_STATE_COLLECT;          
      }
      else {  // The data from current scan was enough. send over asyn and then start over (wait for next trigger)        
        resultParam_->refreshParam(1);
        bytesInResultBuffer_ = 0;        
        SCOPE_DBG_PRINT("INFO: Result Buffer full. Data push over asyn..\n");
        if(cfgDbgMode_) {
          printEcDataArray(resultDataBuffer_,resultDataBufferBytes_,sourceDataItemInfo_->dataType,objectId_);
        }
      }
    }
    
    // Avoid first rubbish trigger timestamp (when first value is read from bus it will differ from "0" and therefor trigger)
    if(oldTriggTime_ != triggTime_) {
      firstTrigg_ = 0;  
    }

    // This trigg is handled. Wait for next trigger
    setWaitForNextTrigg();

    break;

    case ECMC_SCOPE_STATE_COLLECT:

      if (oldTriggTime_ != triggTime_) {
        SCOPE_DBG_PRINT("WARNING: Latch during sampling of data. This trigger will be disregarded.\n");        
        setWaitForNextTrigg();
        missedTriggs_++;
        asynMissedTriggs_->refreshParam(1);
      }
      
      // Ensure not to much data is copied
      if(bytesInResultBuffer_ < resultDataBufferBytes_) {              
        bytesToCp = sourceDataItemInfo_->dataSize;
        if(bytesToCp > (resultDataBufferBytes_ - bytesInResultBuffer_)) {
          bytesToCp = resultDataBufferBytes_ - bytesInResultBuffer_;
        }
        
        // Write directtly into results buffer
        if( sourceDataItem_->read((uint8_t*)&resultDataBuffer_[bytesInResultBuffer_],bytesToCp)){
          SCOPE_DBG_PRINT("ERROR: Failed read data source..\n");
          throw std::runtime_error( "ERROR: Failed read data source." );
        }
        bytesInResultBuffer_ += bytesToCp;
      }
     
      if(bytesInResultBuffer_ >= resultDataBufferBytes_) {
        resultParam_->refreshParam(1);
        bytesInResultBuffer_ = 0;
        scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
       // Wait for next trigger.
        setWaitForNextTrigg();
        SCOPE_DBG_PRINT("INFO: Change state to ECMC_SCOPE_STATE_WAIT_TRIGG.\n");
        SCOPE_DBG_PRINT("INFO: Result Buffer full. Data push over asyn..\n");
        if(cfgDbgMode_) {
          printEcDataArray(resultDataBuffer_,resultDataBufferBytes_,sourceDataItemInfo_->dataType,objectId_);
        }
      }

      // Wait for next trigger.
      setWaitForNextTrigg();

    break;
    default:
      SCOPE_DBG_PRINT("ERROR: Invalid state (state = default).");
      bytesInResultBuffer_ = 0;
      scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
      // Wait for new trigg
      setWaitForNextTrigg();
      return;

    return;
    break;
  }
  
  // Read source data to last scan buffer (only one "old" scan seems to be needed)
  if( sourceDataItem_->read((uint8_t*)&lastScanSourceDataBuffer_[0],sourceDataItemInfo_->dataSize)){
    SCOPE_DBG_PRINT("ERROR: Failed read data source..\n");
    throw std::runtime_error( "ERROR: Failed source data." );
  }

}

/** Calculate depending on bits (32 or 64 bit dc)
 *  If one is 32 bit then only compare lower 32 bits
 * sourceDataNexttimeItemInfo_ is always considered to happen in the future (after trigg)
 * If 32 bit registers then it can max be 2^32 ns between trigg and nexttime (approx 4s).
*/
int64_t ecmcScope::timeDiff() {  
  // retrun time from trigg to next
  if(sourceTriggItemInfo_->dataBitCount<64 || sourceDataNexttimeItemInfo_->dataBitCount<64) {
    // use only 32bit dc info
    uint32_t trigg = getUint32((uint8_t*)&triggTime_);
    uint32_t next  = getUint32((uint8_t*)&sourceNexttime_);
    int64_t retVal = 0;
    // Overflow... always report shortest timediff
    if (std::abs((int64_t)next)-((int64_t)trigg) > (ECMC_MAX_32BIT / 2)) {      
      if(next > trigg) {        
        printf("Overflow 1!\n");
        retVal = -(((int64_t)trigg) + ECMC_MAX_32BIT - ((int64_t)next));        
      } 
      else {
        printf("Overflow 2!\n");
        retVal = ((int64_t)next) + ECMC_MAX_32BIT - ((int64_t)trigg);
      }
      return retVal;
    }
    return ((int64_t)next)-((int64_t)trigg);
  }

  // Both are 64 bit dc timestamps
  return sourceNexttime_ - triggTime_;
}

void ecmcScope::printEcDataArray(uint8_t*       data, 
                                 size_t         size,
                                 ecmcEcDataType dt,
                                 int objId) {
  printf("INFO: Scope id: %d, data: ",objId);

  size_t dataElementSize = getEcDataTypeByteSize(dt);

  uint8_t *pData = data;
  for(unsigned int i = 0; i < size / dataElementSize; ++i) {    
    if(i % 10 == 0) {
      printf("\n");
    } else {
      printf(", ");
    }
    switch(dt) {
      case ECMC_EC_U8:        
        printf("%hhu",getUint8(pData));
        break;
      case ECMC_EC_S8:
        printf("%hhd",getInt8(pData));
        break;
      case ECMC_EC_U16:
        printf("%hu",getUint16(pData));
        break;
      case ECMC_EC_S16:
        printf("%hd",getInt16(pData));
        break;
      case ECMC_EC_U32:
        printf("%u",getUint32(pData));
        break;
      case ECMC_EC_S32:
        printf("%d",getInt32(pData));
        break;
      case ECMC_EC_U64:
        printf("%" PRIu64 "",getInt64(pData));
        break;
      case ECMC_EC_S64:
        printf("%" PRId64 "",getInt64(pData));
        break;
      case ECMC_EC_F32:
        printf("%f",getFloat32(pData));
        break;
      case ECMC_EC_F64:
        printf("%lf",getFloat64(pData));
        break;
      default:
        break;
    }

    pData += dataElementSize;
  }
  printf("\n");
}

uint8_t   ecmcScope::getUint8(uint8_t* data) {
  return *data;
}

int8_t    ecmcScope::getInt8(uint8_t* data) {
  int8_t* p=(int8_t*)data;
  return *p;
}

uint16_t  ecmcScope::getUint16(uint8_t* data) {
  uint16_t* p=(uint16_t*)data;
  return *p;
}

int16_t   ecmcScope::getInt16(uint8_t* data) {
  int16_t* p=(int16_t*)data;
  return *p;
}

uint32_t  ecmcScope::getUint32(uint8_t* data) {
  uint32_t* p=(uint32_t*)data;
  return *p;
}

int32_t   ecmcScope::getInt32(uint8_t* data) {
  int32_t* p=(int32_t*)data;
  return *p;
}

uint64_t  ecmcScope::getUint64(uint8_t* data) {
  uint64_t* p=(uint64_t*)data;
  return *p;
}

int64_t   ecmcScope::getInt64(uint8_t* data) {
  int64_t* p=(int64_t*)data;
  return *p;
}

float     ecmcScope::getFloat32(uint8_t* data) {
  float* p=(float*)data;
  return *p;
}

double    ecmcScope::getFloat64(uint8_t* data) {
  double* p=(double*)data;
  return *p;
}

size_t ecmcScope::getEcDataTypeByteSize(ecmcEcDataType dt){
  switch(dt) {
  case ECMC_EC_NONE:
    return 0;
    break;

  case ECMC_EC_B1:
    return 1;
    break;

  case ECMC_EC_B2:
    return 1;
    break;

  case ECMC_EC_B3:
    return 1;
    break;

  case ECMC_EC_B4:
    return 1;
    break;

  case ECMC_EC_U8:
    return 1;
    break;

  case ECMC_EC_S8:
    return 1;
    break;

  case ECMC_EC_U16:
    return 2;
    break;

  case ECMC_EC_S16:
    return 2;
    break;

  case ECMC_EC_U32:
    return 4;
    break;

  case ECMC_EC_S32:
    return 4;
    break;

  case ECMC_EC_U64:
    return 8;
    break;

  case ECMC_EC_S64:
    return 8;
    break;

  case ECMC_EC_F32:
    return 4;
    break;

  case ECMC_EC_F64:
    return 8;
    break;

  default:
    return 0;
    break;
  }

  return 0;
}

void ecmcScope::initAsyn() {

   ecmcAsynPortDriver *ecmcAsynPort = (ecmcAsynPortDriver *)getEcmcAsynPortDriver();
   if(!ecmcAsynPort) {
     SCOPE_DBG_PRINT("ERROR: ecmcAsynPort NULL.");
     throw std::runtime_error( "ERROR: ecmcAsynPort NULL." );
   }

  // Add resultdata "plugin.scope%d.resultdata"
  std::string paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_RESULTDATA;
  asynParamType asynType = getResultAsynDTFromEcDT(sourceDataItemInfo_->dataType);

  if(asynType == asynParamNotDefined) {
    SCOPE_DBG_PRINT("ERROR: ecmc data type not supported for param.");
    throw std::runtime_error( "ERROR: ecmc data type not supported for param: " + paramName);
  }

  resultParam_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynType,              // asyn type 
                                          resultDataBuffer_,     // pointer to data
                                          resultDataBufferBytes_,// size of data
                                          sourceDataItemInfo_->dataType, // ecmc data type
                                          0);                    // die if fail

  if(!resultParam_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for result.");
    throw std::runtime_error( "ERROR: Failed create asyn param for result: " + paramName);
  }

  resultParam_->setAllowWriteToEcmc(false);  // read only
  resultParam_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  

  // Add enable "plugin.scope%d.enable"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_ENABLE;

  enbaleParam_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynParamInt32,        // asyn type 
                                          (uint8_t*)&cfgEnable_, // pointer to data
                                          sizeof(cfgEnable_),    // size of data
                                          ECMC_EC_S32,           // ecmc data type
                                          0);                    // die if fail

  if(!enbaleParam_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for enable.");
    throw std::runtime_error( "ERROR: Failed create asyn param for enable: " + paramName);
  }

  enbaleParam_->setAllowWriteToEcmc(true);
  enbaleParam_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  

  // Add missed triggers "plugin.scope%d.missed"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_MISSED;

  asynMissedTriggs_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynParamInt32,        // asyn type 
                                          (uint8_t*)&missedTriggs_, // pointer to data
                                          sizeof(missedTriggs_),    // size of data
                                          ECMC_EC_S32,           // ecmc data type
                                          0);                    // die if fail

  if(!asynMissedTriggs_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for missed trigg counter.");   
    throw std::runtime_error( "ERROR: Failed create asyn param for missed trigg counter: " + paramName);
  }

  asynMissedTriggs_->setAllowWriteToEcmc(false);
  asynMissedTriggs_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  

  // Add trigger counter "plugin.scope%d.count"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_TRIGG_COUNT;

  asynTriggerCounter_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynParamInt32,        // asyn type 
                                          (uint8_t*)&triggerCounter_, // pointer to data
                                          sizeof(triggerCounter_),    // size of data
                                          ECMC_EC_S32,           // ecmc data type
                                          0);                    // die if fail

  if(!asynTriggerCounter_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for trigg counter.");   
    throw std::runtime_error( "ERROR: Failed create asyn param for trigg counter: " + paramName);
  }

  asynTriggerCounter_->setAllowWriteToEcmc(false);
  asynTriggerCounter_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  

  // Add enable "plugin.scope%d.source"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_SCOPE_SOURCE;

  sourceStrParam_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynParamInt8Array,    // asyn type 
                                          (uint8_t*)cfgDataSourceStr_,// pointer to data
                                          strlen(cfgDataSourceStr_),  // size of data
                                          ECMC_EC_U8,            // ecmc data type
                                          0);                    // die if fail

  if(!sourceStrParam_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for data source.");   
    throw std::runtime_error( "ERROR: Failed create asyn param for data source: " + paramName);
  }

  sourceStrParam_->setAllowWriteToEcmc(false);  // read only
  sourceStrParam_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  

  // Add enable "plugin.scope%d.trigg"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_SCOPE_TRIGG;

  triggStrParam_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynParamInt8Array,    // asyn type 
                                          (uint8_t*)cfgTriggStr_,// pointer to data
                                          strlen(cfgTriggStr_),  // size of data
                                          ECMC_EC_U8,            // ecmc data type
                                          0);                    // die if fail

  if(!triggStrParam_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for trigger.");       
    throw std::runtime_error( "ERROR: Failed create asyn param for trigger: " + paramName);
  }

  triggStrParam_->setAllowWriteToEcmc(false);  // read only
  triggStrParam_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  

  // Add enable "plugin.scope%d.nexttime"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_SCOPE_NEXT_SYNC;

  sourceNexttimeStrParam_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),     // name
                                          asynParamInt8Array,    // asyn type 
                                          (uint8_t*)cfgDataNexttimeStr_,// pointer to data
                                          strlen(cfgDataNexttimeStr_),  // size of data
                                          ECMC_EC_U8,            // ecmc data type
                                          0);                    // die if fail

  if(!sourceNexttimeStrParam_) {
    SCOPE_DBG_PRINT("ERROR: Failed create asyn param for nexttime.");       
    throw std::runtime_error( "ERROR: Failed create asyn param for nexttime: " + paramName);
  }

  sourceNexttimeStrParam_->setAllowWriteToEcmc(false);  // read only
  sourceNexttimeStrParam_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);

}

asynParamType ecmcScope::getResultAsynDTFromEcDT(ecmcEcDataType ecDT) {
  
/*typedef enum {
    asynParamNotDefined,
    asynParamInt32,
    asynParamUInt32Digital,
    asynParamFloat64,
    asynParamOctet,
    asynParamInt8Array,
    asynParamInt16Array,
    asynParamInt32Array,
    asynParamFloat32Array,
    asynParamFloat64Array,
    asynParamGenericPointer
} asynParamType;*/

  switch(ecDT) {
    case ECMC_EC_NONE:
    return asynParamNotDefined;
    break;
    case ECMC_EC_B1 :
    return asynParamNotDefined;
    break;
    case ECMC_EC_B2 :
    return asynParamNotDefined;
    break;
    case ECMC_EC_B3 :
    return asynParamNotDefined;
    break;
    case ECMC_EC_B4 :
    return asynParamNotDefined;
    break;
    case ECMC_EC_U8 :
    return asynParamInt8Array;    
    break;
    case ECMC_EC_S8 :
    return asynParamInt8Array;
    break;
    case ECMC_EC_U16:
    return asynParamInt16Array;
    break;
    case ECMC_EC_S16:
    return asynParamInt16Array;
    break;
    case ECMC_EC_U32:
    return asynParamInt32Array;
    break;
    case ECMC_EC_S32:
    return asynParamInt32Array;
    break;
    case ECMC_EC_U64:
    return asynParamNotDefined;
    break;
    case ECMC_EC_S64:
    return asynParamNotDefined;
    break;
    case ECMC_EC_F32:
    return asynParamFloat32Array;
    break;
    case ECMC_EC_F64:
    return asynParamFloat64Array;
    break;
    default:
    return asynParamNotDefined;
    break;
  }
  return asynParamNotDefined;
}

// Avoid issues with std:to_string()
std::string ecmcScope::to_string(int value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

void ecmcScope::setEnable(int enable) {
  if(enable) {
    SCOPE_DBG_PRINT("INFO: Scope enabled.\n");
  }
  else {
    SCOPE_DBG_PRINT("INFO: Scope disabled.\n");
  }

  cfgEnable_ = enable;
  enbaleParam_->refreshParam(1);
}
  
void ecmcScope::triggScope() {
  clearBuffers();
  triggOnce_ = 1;
}

void ecmcScope::setWaitForNextTrigg() {
  oldTriggTime_ = triggTime_; 
}