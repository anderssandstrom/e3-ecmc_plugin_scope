/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcPluginScope.cpp
*
*  Created on: Sept 21, 2020
*      Author: anderssandstrom
*
\*************************************************************************/

// Needed to get headers in ecmc right...
#define ECMC_IS_PLUGIN
#define ECMC_EXAMPLE_PLUGIN_VERSION 2

#ifdef __cplusplus
extern "C" {
#endif  // ifdef __cplusplus

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecmcPluginDefs.h"
#include "ecmcScopeDefs.h"
#include "ecmcScopeWrap.h"

static int    lastEcmcError   = 0;
static char*  lastConfStr         = NULL;

/** Optional. 
 *  Will be called once after successfull load into ecmc.
 *  Return value other than 0 will be considered error.
 *  configStr can be used for configuration parameters.
 **/
int scopeConstruct(char *configStr)
{
  //This module is allowed to load several times so no need to check if loaded

  // create FFT object and register data callback
  lastConfStr = strdup(configStr);
  return createScope(configStr);
}

/** Optional function.
 *  Will be called once at unload.
 **/
void scopeDestruct(void)
{
  deleteAllScopes();
  if(lastConfStr){
    free(lastConfStr);
  }
}

/** Optional function.
 *  Will be called each realtime cycle if definded
 *  ecmcError: Error code of ecmc. Makes it posible for 
 *  this plugin to react on ecmc errors
 *  Return value other than 0 will be considered to be an error code in ecmc.
 **/
int scopeRealtime(int ecmcError)
{ 
  lastEcmcError = ecmcError;  
  return executeScopes();
}

/** Link to data source here since all sources should be availabe at this stage
 *  (for example ecmc PLC variables are defined only at enter of realtime)
 **/
int scopeEnterRT(){
  return linkDataToScopes();
}

/** Optional function.
 *  Will be called once just before leaving realtime mode
 *  Return value other than 0 will be considered error.
 **/
int scopeExitRT(void){
  return 0;
}

// Plc function for clear of buffers
double scope_clear(double index) {
  return (double)clearScope((int)index);
}

// Plc function for enable
double scope_enable(double index, double enable) {
  return (double)enableScope((int)index, (int)enable);
}

// Plc function for trigg new measurement (will clear buffers)
double scope_trigg(double index) {
  return (double)triggScope((int)index);
}
#define ECMC_PLUGIN_DBG_PRINT_OPTION_CMD       "DBG_PRINT="
#define ECMC_PLUGIN_SOURCE_OPTION_CMD          "SOURCE="
#define ECMC_PLUGIN_SOURCE_NEXTTIME_OPTION_CMD "SOURCE_NEXTTIME="
#define ECMC_PLUGIN_TRIGG_OPTION_CMD           "TRIGG="
#define ECMC_PLUGIN_RESULT_ELEMENTS_OPTION_CMD "RESULT_ELEMENS="
#define ECMC_PLUGIN_ENABLE_OPTION_CMD          "ENABLE="

// Register data for plugin so ecmc know what to use
struct ecmcPluginData pluginDataDef = {
  // Allways use ECMC_PLUG_VERSION_MAGIC
  .ifVersion = ECMC_PLUG_VERSION_MAGIC, 
  // Name 
  .name = "ecmcPlugin_Scope",
  // Description
  .desc = "Scope plugin for use with ecmc.",
  // Option description
  .optionDesc = "\n    "ECMC_PLUGIN_DBG_PRINT_OPTION_CMD"<1/0>    : Enables/disables printouts from plugin, default = disabled.\n"
                "    "ECMC_PLUGIN_SOURCE_OPTION_CMD"<source>    : Ec source variable (example: ec0.s1.mm.CH1_ARRAY).\n"
                "    "ECMC_PLUGIN_RESULT_ELEMENTS_OPTION_CMD"<Result buffer size>        : Data points to collect, default = 4096.\n"
                "    "ECMC_PLUGIN_SOURCE_NEXTTIME_OPTION_CMD"<nexttime>   : Ec next sync time for source (example: ec0.s1.NEXTTIME)\n"
                "    "ECMC_PLUGIN_TRIGG_OPTION_CMD"<trigger>   : Ec trigg time (example: ec0.s2.LATCH_POS).\n"
                "    "ECMC_PLUGIN_ENABLE_OPTION_CMD"<1/0>   : Enable data acq, defaults to enabled.\n"
                , 
  // Plugin version
  .version = ECMC_EXAMPLE_PLUGIN_VERSION,
  // Optional construct func, called once at load. NULL if not definded.
  .constructFnc = scopeConstruct,
  // Optional destruct func, called once at unload. NULL if not definded.
  .destructFnc = scopeDestruct,
  // Optional func that will be called each rt cycle. NULL if not definded.
  .realtimeFnc = scopeRealtime,
  // Optional func that will be called once just before enter realtime mode
  .realtimeEnterFnc = scopeEnterRT,
  // Optional func that will be called once just before exit realtime mode
  .realtimeExitFnc = scopeExitRT,
  // PLC funcs
  .funcs[0] =
      { /*----fft_clear----*/
        // Function name (this is the name you use in ecmc plc-code)
        .funcName = "scope_clear",
        // Function description
        .funcDesc = "double scope_clear(index) : Clear/reset scope[index].",
        /**
        * 12 different prototypes allowed (only doubles since reg in plc).
        * Only funcArg${argCount} func shall be assigned the rest set to NULL.
        **/
        .funcArg0 = NULL,
        .funcArg1 = scope_clear,
        .funcArg2 = NULL,
        .funcArg3 = NULL,
        .funcArg4 = NULL,
        .funcArg5 = NULL,
        .funcArg6 = NULL,
        .funcArg7 = NULL,
        .funcArg8 = NULL,
        .funcArg9 = NULL,
        .funcArg10 = NULL,
        .funcGenericObj = NULL,
      },
  .consts[0] = {0}, // last element set all to zero..
};

ecmc_plugin_register(pluginDataDef);

# ifdef __cplusplus
}
# endif  // ifdef __cplusplus
