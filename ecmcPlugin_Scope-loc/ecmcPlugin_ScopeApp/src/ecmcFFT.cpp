/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcFFT.cpp
*
*  Created on: Mar 22, 2020
*      Author: anderssandstrom
*      Credits to  https://github.com/sgreg/dynamic-loading 
*
\*************************************************************************/

// Needed to get headers in ecmc right...
#define ECMC_IS_PLUGIN

#define ECMC_PLUGIN_ASYN_PREFIX      "plugin.fft"
#define ECMC_PLUGIN_ASYN_ENABLE      "enable"
#define ECMC_PLUGIN_ASYN_RAWDATA     "rawdata"
#define ECMC_PLUGIN_ASYN_PPDATA      "preprocdata"
#define ECMC_PLUGIN_ASYN_FFT_AMP     "fftamplitude"
#define ECMC_PLUGIN_ASYN_FFT_MODE    "mode"
#define ECMC_PLUGIN_ASYN_FFT_STAT    "status"
#define ECMC_PLUGIN_ASYN_FFT_SOURCE  "source"
#define ECMC_PLUGIN_ASYN_FFT_TRIGG   "trigg"
#define ECMC_PLUGIN_ASYN_FFT_X_FREQS "fftxaxis"
#define ECMC_PLUGIN_ASYN_NFFT        "nfft"
#define ECMC_PLUGIN_ASYN_RATE        "samplerate"


#include <sstream>
#include "ecmcFFT.h"
#include "ecmcPluginClient.h"
#include "ecmcAsynPortDriver.h"
#include "epicsThread.h"


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
  ecmcFFT * fftObj = (ecmcFFT*)obj;

  // Call the correct fft object with new data
  fftObj->dataUpdatedCallback(data,size,dt);
}

void f_worker(void *obj) {
  if(!obj) {
    printf("%s/%s:%d: Error: Worker thread FFT object NULL..\n",
            __FILE__, __FUNCTION__, __LINE__);
    return;
  }
  ecmcFFT * fftObj = (ecmcFFT*)obj;
  fftObj->doCalcWorker();
}

/** ecmc FFT class
 * This object can throw: 
 *    - bad_alloc
 *    - invalid_argument
 *    - runtime_error
*/
ecmcFFT::ecmcFFT(int   fftIndex,       // index of this object (if several is created)
                 char* configStr,
                 char* portName) 
                  : asynPortDriver(portName,
                   1, /* maxAddr */
                   asynInt32Mask | asynFloat64Mask | asynFloat32ArrayMask |
                   asynFloat64ArrayMask | asynEnumMask | asynDrvUserMask |
                   asynOctetMask | asynInt8ArrayMask | asynInt16ArrayMask |
                   asynInt32ArrayMask | asynUInt32DigitalMask, /* Interface mask */
                   asynInt32Mask | asynFloat64Mask | asynFloat32ArrayMask |
                   asynFloat64ArrayMask | asynEnumMask | asynDrvUserMask |
                   asynOctetMask | asynInt8ArrayMask | asynInt16ArrayMask |
                   asynInt32ArrayMask | asynUInt32DigitalMask, /* Interrupt mask */
                   ASYN_CANBLOCK , /*NOT ASYN_MULTI_DEVICE*/
                   1, /* Autoconnect */
                   0, /* Default priority */
                   0) /* Default stack size */
                   {
  cfgDataSourceStr_ = NULL;
  rawDataBuffer_    = NULL;
  dataItem_         = NULL;
  dataItemInfo_     = NULL;
  fftDouble_        = NULL;
  status_           = NO_STAT;
  elementsInBuffer_ = 0;
  fftWaitingForCalc_= 0;
  destructs_        = 0;
  callbackHandle_   = -1;
  objectId_         = fftIndex;
  scale_            = 1.0;
  triggOnce_        = 0;
  cycleCounter_     = 0;
  ignoreCycles_     = 0;
  dataSourceLinked_ = 0;

  // Asyn
  asynEnableId_     = -1;    // Enable/disable acq./calcs
  asynRawDataId_    = -1;    // Raw data (input) array (double)
  asynPPDataId_     = -1;    // Pre-processed data array (double)
  asynFFTAmpId_     = -1;    // FFT amplitude array (double)
  asynFFTModeId_    = -1;    // FFT mode (cont/trigg)
  asynFFTStatId_    = -1;    // FFT status (no_stat/idle/acq/calc)
  asynSourceId_     = -1;    // SOURCE
  asynTriggId_      = -1;    // Trigg new measurement
  asynFFTXAxisId_   = -1;    // FFT X-axis frequencies
  asynNfftId_       = -1;    // Nfft
  asynSRateId_      = -1;    // Sample rate Hz

  ecmcSampleRateHz_ = getEcmcSampleRate();
  cfgFFTSampleRateHz_ = ecmcSampleRateHz_;

  // Config defaults
  cfgDbgMode_       = 0;
  cfgNfft_          = ECMC_PLUGIN_DEFAULT_NFFT; // samples in fft (must be n^2)
  cfgDcRemove_      = 0;
  cfgLinRemove_     = 0;
  cfgApplyScale_    = 1;   // Scale as default to get correct amplitude in fft
  cfgEnable_        = 0;   // start disabled (enable over asyn)
  cfgMode_          = TRIGG;

  parseConfigStr(configStr); // Assigns all configs
  // Check valid nfft
  if(cfgNfft_ <= 0) {
    throw std::out_of_range("NFFT must be > 0 and even N^2.");
  }

  // Check valid sample rate
  if(cfgFFTSampleRateHz_ <= 0) {
    throw std::out_of_range("FFT Invalid sample rate"); 
  }
  if(cfgFFTSampleRateHz_ > ecmcSampleRateHz_) {
    printf("Warning FFT sample rate faster than ecmc rate. FFT rate will be set to ecmc rate.\n");
    cfgFFTSampleRateHz_ = ecmcSampleRateHz_;
  }

  // Se if any data update cycles should be ignored
  // example ecmc 1000Hz, fft 100Hz then ignore 9 cycles (could be strange if not multiples)
  ignoreCycles_ = ecmcSampleRateHz_ / cfgFFTSampleRateHz_ -1;

  // set scale factor
  scale_ = 1.0 / ((double)cfgNfft_); // sqrt((double)cfgNfft_);

  // Allocate buffers
  rawDataBuffer_      = new double[cfgNfft_];               // Raw input data (real)
  prepProcDataBuffer_ = new double[cfgNfft_];               // Data for preprocessing
  fftBufferInput_     = new std::complex<double>[cfgNfft_]; // FFT input  (complex)
  fftBufferResult_    = new std::complex<double>[cfgNfft_]; // FFT result (complex)
  fftBufferResultAmp_ = new double[cfgNfft_ / 2 + 1];       // FFT result amplitude (real)
  fftBufferXAxis_     = new double[cfgNfft_ / 2 + 1];       // FFT x axis with freqs
  clearBuffers();

  // Allocate KissFFT
  fftDouble_ = new kissfft<double>(cfgNfft_,false);
  
  // Create worker thread
  std::string threadname = "ecmc." ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_);
  if(epicsThreadCreate(threadname.c_str(), 0, 32768, f_worker, this) == NULL) {
    throw std::runtime_error("Error: Failed create worker thread.");
  }
  
  initAsyn();
}

ecmcFFT::~ecmcFFT() {
  // kill worker
  destructs_ = 1;  // maybe need todo in other way..
  doCalcEvent_.signal();

  if(rawDataBuffer_) {
    delete[] rawDataBuffer_;
  }

  if(prepProcDataBuffer_) {
    delete[] prepProcDataBuffer_;
  }
  
  // De register callback when unload
  if(callbackHandle_ >= 0) {
    dataItem_->deregDataUpdatedCallback(callbackHandle_);
  }
  if(cfgDataSourceStr_) {
    free(cfgDataSourceStr_);
  }
  if(fftDouble_) {
    delete fftDouble_;
  }
  if (fftBufferInput_){
    delete[] fftBufferInput_;
  }  
}

void ecmcFFT::parseConfigStr(char *configStr) {

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

      // ECMC_PLUGIN_NFFT_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_NFFT_OPTION_CMD, strlen(ECMC_PLUGIN_NFFT_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_NFFT_OPTION_CMD);
        cfgNfft_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_APPLY_SCALE_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_APPLY_SCALE_OPTION_CMD, strlen(ECMC_PLUGIN_APPLY_SCALE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_APPLY_SCALE_OPTION_CMD);
        cfgApplyScale_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_RM_DC_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_RM_DC_OPTION_CMD, strlen(ECMC_PLUGIN_RM_DC_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_RM_DC_OPTION_CMD);
        cfgDcRemove_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_RM_LIN_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_RM_LIN_OPTION_CMD, strlen(ECMC_PLUGIN_RM_LIN_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_RM_LIN_OPTION_CMD);
        cfgLinRemove_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_ENABLE_OPTION_CMD (1/0)
      else if (!strncmp(pThisOption, ECMC_PLUGIN_ENABLE_OPTION_CMD, strlen(ECMC_PLUGIN_ENABLE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_ENABLE_OPTION_CMD);
        cfgEnable_ = atoi(pThisOption);
      }

      // ECMC_PLUGIN_MODE_OPTION_CMD CONT/TRIGG
      else if (!strncmp(pThisOption, ECMC_PLUGIN_MODE_OPTION_CMD, strlen(ECMC_PLUGIN_MODE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_MODE_OPTION_CMD);
        if(!strncmp(pThisOption, ECMC_PLUGIN_MODE_CONT_OPTION,strlen(ECMC_PLUGIN_MODE_CONT_OPTION))){
          cfgMode_ = CONT;
        }
        if(!strncmp(pThisOption, ECMC_PLUGIN_MODE_TRIGG_OPTION,strlen(ECMC_PLUGIN_MODE_TRIGG_OPTION))){
          cfgMode_ = TRIGG;
        }
      }

      // ECMC_PLUGIN_RATE_OPTION_CMD rate in HZ
      else if (!strncmp(pThisOption, ECMC_PLUGIN_RATE_OPTION_CMD, strlen(ECMC_PLUGIN_RATE_OPTION_CMD))) {
        pThisOption += strlen(ECMC_PLUGIN_RATE_OPTION_CMD);
        cfgFFTSampleRateHz_ = atof(pThisOption);
      }

      pThisOption = pNextOption;
    }    
    free(pOptions);
  }

  // Data source must be defined...
  if(!cfgDataSourceStr_) { 
    throw std::invalid_argument( "Data source not defined.");
  }
}

void ecmcFFT::connectToDataSource() {
  /* Check if already linked (one call to enterRT per loaded FFT lib (FFT object))
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
  if( !dataTypeSupported(dataItem_->getEcmcDataType()) ) {
    throw std::invalid_argument( "Data type not supported." );
  }
  dataSourceLinked_ = 1;
  updateStatus(IDLE);
}

void ecmcFFT::dataUpdatedCallback(uint8_t*       data, 
                                  size_t         size,
                                  ecmcEcDataType dt) {
  
  if(fftWaitingForCalc_) {
    return;
  }
  // No buffer or full or not enabled
  if(!rawDataBuffer_ || !cfgEnable_) {
    return;
  }

  // See if data should be ignored
  if(cycleCounter_ < ignoreCycles_) {
    cycleCounter_++;
    return; // ignore this callback
  }

  cycleCounter_ = 0;

  if (cfgMode_ == TRIGG && !triggOnce_ ) {
    updateStatus(IDLE);
    return; // Wait for trigger from plc or asyn
  }

  if(cfgDbgMode_) {
    printEcDataArray(data, size, dt, objectId_);

    if(elementsInBuffer_ == cfgNfft_) {
      printf("Buffer full (%zu elements appended).\n",elementsInBuffer_);
    }
  }
  
  if(elementsInBuffer_ >= cfgNfft_) {
    //Buffer full
    if(!fftWaitingForCalc_){            
      // Perform calcs
      updateStatus(CALC);
      fftWaitingForCalc_ = 1;
      doCalcEvent_.signal(); // let worker start
    }
    return;
  }

  updateStatus(ACQ);

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

void ecmcFFT::addDataToBuffer(double data) {
  if(rawDataBuffer_ && (elementsInBuffer_ < cfgNfft_) ) {
    rawDataBuffer_[elementsInBuffer_] = data;
    prepProcDataBuffer_[elementsInBuffer_] = data;
  }
  elementsInBuffer_ ++;
}

void ecmcFFT::clearBuffers() {
  memset(rawDataBuffer_,   0, cfgNfft_ * sizeof(double));
  memset(prepProcDataBuffer_, 0, cfgNfft_ * sizeof(double));
  memset(fftBufferResultAmp_, 0, (cfgNfft_ / 2 + 1) * sizeof(double));
  memset(fftBufferXAxis_, 0, (cfgNfft_ / 2 + 1) * sizeof(double));  
  for(unsigned int i = 0; i < cfgNfft_; ++i) {
    fftBufferResult_[i].real(0);
    fftBufferResult_[i].imag(0);
    fftBufferInput_[i].real(0);
    fftBufferInput_[i].imag(0);
  }
  elementsInBuffer_ = 0;
}

void ecmcFFT::calcFFT() {
  // move pre-processed data to fft input buffer
  for(unsigned int i = 0; i < cfgNfft_; ++i) {
    fftBufferInput_[i].real(prepProcDataBuffer_[i]);
    fftBufferInput_[i].imag(0);
  }

  // Do fft
  fftDouble_->transform(fftBufferInput_, fftBufferResult_);
}

void ecmcFFT::scaleFFT() {
  if(!cfgApplyScale_) {
    return;
  }

  for(unsigned int i = 0 ; i < cfgNfft_ ; ++i ) {
    fftBufferResult_[i] = fftBufferResult_[i] * scale_;
  }
}

void ecmcFFT::calcFFTAmp() {  
  for(unsigned int i = 0 ; i < cfgNfft_ / 2 + 1 ; ++i ) {
    fftBufferResultAmp_[i] = std::abs(fftBufferResult_[i]);
  }
}

// Should be enough todo once
void ecmcFFT::calcFFTXAxis() {
  //fill x axis buffer with freqs
  double freq = 0;
  double deltaFreq = ecmcSampleRateHz_* ((double)dataItemInfo_->dataSize / 
                     (double)dataItemInfo_->dataElementSize) / ((double)(cfgNfft_));
  for(unsigned int i = 0; i < (cfgNfft_ / 2 + 1); ++i) {
    fftBufferXAxis_[i] = freq;
    freq = freq + deltaFreq;
  }  
}

void ecmcFFT::removeDCOffset() {
  if(!cfgDcRemove_) {
    return;
  }

  // calc average of preprocess buffer data
  double sum = 0;
  for(unsigned int i = 0; i < cfgNfft_; ++i ) {
    sum += prepProcDataBuffer_[i];
  }
  double avg = sum / ((double)cfgNfft_);
  for(unsigned int i = 0; i < cfgNfft_; ++i ) {    
    prepProcDataBuffer_[i] = (prepProcDataBuffer_[i]-avg);
  }
}

void ecmcFFT::removeLin() {
  if(!cfgLinRemove_) {
    return;
  }

  double k=0;
  double m=0;
  // calc least square (best fit of line)
  if(leastSquare(cfgNfft_,prepProcDataBuffer_,&k,&m)) {
    printf("%s/%s:%d: Error: " ECMC_PLUGIN_RM_LIN_OPTION_CMD " failed, divison by 0. Data will not be processed with the option/configuration.\n",
           __FILE__, __FUNCTION__, __LINE__);
    return;
  }
  
  // remove linear component (now we have k and m (y=k*x+m))  
  for(unsigned int x = 0; x < cfgNfft_; ++x ) {
    prepProcDataBuffer_[x] = prepProcDataBuffer_[x] - (k*x + m);
  }
}

void ecmcFFT::printEcDataArray(uint8_t*       data, 
                               size_t         size,
                               ecmcEcDataType dt,
                               int objId) {
  printf("fft id: %d, data: ",objId);

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

void ecmcFFT::printComplexArray(std::complex<double>* fftBuff,
                                size_t elements,
                                int objId) {
  printf("fft id: %d, results: \n",objId);
  for(unsigned int i = 0 ; i < elements ; ++i ) {
    printf("%d: %lf\n", i, std::abs(fftBuff[i]));
  }
}

int ecmcFFT::dataTypeSupported(ecmcEcDataType dt) {

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

uint8_t   ecmcFFT::getUint8(uint8_t* data) {
  return *data;
}

int8_t    ecmcFFT::getInt8(uint8_t* data) {
  int8_t* p=(int8_t*)data;
  return *p;
}

uint16_t  ecmcFFT::getUint16(uint8_t* data) {
  uint16_t* p=(uint16_t*)data;
  return *p;
}

int16_t   ecmcFFT::getInt16(uint8_t* data) {
  int16_t* p=(int16_t*)data;
  return *p;
}

uint32_t  ecmcFFT::getUint32(uint8_t* data) {
  uint32_t* p=(uint32_t*)data;
  return *p;
}

int32_t   ecmcFFT::getInt32(uint8_t* data) {
  int32_t* p=(int32_t*)data;
  return *p;
}

uint64_t  ecmcFFT::getUint64(uint8_t* data) {
  uint64_t* p=(uint64_t*)data;
  return *p;
}

int64_t   ecmcFFT::getInt64(uint8_t* data) {
  int64_t* p=(int64_t*)data;
  return *p;
}

float     ecmcFFT::getFloat32(uint8_t* data) {
  float* p=(float*)data;
  return *p;
}

double    ecmcFFT::getFloat64(uint8_t* data) {
  double* p=(double*)data;
  return *p;
}

size_t ecmcFFT::getEcDataTypeByteSize(ecmcEcDataType dt){
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

void ecmcFFT::initAsyn() {

  // Add enable "plugin.fft%d.enable"
  std::string paramName =ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_ENABLE;
  
  if( createParam(0, paramName.c_str(), asynParamInt32, &asynEnableId_) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter enable");
  }
  setIntegerParam(asynEnableId_, cfgEnable_);

  // Add rawdata "plugin.fft%d.rawdata"
  paramName =ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_RAWDATA;

  if( createParam(0, paramName.c_str(), asynParamFloat64Array, &asynRawDataId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter rawdata");
  }
  doCallbacksFloat64Array(rawDataBuffer_, cfgNfft_, asynRawDataId_,0);

  // Add rawdata "plugin.fft%d.preprocdata"
  paramName =ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_PPDATA;

  if( createParam(0, paramName.c_str(), asynParamFloat64Array, &asynPPDataId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter preprocdata");
  }
  doCallbacksFloat64Array(prepProcDataBuffer_, cfgNfft_, asynRawDataId_,0);



  // Add fft amplitude "plugin.fft%d.fftamplitude"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_FFT_AMP;

  if( createParam(0, paramName.c_str(), asynParamFloat64Array, &asynFFTAmpId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter fftamplitude");
  }
  doCallbacksFloat64Array(fftBufferResultAmp_, cfgNfft_/2+1, asynFFTXAxisId_,0);

  // Add fft mode "plugin.fft%d.mode"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_FFT_MODE;

  if( createParam(0, paramName.c_str(), asynParamInt32, &asynFFTModeId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter mode");
  }
  setIntegerParam(asynFFTModeId_, (epicsInt32)cfgMode_);

  // Add fft mode "plugin.fft%d.status"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_FFT_STAT;

  if( createParam(0, paramName.c_str(), asynParamInt32, &asynFFTStatId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter status");
  }
  setIntegerParam(asynFFTStatId_, (epicsInt32)status_);

  // Add fft mode "plugin.fft%d.source"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_FFT_SOURCE;

  if( createParam(0, paramName.c_str(), asynParamInt8Array, &asynSourceId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter source");
  }
  doCallbacksInt8Array(cfgDataSourceStr_, strlen(cfgDataSourceStr_), asynSourceId_,0);

  // Add fft mode "plugin.fft%d.trigg"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_FFT_TRIGG;

  if( createParam(0, paramName.c_str(), asynParamInt32, &asynTriggId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter trigg");
  }
  setIntegerParam(asynTriggId_, (epicsInt32)triggOnce_);

  // Add fft mode "plugin.fft%d.fftxaxis"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_FFT_X_FREQS;

  if( createParam(0, paramName.c_str(), asynParamFloat64Array, &asynFFTXAxisId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter xaxisfreqs");
  }
  doCallbacksFloat64Array(fftBufferXAxis_,cfgNfft_ / 2 + 1, asynFFTXAxisId_,0);

  // Add fft mode "plugin.fft%d.nfft"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_NFFT;

  if( createParam(0, paramName.c_str(), asynParamInt32, &asynNfftId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter trigg");
  }
  setIntegerParam(asynNfftId_, (epicsInt32)cfgNfft_);

  // Add fft mode "plugin.fft%d.rate"
  paramName = ECMC_PLUGIN_ASYN_PREFIX + to_string(objectId_) + 
             "." + ECMC_PLUGIN_ASYN_RATE;

  if( createParam(0, paramName.c_str(), asynParamInt32, &asynSRateId_ ) != asynSuccess ) {
    throw std::runtime_error("Failed create asyn parameter trigg");
  }
  setDoubleParam(asynSRateId_, cfgFFTSampleRateHz_);

  // Update integers
  callParamCallbacks();
}

// Avoid issues with std:to_string()
std::string ecmcFFT::to_string(int value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

void ecmcFFT::setEnable(int enable) {
  cfgEnable_ = enable;
  setIntegerParam(asynEnableId_, enable);
}
  
void ecmcFFT::triggFFT() {
  clearBuffers();
  triggOnce_ = 1;
  setIntegerParam(asynTriggId_,0);
}

void ecmcFFT::setModeFFT(FFT_MODE mode) {
  cfgMode_ = mode;
  setIntegerParam(asynFFTModeId_,(epicsInt32)mode);
}

FFT_STATUS ecmcFFT::getStatusFFT() {
  return status_;
}

void ecmcFFT::updateStatus(FFT_STATUS status) {
  status_ = status;
  setIntegerParam(asynFFTStatId_,(epicsInt32) status);
  callParamCallbacks();
}

// Called from low prio worker thread. Makes the hard work
void ecmcFFT::doCalcWorker() {

  while(true) {
    doCalcEvent_.wait();
    if(destructs_) {
      break;
    }
    // Pre-process    
    removeDCOffset();  // Remove dc on rawdata
    removeLin();       // Remove fitted line
    // Process
    calcFFT();         // FFT cacluation
    // Post-process    
    scaleFFT();        // Scale FFT
    calcFFTAmp();      // Calculate amplitude from complex
    calcFFTXAxis();    // Calculate x axis

    doCallbacksFloat64Array(rawDataBuffer_,     cfgNfft_,     asynRawDataId_, 0);
    doCallbacksFloat64Array(prepProcDataBuffer_, cfgNfft_,    asynPPDataId_,  0);
    doCallbacksFloat64Array(fftBufferResultAmp_,cfgNfft_/2+1, asynFFTAmpId_,  0);
    doCallbacksFloat64Array(fftBufferXAxis_,    cfgNfft_/2+1, asynFFTXAxisId_,0);
    callParamCallbacks();    
    if(cfgDbgMode_){
      printComplexArray(fftBufferResult_,
                        cfgNfft_,
                        objectId_);
      printEcDataArray((uint8_t*)rawDataBuffer_,
                       cfgNfft_*sizeof(double),
                       ECMC_EC_F64,
                       objectId_);    
    }
    
    clearBuffers();
    triggOnce_ = 0;    // Wait for next trigger if in trigg mode
    setIntegerParam(asynTriggId_,triggOnce_);
    fftWaitingForCalc_ = 0;
  } 
}

asynStatus ecmcFFT::writeInt32(asynUser *pasynUser, epicsInt32 value) {
  int function = pasynUser->reason;
  if( function == asynEnableId_ ) {
    cfgEnable_ = value;
    return asynSuccess;
  } else if( function == asynFFTModeId_){
    cfgMode_ = (FFT_MODE)value;
    return asynSuccess;
  } else if( function == asynTriggId_){
    triggOnce_ = value > 0;
    return asynSuccess;
  }
  return asynError;
}

asynStatus ecmcFFT::readInt32(asynUser *pasynUser, epicsInt32 *value) {
  int function = pasynUser->reason;
  if( function == asynEnableId_ ) {
    *value = cfgEnable_;
    return asynSuccess;
  } else if( function == asynFFTModeId_ ){
    *value = cfgMode_;
    return asynSuccess;
  } else if( function == asynTriggId_ ){
    *value = triggOnce_;
    return asynSuccess;
  }else if( function == asynFFTStatId_ ){
    *value = (epicsInt32)status_;
    return asynSuccess;
  }else if( function == asynNfftId_ ){
    *value = (epicsInt32)cfgNfft_;
    return asynSuccess;
  }

  return asynError;
}

asynStatus ecmcFFT::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                     size_t nElements, size_t *nIn) {
  int function = pasynUser->reason;
  if( function == asynRawDataId_ ) {
    unsigned int ncopy = cfgNfft_;
    if(nElements < ncopy) {
      ncopy = nElements;
    } 
    memcpy (value, rawDataBuffer_, ncopy);
    *nIn = ncopy;
    return asynSuccess;
  } else if( function == asynPPDataId_) {
    unsigned int ncopy = cfgNfft_;
    if(nElements < ncopy) {
      ncopy = nElements;
    } 
    memcpy (value, prepProcDataBuffer_, ncopy);
    *nIn = ncopy;
    return asynSuccess;
  } else if( function == asynFFTXAxisId_ ) {
    unsigned int ncopy = cfgNfft_/ 2 + 1;
    if(nElements < ncopy) {
      ncopy = nElements;
    } 
    memcpy (value, fftBufferXAxis_, ncopy);
    *nIn = ncopy;
    return asynSuccess;
  } if( function == asynFFTAmpId_ ) {
    unsigned int ncopy = cfgNfft_/ 2 + 1;
    if(nElements < ncopy) {
      ncopy = nElements;
    } 
    memcpy (value, fftBufferResultAmp_, ncopy);
    *nIn = ncopy;
    return asynSuccess;
  }

  *nIn = 0;
  return asynError;
}

asynStatus ecmcFFT::readInt8Array(asynUser *pasynUser, epicsInt8 *value, 
                                   size_t nElements, size_t *nIn) {
  int function = pasynUser->reason;
  if( function == asynSourceId_ ) {
    unsigned int ncopy = strlen(cfgDataSourceStr_);
    if(nElements < ncopy) {
      ncopy = nElements;
    } 
    memcpy (value, cfgDataSourceStr_, ncopy);
    *nIn = ncopy;
    return asynSuccess;
  }

  *nIn = 0;
  return asynError;
}

asynStatus  ecmcFFT::readFloat64(asynUser *pasynUser, epicsFloat64 *value) {
  int function = pasynUser->reason;
  if( function == asynSRateId_ ) {
    *value = cfgFFTSampleRateHz_;
    return asynSuccess;
  }

  return asynError;
}

/* y = k*x+m */
int ecmcFFT::leastSquare(int n, const double y[], double* k, double* m){
  double   sumx  = 0.0;
  double   sumx2 = 0.0;
  double   sumxy = 0.0;
  double   sumy  = 0.0;
  double   sumy2 = 0.0;

  for (int x = 0; x < n; ++x){ 
    //simulate x by just index
    sumx  += x;
    sumx2 += x * x;
    sumxy += x * y[x];
    sumy  += y[x];
    sumy2 += y[x] * y[x];
  } 

  double denom = (n * sumx2 - sumx * sumx);
  if (denom == 0) {
    // Cannot dive by 0.. something wrong..
    *k = 0;
    *m = 0;
    return 1; // Error
  }

  *k = (n * sumxy  -  sumx * sumy) / denom;
  *m = (sumy * sumx2  -  sumx * sumxy) / denom;
  return 0; 
}