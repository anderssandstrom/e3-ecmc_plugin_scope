/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcScope.h
*
*  Created on: Mar 22, 2020
*      Author: anderssandstrom
*
\*************************************************************************/
#ifndef ECMC_SCOPE_H_
#define ECMC_SCOPE_H_

#include <stdexcept>
#include "ecmcDataItem.h"
#include "ecmcScopeDefs.h"
#include "inttypes.h"
#include <string>

class ecmcScope {
 public:

  /** ecmc Scope class
   * This object can throw: 
   *    - bad_alloc
   *    - invalid_argument
   *    - runtime_error
   *    - out_of_range
  */
  ecmcScope(int   scopeIndex,    // index of this object  
            char* configStr            );
  ~ecmcScope();  

  // Add data to buffer (called from "external" callback)
  void                  dataUpdatedCallback(uint8_t* data, 
                                            size_t size,
                                            ecmcEcDataType dt);
  // Call just before realtime because then all data sources should be available
  void                  connectToDataSource();
  void                  setEnable(int enable);
  void                  clearBuffers();
  void                  triggScope();

 private:
  void                  parseConfigStr(char *configStr);
  void                  addDataToBuffer(double data);
  void                  initAsyn();
  
  ecmcDataItem         *dataItem_;
  ecmcDataItemInfo     *dataItemInfo_;

  double*               rawDataBuffer_;      // Input data (real)
  double*               prepProcDataBuffer_; // Preprocessed data (real)
  size_t                elementsInBuffer_;
  double                ecmcSampleRateHz_;
  int                   dataSourceLinked_;   // To avoid link several times
  // ecmc callback handle for use when deregister at unload
  int                   callbackHandle_;
  int                   destructs_;
  int                   objectId_;           // Unique object id
  int                   triggOnce_;
  
  // Config options
  char*                 cfgDataSourceStr_;   // Config: data source string
  int                   cfgDbgMode_;         // Config: allow dbg printouts
  size_t                cfgBufferSize_;      // Config: Data set size
  int                   cfgEnable_;          // Config: Enable data acq./calc.

  // Asyn
  int                   asynEnableId_;       // Enable/disable acq./calcs
  int                   asynRawDataId_;      // Raw data (input) array (double)
  int                   asynSourceId_;       // SOURCE
  int                   asynTriggId_;        // Trigg new measurement
  int                   asynBufferSizeId_;         // NFFT
    
  // Some generic utility functions
  static uint8_t        getUint8(uint8_t* data);
  static int8_t         getInt8(uint8_t* data);
  static uint16_t       getUint16(uint8_t* data);
  static int16_t        getInt16(uint8_t* data);
  static uint32_t       getUint32(uint8_t* data);
  static int32_t        getInt32(uint8_t* data);
  static uint64_t       getUint64(uint8_t* data);
  static int64_t        getInt64(uint8_t* data);
  static float          getFloat32(uint8_t* data);
  static double         getFloat64(uint8_t* data);
  static size_t         getEcDataTypeByteSize(ecmcEcDataType dt);
  static void           printEcDataArray(uint8_t*       data, 
                                         size_t         size,
                                         ecmcEcDataType dt,
                                         int objId);
  static std::string    to_string(int value);
};

#endif  /* ECMC_SCOPE_H_ */
