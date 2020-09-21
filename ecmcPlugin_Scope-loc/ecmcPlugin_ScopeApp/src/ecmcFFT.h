/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcFFT.h
*
*  Created on: Mar 22, 2020
*      Author: anderssandstrom
*
\*************************************************************************/
#ifndef ECMC_FFT_H_
#define ECMC_FFT_H_

#include <stdexcept>
#include "ecmcDataItem.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcFFTDefs.h"
#include "inttypes.h"
#include <string>
#include "kissfft/kissfft.hh"

class ecmcFFT : public asynPortDriver {
 public:

  /** ecmc FFT class
   * This object can throw: 
   *    - bad_alloc
   *    - invalid_argument
   *    - runtime_error
   *    - out_of_range
  */
  ecmcFFT(int   fftIndex,    // index of this object  
          char* configStr,
          char* portName);
  ~ecmcFFT();  

  // Add data to buffer (called from "external" callback)
  void                  dataUpdatedCallback(uint8_t* data, 
                                            size_t size,
                                            ecmcEcDataType dt);
  // Call just before realtime because then all data sources should be available
  void                  connectToDataSource();
  void                  setEnable(int enable);
  void                  setModeFFT(FFT_MODE mode);
  FFT_STATUS            getStatusFFT();
  void                  clearBuffers();
  void                  triggFFT();
  void                  doCalcWorker();  // Called from worker thread calc the results
  virtual asynStatus    writeInt32(asynUser *pasynUser, epicsInt32 value);
  virtual asynStatus    readInt32(asynUser *pasynUser, epicsInt32 *value);
  virtual asynStatus    readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                         size_t nElements, size_t *nIn);
  virtual asynStatus    readInt8Array(asynUser *pasynUser, epicsInt8 *value, 
                                      size_t nElements, size_t *nIn);
  virtual asynStatus    readFloat64(asynUser *pasynUser, epicsFloat64 *value);


 private:
  void                  parseConfigStr(char *configStr);
  void                  addDataToBuffer(double data);
  void                  calcFFT();
  void                  scaleFFT();
  void                  calcFFTAmp();
  void                  calcFFTXAxis();
  void                  removeDCOffset();
  void                  removeLin();
  void                  initAsyn();
  void                  updateStatus(FFT_STATUS status);  // Also updates asynparam
  static int            dataTypeSupported(ecmcEcDataType dt);

  ecmcDataItem         *dataItem_;
  ecmcDataItemInfo     *dataItemInfo_;
  ecmcAsynPortDriver   *asynPort_;
  kissfft<double>*      fftDouble_;
  double*               rawDataBuffer_;      // Input data (real)
  double*               prepProcDataBuffer_; // Preprocessed data (real)
  std::complex<double>* fftBufferInput_;     // Result (complex)
  std::complex<double>* fftBufferResult_;    // Result (complex)
  double*               fftBufferResultAmp_; // Resulting amplitude (abs of fftBufferResult_)
  double*               fftBufferXAxis_;     // FFT x axis with freqs
  size_t                elementsInBuffer_;
  double                ecmcSampleRateHz_;
  int                   dataSourceLinked_;   // To avoid link several times
  // ecmc callback handle for use when deregister at unload
  int                   callbackHandle_;
  int                   fftWaitingForCalc_;
  int                   destructs_;
  int                   objectId_;           // Unique object id
  int                   triggOnce_;
  int                   cycleCounter_;
  int                   ignoreCycles_;
  double                scale_;              // Config: Data set size  
  FFT_STATUS            status_;             // Status/state  (NO_STAT, IDLE, ACQ, CALC)

  // Config options
  char*                 cfgDataSourceStr_;   // Config: data source string
  int                   cfgDbgMode_;         // Config: allow dbg printouts
  int                   cfgApplyScale_;      // Config: apply scale 1/nfft
  int                   cfgDcRemove_;        // Config: remove dc (average) 
  int                   cfgLinRemove_;       // Config: remove linear componet (by least square) 
  size_t                cfgNfft_;            // Config: Data set size
  int                   cfgEnable_;          // Config: Enable data acq./calc.
  FFT_MODE              cfgMode_;            // Config: Mode continous or triggered.
  double                cfgFFTSampleRateHz_; // Config: Sample rate (defaukts to ecmc rate)

  // Asyn
  int                   asynEnableId_;       // Enable/disable acq./calcs
  int                   asynRawDataId_;      // Raw data (input) array (double)
  int                   asynPPDataId_;       // Pre-processed data array (double)
  int                   asynFFTAmpId_;       // FFT amplitude array (double)
  int                   asynFFTModeId_;      // FFT mode (cont/trigg)
  int                   asynFFTStatId_;      // FFT status (no_stat/idle/acq/calc)
  int                   asynSourceId_;       // SOURCE
  int                   asynTriggId_;        // Trigg new measurement
  int                   asynFFTXAxisId_;     // FFT X-axis frequencies
  int                   asynNfftId_;         // NFFT
  int                   asynSRateId_;        // Sample rate

  // Thread related
  epicsEvent            doCalcEvent_;


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
  static void           printComplexArray(std::complex<double>* fftBuff,
                                          size_t elements,
                                          int objId);
  static std::string    to_string(int value);
  static int            leastSquare(int n,
                                    const double y[],
                                    double* k,
                                    double* m);  // y=kx+m
};

#endif  /* ECMC_FFT_H_ */
