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

#define ECMC_PLUGIN_ASYN_PREFIX       "plugin.scope"
#define ECMC_PLUGIN_ASYN_ENABLE       "enable"
#define ECMC_PLUGIN_ASYN_RESULTDATA   "resultdata"
#define ECMC_PLUGIN_ASYN_SCOPE_SOURCE "source"
#define ECMC_PLUGIN_ASYN_SCOPE_TRIGG  "trigg"
#define ECMC_PLUGIN_ASYN_RESULT_ELEMENTS "resultelements"


#define SCOPE_DBG_PRINT(str)  \
if(cfgDbgMode_) { \
    printf(str);  \
}                 \


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
  cfgDataSourceStr_   = NULL;
  cfgDataNexttimeStr_ = NULL;
  cfgTriggStr_        = NULL;
  resultDataBuffer_   = NULL;  
  sourceDataBuffer_   = NULL;
  objectId_           = scopeIndex;  
  triggOnce_          = 0;
  dataSourceLinked_   = 0;
  resultDataBufferBytes_ = 0;
  bytesInResultBuffer_ = 0;
  oldTriggTime_       = 0;
  triggTime_          = 0;
  sourceNexttime_     = 0;
  sourceSampleRateNS_ = 0;
  sourceElementsPerSample_ = 0;
  scopeState_         = ECMC_SCOPE_STATE_INVALID;
  ecmcSmapleTimeNS_   = (uint64_t)getEcmcSampleTimeMS()*1E6;
  
  // Asyn
  sourceParam_            = NULL;

  // ecmcDataItems
  sourceDataItem_         = NULL;
  sourceDataNexttimeItem_ = NULL;
  sourceTriggItem_        = NULL;

  sourceDataItemInfo_     = NULL;
  sourceDataNexttimeItemInfo_ = NULL;
  sourceTriggItemInfo_    = NULL;
  
  // Config defaults
  cfgDbgMode_       = 0;
  cfgBufferElementCount_    = ECMC_PLUGIN_DEFAULT_BUFFER_SIZE;
  cfgEnable_        = 1;   // start enabled (enable over asyn)
  
  parseConfigStr(configStr); // Assigns all configs
  
  // Check valid buffer size
  if(cfgBufferElementCount_ <= 0) {
    throw std::out_of_range("Buffer Size must be > 0.");
  }

  // Allocate buffer use element size of 8 bytes to cover all cases
  resultDataBuffer_      = NULL;
  resultDataBufferBytes_ = 0;
  //initAsyn();
}

ecmcScope::~ecmcScope() {
  
  if(resultDataBuffer_) {
    delete[] resultDataBuffer_;
  }

  if(sourceDataBuffer_) {
    delete[] sourceDataBuffer_;
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
  SCOPE_DBG_PRINT("ecmcScope::parseConfigStr()!!!");
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


      // ECMC_PLUGIN_MODE_OPTION_CMD CONT/TRIGG
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
    throw std::invalid_argument( "Data source not defined.");
  }
  if(!cfgTriggStr_) { 
    throw std::invalid_argument( "Trigger not defined.");
  }
  if(!cfgDataNexttimeStr_) { 
    throw std::invalid_argument( "Source Nexttime not defined.");
  }
}

void ecmcScope::connectToDataSources() {
  SCOPE_DBG_PRINT("ecmcScope::connectToDataSources()!!!");
  /* Check if already linked (one call to enterRT per loaded Scope lib (Scope object))
      But link should only happen once!!*/
  if( dataSourceLinked_ ) {
    return;
  }

  // Get source dataItem
  sourceDataItem_        = (ecmcDataItem*) getEcmcDataItem(cfgDataSourceStr_);
  if(!sourceDataItem_) {
    throw std::runtime_error( "Source dataitem NULL." );
  }
  sourceDataItemInfo_ = sourceDataItem_->getDataItemInfo();

  if(!sourceDataItemInfo_) {
    throw std::runtime_error( "Source dataitem info NULL." );
  }

  // Allocate buffer for result
  resultDataBuffer_      = new uint8_t[cfgBufferElementCount_ * sourceDataItemInfo_->dataElementSize];
  memset(&resultDataBuffer_[0],0,cfgBufferElementCount_ * sourceDataItemInfo_->dataElementSize);
  resultDataBufferBytes_ = cfgBufferElementCount_ * sourceDataItemInfo_->dataElementSize;
  // Allow to cycles of storage/fifo
  sourceDataBuffer_      = new uint8_t[sourceDataItemInfo_->dataSize*2];
  memset(&sourceDataBuffer_[0],0,sourceDataItemInfo_->dataSize*2);
  sourceElementsPerSample_ = sourceDataItemInfo_->dataSize / sourceDataItemInfo_->dataElementSize;
  
  printf("sourceDataItemInfo_->dataSize=%zu, sourceDataItemInfo_->dataElementSize=%zu\n",sourceDataItemInfo_->dataSize,sourceDataItemInfo_->dataElementSize);
  sourceSampleRateNS_    = ecmcSmapleTimeNS_ / sourceElementsPerSample_;
  printf("sourceSampleRateNS_=%" PRIu64 "ecmcSmapleTimeNS_=%" PRIu64 "sourceElementsPerSample_ =%zu\n",sourceSampleRateNS_ ,ecmcSmapleTimeNS_ ,sourceElementsPerSample_);

  // Get source nexttime dataItem
  sourceDataNexttimeItem_        = (ecmcDataItem*) getEcmcDataItem(cfgDataNexttimeStr_);
  if(!sourceDataNexttimeItem_) {
    throw std::runtime_error( "Source nexttime dataitem NULL." );
  }
  sourceDataNexttimeItemInfo_ = sourceDataNexttimeItem_->getDataItemInfo();
  
  if(!sourceDataNexttimeItemInfo_) {
    throw std::runtime_error( "Source nexttime dataitem info NULL." );
  }

  // Get trigg dataItem
  sourceTriggItem_        = (ecmcDataItem*) getEcmcDataItem(cfgTriggStr_);
  if(!sourceTriggItem_) {
    throw std::runtime_error( "Trigg dataitem NULL." );
  }
  
  sourceTriggItemInfo_ = sourceTriggItem_->getDataItemInfo();
  if(!sourceTriggItemInfo_) {
    throw std::runtime_error( "Trigg dataitem info NULL." );
  }

  if( sourceTriggItem_->read((uint8_t*)(&oldTriggTime_),sourceTriggItemInfo_->dataElementSize)){
    throw std::runtime_error( "Failed read trigg time." );
  }
  
  // Register data callback
  // callbackHandle_ = sourceDataItem_->regDataUpdatedCallback(f_dataUpdatedCallback, this);
  // if (callbackHandle_ < 0) {
  //   throw std::runtime_error( "Failed to register data source callback.");
  // }

  if(!sourceDataTypeSupported(sourceDataItem_->getEcmcDataType())) {
    throw std::runtime_error( "Source data type not suppported.");
  }

  dataSourceLinked_ = 1;
  scopeState_         = ECMC_SCOPE_STATE_WAIT_TRIGG;
  
}

bool ecmcScope::sourceDataTypeSupported(ecmcEcDataType dt) {
  SCOPE_DBG_PRINT("ecmcScope::sourceDataTypeSupported()!!!");

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

void ecmcScope::execute() {
  size_t bytesToCp = 0;
  oldTriggTime_ = triggTime_;
  if(!cfgEnable_ || getEcmcEpicsIOCState()<15) {
    bytesInResultBuffer_ = 0;
    scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
    return;
  }

  // Read trigg data
  if( sourceTriggItem_->read((uint8_t*)&triggTime_,sourceTriggItemInfo_->dataElementSize)){
    throw std::runtime_error( "Failed read trigg time." );
  }

  //move samples forward in fifo (move last part of array first, newest value last)
  memmove(&sourceDataBuffer_[0],&sourceDataBuffer_[sourceDataItemInfo_->dataSize],sourceDataItemInfo_->dataSize);

  // Read source data
  if( sourceDataItem_->read((uint8_t*)&sourceDataBuffer_[sourceDataItemInfo_->dataSize],sourceDataItemInfo_->dataSize)){
    throw std::runtime_error( "Failed source data." );
  }


  switch(scopeState_) {
    case ECMC_SCOPE_STATE_INVALID:
      SCOPE_DBG_PRINT("ecmcScope::execute():ECMC_SCOPE_STATE_INVALID!!!");

    return;
    break;
    
    case ECMC_SCOPE_STATE_WAIT_TRIGG:

    // New trigger then collect data!!
    if(oldTriggTime_ != triggTime_ ) {
      printf("New trigger!!\n");      
      
      if( sourceDataNexttimeItem_->read((uint8_t*)&sourceNexttime_,sourceDataNexttimeItemInfo_->dataElementSize)){
        throw std::runtime_error( "Failed read next time." );
      }

      printf("sourceNexttime_=%" PRIu64 " ,sourceDataNexttimeItemInfo_->dataSize = %zu\n",sourceNexttime_,sourceDataNexttimeItemInfo_->dataSize);

      // calculate how many samples ago trigger occured     
      size_t samplesSinceLastTrigg = timeDiff() / sourceSampleRateNS_;
      
      // Copy the the samples to result buffer (buffer allows to ethecat scans with value(s))
      if(samplesSinceLastTrigg <= 0 || samplesSinceLastTrigg > sourceElementsPerSample_ * 2) {
        printf("WARNING: INVALID TRIGGER TIME..");
        return;
        //throw std::runtime_error( "Invalid trigg time (not within current sample)." );
      }

      // Copy elements in the end
      bytesToCp = samplesSinceLastTrigg * sourceDataItemInfo_->dataElementSize;
      if(resultDataBufferBytes_ < bytesToCp) {
        bytesToCp = resultDataBufferBytes_;
      }

      printf("bytesToCp = %lu\n", bytesToCp);

      memcpy( &resultDataBuffer_[0], &sourceDataBuffer_[sourceElementsPerSample_*2-samplesSinceLastTrigg], bytesToCp);
      bytesInResultBuffer_ = bytesToCp;
      
      if(bytesInResultBuffer_ < resultDataBufferBytes_) {
        // Fill more data from next scan
        scopeState_ = ECMC_SCOPE_STATE_COLLECT;
        printf("Change state to ECMC_SCOPE_STATE_COLLECT!!!\n");

      }
      else {
        bytesInResultBuffer_ = 0;
        printf("Result Buffer full! SEND OVER ASYN!!!\n");
      }

    }
    break;
    
    case ECMC_SCOPE_STATE_COLLECT:

      if (oldTriggTime_ != triggTime_) {
        printf("WARNING: LATCH DISGARDED SINCE STATE==ECMC_SCOPE_STATE_COLLECT.\n");
        oldTriggTime_ = triggTime_;
      }

      // Read source data
      //if( sourceDataItem_->read((uint8_t*)sourceDataBuffer_,sourceDataItemInfo_->dataSize)){
      //  throw std::runtime_error( "Failed read source data." );
      //}
      
      bytesToCp = sourceDataItemInfo_->dataSize;
      if(bytesToCp >  resultDataBufferBytes_ - bytesInResultBuffer_) {
        bytesToCp = resultDataBufferBytes_ - bytesInResultBuffer_;
      }

      // Newest data in end of source array which holds two sampes of ethercat data (example 2*100 Elements).
      // So, last scan of source data is in sourceDataBuffer_[sourceDataItemInfo_->dataSize..2 * sourceDataItemInfo_->dataSize]
      memcpy(&resultDataBuffer_[bytesInResultBuffer_],&sourceDataBuffer_[sourceDataItemInfo_->dataSize],bytesToCp);
      bytesInResultBuffer_ = bytesInResultBuffer_+ bytesToCp;
      
      if(bytesInResultBuffer_ == resultDataBufferBytes_) {
        bytesInResultBuffer_ = 0;
        scopeState_ = ECMC_SCOPE_STATE_WAIT_TRIGG;
        printf("Change state to ECMC_SCOPE_STATE_WAIT_TRIGG!!!\n");
        printf("Result Buffer full! SEND OVER ASYN!!!\n");

        printEcDataArray(resultDataBuffer_,resultDataBufferBytes_,sourceDataItemInfo_->dataType,objectId_);
      }
    
    break;
    default:
    return;
    break;
  }
  
}

/** Calculate depending on bits (32 or 64 bit dc)
 *  If one is 32 bit then only compare lower 32 bits
 * sourceDataNexttimeItemInfo_ is always considered to happen in the future (after trigg)
 * If 32 bit registers then it can max be 2^32 ns between trigg and nexttime (approx 5s).
*/
uint64_t ecmcScope::timeDiff() {  

  printf("trigg=%" PRIu64 ", next=%" PRIu64 "\n",triggTime_,sourceNexttime_);

  if(sourceTriggItemInfo_->dataBitCount<64 || sourceDataNexttimeItemInfo_->dataBitCount<=64) {
    // use only 32bit dc info
    uint32_t trigg = getUint32((uint8_t*)&triggTime_);
    uint32_t next  = getUint32((uint8_t*)&sourceNexttime_);
    printf("trigg=%u, next=%u\n",trigg,next);
    //if(trigg > next) {
    //  return std::numeric_limits<uint32_t>::max() - trigg + next;
   // }
   // else {
      return next-trigg;
    //}
  }

  return sourceNexttime_ - triggTime_;
}


// void ecmcScope::dataUpdatedCallback(uint8_t*       data, 
//                                     size_t         size,
//                                     ecmcEcDataType dt) {
//   printf("NEW DATA (%u bytes)!!!!!!!!!!!!!!!!!!!!!!!\n",size);

//   // No buffer or full or not enabled  
//   if(!resultDataBuffer_ || !cfgEnable_) {
//     return;
//   }

//   if(cfgDbgMode_) {
//     printEcDataArray(data, size, dt, objectId_);
//   }
  
//   size_t dataElementSize = getEcDataTypeByteSize(dt);
  
//   // uint8_t *pData = data;
//   // for(unsigned int i = 0; i < size / dataElementSize; ++i) {    
//   //   switch(dt) {
//   //     case ECMC_EC_U8:        
//   //       addDataToBuffer((double)getUint8(pData));
//   //       break;
//   //     case ECMC_EC_S8:
//   //       addDataToBuffer((double)getInt8(pData));
//   //       break;
//   //     case ECMC_EC_U16:
//   //       addDataToBuffer((double)getUint16(pData));
//   //       break;
//   //     case ECMC_EC_S16:
//   //       addDataToBuffer((double)getInt16(pData));
//   //       break;
//   //     case ECMC_EC_U32:
//   //       addDataToBuffer((double)getUint32(pData));
//   //       break;
//   //     case ECMC_EC_S32:
//   //       addDataToBuffer((double)getInt32(pData));
//   //       break;
//   //     case ECMC_EC_U64:
//   //       addDataToBuffer((double)getUint64(pData));
//   //       break;
//   //     case ECMC_EC_S64:
//   //       addDataToBuffer((double)getInt64(pData));
//   //       break;
//   //     case ECMC_EC_F32:
//   //       addDataToBuffer((double)getFloat32(pData));
//   //       break;
//   //     case ECMC_EC_F64:
//   //       addDataToBuffer((double)getFloat64(pData));
//   //       break;
//   //     default:
//   //       break;
//   //   }
    
//   //   pData += dataElementSize;
//   //}
// }

// void ecmcScope::addDataToBuffer(double data) {
//   if(resultDataBuffer_ && (elementsInBuffer_ < cfgBufferElementCount_) ) {
//     resultDataBuffer_[elementsInBuffer_] = data;    
//   }
//   elementsInBuffer_ ++;
// }

// void ecmcScope::clearBuffers() {
//   memset(resultDataBuffer_,   0, cfgBufferElementCount_ * sizeof(double));
//   elementsInBuffer_ = 0;
// }

void ecmcScope::printEcDataArray(uint8_t*       data, 
                                 size_t         size,
                                 ecmcEcDataType dt,
                                 int objId) {
  printf("Scope id: %d, data: ",objId);

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
     throw std::runtime_error( "ecmcAsynPort NULL." );
   }

   // Add resultdata "plugin.scope%d.resultdata" use doube in beginning..
  std::string paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
                          "." + ECMC_PLUGIN_ASYN_RESULTDATA;

  sourceParam_ = ecmcAsynPort->addNewAvailParam(
                                          paramName.c_str(),    // name
                                          asynParamFloat64Array,// asyn type 
                                          resultDataBuffer_,       // pointer to data
                                          resultDataBufferBytes_,  // size of data
                                          ECMC_EC_F64,          // ecmc data type
                                          0);                   // die if fail

  if(!sourceParam_) {     
     throw std::runtime_error( "Failed create asyn param for source: " + paramName);
  }

  // Support all array since do not know correct type yet..
  sourceParam_->addSupportedAsynType(asynParamInt8Array);
  sourceParam_->addSupportedAsynType(asynParamInt16Array);
  sourceParam_->addSupportedAsynType(asynParamInt32Array);
  sourceParam_->addSupportedAsynType(asynParamFloat32Array);
  sourceParam_->addSupportedAsynType(asynParamFloat64Array);
  sourceParam_->setAllowWriteToEcmc(false);  // read only
  sourceParam_->refreshParam(1); // read once into asyn param lib
  ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);  
}

// Avoid issues with std:to_string()
std::string ecmcScope::to_string(int value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

void ecmcScope::setEnable(int enable) {
  cfgEnable_ = enable;
}
  
void ecmcScope::triggScope() {
  clearBuffers();
  triggOnce_ = 1;
}
