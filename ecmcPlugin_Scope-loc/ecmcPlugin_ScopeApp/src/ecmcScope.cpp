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
#define ECMC_PLUGIN_ASYN_RAWDATA      "rawdata"
#define ECMC_PLUGIN_ASYN_SCOPE_SOURCE "source"
#define ECMC_PLUGIN_ASYN_SCOPE_TRIGG  "trigg"
#define ECMC_PLUGIN_ASYN_BUFFER_SIZE  "buffersize"

#include <sstream>
#include "ecmcScope.h"
#include "ecmcPluginClient.h"
#include "ecmcAsynPortDriver.h"


// New data callback from ecmc
static int printMissingObjError = 1;

/** This callback will not be used (sample data inteface is used instead to get an stable sample freq)
  since the callback is called when data is updated it might */
void f_dataUpdatedCallback(uint8_t* data, size_t size, ecmcEcDataType dt, void* obj) {
  if(!obj) {
    if(printMissingObjError){
      printf("%s/%s:%d: Error: Callback object NULL.. Data will not be added to buffer.\n",
              __FILE__, __FUNCTION__, __LINE__);
      printMissingObjError = 0;
      return;
    }
  }
  ecmcScope * scopeObj = (ecmcScope*)obj;

  // Call the correct scope object with new data
  scopeObj->dataUpdatedCallback(data,size,dt);
}

/** ecmc Scope class
 * This object can throw: 
 *    - bad_alloc
 *    - invalid_argument
 *    - runtime_error
*/
ecmcScope::ecmcScope(int   scopeIndex,       // index of this object (if several is created)
                     char* configStr)                  
                   {
  cfgDataSourceStr_ = NULL;
  rawDataBuffer_    = NULL;
  elementsInBuffer_ = 0;  
  callbackHandle_   = -1;
  objectId_         = scopeIndex;  
  triggOnce_        = 0;
  dataSourceLinked_ = 0;

  // Asyn
  asynEnableId_     = -1;    // Enable/disable acq./calcs
  asynRawDataId_    = -1;    // Raw data (input) array (double)
  asynSourceId_     = -1;    // SOURCE
  asynTriggId_      = -1;    // Trigg new measurement

  ecmcSampleRateHz_ = getEcmcSampleRate();  

  // Config defaults
  cfgDbgMode_       = 0;
  cfgBufferSize_    = ECMC_PLUGIN_DEFAULT_BUFFER_SIZE;
  cfgEnable_        = 0;   // start disabled (enable over asyn)
  
  parseConfigStr(configStr); // Assigns all configs
  
  // Check valid buffer size
  if(cfgBufferSize_ <= 0) {
    throw std::out_of_range("Buffer Size must be > 0.");
  }

      // Allocate buffers
  rawDataBuffer_      = new double[cfgBufferSize_];               // Raw input data (real)
    
  clearBuffers();

  initAsyn();
}

ecmcScope::~ecmcScope() {
  
  if(rawDataBuffer_) {
    delete[] rawDataBuffer_;
  }

  // De register callback when unload
  if(callbackHandle_ >= 0) {
    dataItem_->deregDataUpdatedCallback(callbackHandle_);
  }

  if(cfgDataSourceStr_) {
    free(cfgDataSourceStr_);
  }
}

void ecmcScope::parseConfigStr(char *configStr) {

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

      // ECMC_PLUGIN_BUFFER_SIZE_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_BUFFER_SIZE_OPTION_CMD, strlen(ECMC_PLUGIN_BUFFER_SIZE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_BUFFER_SIZE_OPTION_CMD);
        cfgBufferSize_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_ENABLE_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_ENABLE_OPTION_CMD, strlen(ECMC_PLUGIN_ENABLE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_ENABLE_OPTION_CMD);
        cfgEnable_ = atoi(pThisOption);
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
}

void ecmcScope::connectToDataSource() {
  /* Check if already linked (one call to enterRT per loaded Scope lib (Scope object))
      But link should only happen once!!*/
  if( dataSourceLinked_ ) {
    return;
  }

  // Get dataItem
  dataItem_        = (ecmcDataItem*) getEcmcDataItem(cfgDataSourceStr_);
  if(!dataItem_) {
    throw std::runtime_error( "Data item NULL." );
  }
  
  dataItemInfo_ = dataItem_->getDataItemInfo();

  // Register data callback
  callbackHandle_ = dataItem_->regDataUpdatedCallback(f_dataUpdatedCallback, this);
  if (callbackHandle_ < 0) {
    throw std::runtime_error( "Failed to register data source callback.");
  }

  // Check data source
//   if( !dataTypeSupported(dataItem_->getEcmcDataType()) ) {
//     throw std::invalid_argument( "Data type not supported." );
//   }
  dataSourceLinked_ = 1;
}

void ecmcScope::dataUpdatedCallback(uint8_t*       data, 
                                  size_t         size,
                                  ecmcEcDataType dt) {
  
  // No buffer or full or not enabled
  if(!rawDataBuffer_ || !cfgEnable_) {
    return;
  }

  if(cfgDbgMode_) {
    printEcDataArray(data, size, dt, objectId_);

    if(elementsInBuffer_ == cfgBufferSize_) {
      printf("Buffer full (%zu elements appended).\n",elementsInBuffer_);
    }
  }
  
  size_t dataElementSize = getEcDataTypeByteSize(dt);

  uint8_t *pData = data;
  for(unsigned int i = 0; i < size / dataElementSize; ++i) {    
    switch(dt) {
      case ECMC_EC_U8:        
        addDataToBuffer((double)getUint8(pData));
        break;
      case ECMC_EC_S8:
        addDataToBuffer((double)getInt8(pData));
        break;
      case ECMC_EC_U16:
        addDataToBuffer((double)getUint16(pData));
        break;
      case ECMC_EC_S16:
        addDataToBuffer((double)getInt16(pData));
        break;
      case ECMC_EC_U32:
        addDataToBuffer((double)getUint32(pData));
        break;
      case ECMC_EC_S32:
        addDataToBuffer((double)getInt32(pData));
        break;
      case ECMC_EC_U64:
        addDataToBuffer((double)getUint64(pData));
        break;
      case ECMC_EC_S64:
        addDataToBuffer((double)getInt64(pData));
        break;
      case ECMC_EC_F32:
        addDataToBuffer((double)getFloat32(pData));
        break;
      case ECMC_EC_F64:
        addDataToBuffer((double)getFloat64(pData));
        break;
      default:
        break;
    }
    
    pData += dataElementSize;
  }
}

void ecmcScope::addDataToBuffer(double data) {
  if(rawDataBuffer_ && (elementsInBuffer_ < cfgBufferSize_) ) {
    rawDataBuffer_[elementsInBuffer_] = data;    
  }
  elementsInBuffer_ ++;
}

void ecmcScope::clearBuffers() {
  memset(rawDataBuffer_,   0, cfgBufferSize_ * sizeof(double));
  elementsInBuffer_ = 0;
}

void ecmcScope::printEcDataArray(uint8_t*       data, 
                               size_t         size,
                               ecmcEcDataType dt,
                               int objId) {
  printf("Scope id: %d, data: ",objId);

  size_t dataElementSize = getEcDataTypeByteSize(dt);

  uint8_t *pData = data;
  for(unsigned int i = 0; i < size / dataElementSize; ++i) {    
    switch(dt) {
      case ECMC_EC_U8:        
        printf("%hhu\n",getUint8(pData));
        break;
      case ECMC_EC_S8:
        printf("%hhd\n",getInt8(pData));
        break;
      case ECMC_EC_U16:
        printf("%hu\n",getUint16(pData));
        break;
      case ECMC_EC_S16:
        printf("%hd\n",getInt16(pData));
        break;
      case ECMC_EC_U32:
        printf("%u\n",getUint32(pData));
        break;
      case ECMC_EC_S32:
        printf("%d\n",getInt32(pData));
        break;
      case ECMC_EC_U64:
        printf("%" PRIu64 "\n",getInt64(pData));
        break;
      case ECMC_EC_S64:
        printf("%" PRId64 "\n",getInt64(pData));
        break;
      case ECMC_EC_F32:
        printf("%f\n",getFloat32(pData));
        break;
      case ECMC_EC_F64:
        printf("%lf\n",getFloat64(pData));
        break;
      default:
        break;
    }
    
    pData += dataElementSize;
  }
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

//   ecmcAsynPortDriver *ecmcAsynPort = (ecmcAsynPortDriver *)getEcmcAsynPortDriver();
//   if(!ecmcAsynPort) {
//     printf("Error: ecmcPlugin_Advanced: ecmcAsynPortDriver NULL.");
//     return ECMC_ERROR_ASYNPORT_NULL;
//   }
//   // Add a dummy counter that incraeses one for each rt cycle
//   paramCount = ecmcAsynPort->addNewAvailParam(
//                                         "plugin.adv.counter",  // name
//                                          asynParamInt32,       // asyn type 
//                                          (uint8_t *)&(counter),// pointer to data
//                                          sizeof(counter),      // size of data
//                                          ECMC_EC_S32,          // ecmc data type
//                                          0);                   // die if fail
//   if(!paramCount) {
//     printf("Error: ecmcPlugin_Advanced: Failed to create asyn param \"plugin.adv.counter\".");
//     return ECMC_ERROR_ASYN_PARAM_FAIL;
//   }
//   paramCount->addSupportedAsynType(asynParamInt32);  // Only allow records of this type 
//   paramCount->setAllowWriteToEcmc(false);  // read only
//   paramCount->refreshParam(1); // read once into asyn param lib
//   ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);
//   return 0;
}

// Avoid issues with std:to_string()
std::string ecmcScope::to_string(int value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

void ecmcScope::setEnable(int enable) {
  cfgEnable_ = enable;
  //setIntegerParam(asynEnableId_, enable);
}
  
void ecmcScope::triggScope() {
  clearBuffers();
  triggOnce_ = 1;
  //setIntegerParam(asynTriggId_,0);
}
