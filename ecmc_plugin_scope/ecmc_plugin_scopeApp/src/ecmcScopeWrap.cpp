/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcScopeWrap.cpp
*
*  Created on: Sept 21, 2020
*      Author: anderssandstrom
*      Credits to  https://github.com/sgreg/dynamic-loading 
*
\*************************************************************************/

// Needed to get headers in ecmc right...
#define ECMC_IS_PLUGIN

#include <vector>
#include <stdexcept>
#include <string>
#include "ecmcScopeWrap.h"
#include "ecmcScope.h"
#include "ecmcScopeDefs.h"

#define ECMC_PLUGIN_PORTNAME_PREFIX "PLUGIN.SCOPE"
#define ECMC_PLUGIN_SCOPE_ERROR_CODE 1

static std::vector<ecmcScope*>  scopes;
static int                    scopeObjCounter = 0;

int createScope(char* configStr) {

  // create new ecmcFFT object
  ecmcScope* scope = NULL;

  try {
    scope = new ecmcScope(scopeObjCounter, configStr);
  }
  catch(std::exception& e) {
    if(scope) {
      delete scope;
    }
    printf("Exception: %s. Plugin will unload.\n",e.what());
    return ECMC_PLUGIN_SCOPE_ERROR_CODE;
  }
  
  scopes.push_back(scope);
  scopeObjCounter++;

  return 0;
}

void deleteAllScopes() {
  for(std::vector<ecmcScope*>::iterator pscope = scopes.begin(); pscope != scopes.end(); ++pscope) {
    if(*pscope) {
      delete (*pscope);
    }
  }
}

int  linkDataToScopes() {
  for(std::vector<ecmcScope*>::iterator pscope = scopes.begin(); pscope != scopes.end(); ++pscope) {
    if(*pscope) {
      try {
        (*pscope)->connectToDataSources();
      }
      catch(std::exception& e) {
        printf("Exception: %s. Plugin will unload.\n",e.what());
        return ECMC_PLUGIN_SCOPE_ERROR_CODE;
      }
    }
  }
  return 0;
}

int enableScope(int scopeIndex, int enable) {
  try {
    scopes.at(scopeIndex)->setEnable(enable);
  }
  catch(std::exception& e) {
    printf("Exception: %s. Scope index out of range.\n",e.what());
    return ECMC_PLUGIN_SCOPE_ERROR_CODE;
  }
  return 0;
}

// int clearScope(int scopeIndex) {
//   try {
//     scopes.at(scopeIndex)->clearBuffers();
//   }
//   catch(std::exception& e) {
//     printf("Exception: %s. Scope index out of range.\n",e.what());
//     return ECMC_PLUGIN_SCOPE_ERROR_CODE;
//   }  
//   return 0;
// }

int triggScope(int scopeIndex) {
  try {
    scopes.at(scopeIndex)->triggScope();
  }
  catch(std::exception& e) {
    printf("Exception: %s. Scope index out of range.\n",e.what());
    return ECMC_PLUGIN_SCOPE_ERROR_CODE;
  }  
  return 0;
}

int executeScopes() {
  try {
    for(std::vector<ecmcScope*>::iterator pscope = scopes.begin(); pscope != scopes.end(); ++pscope) {
      if(*pscope) {
        (*pscope)->execute();
      }
     }
  }
  catch(std::exception& e) {
    printf("Exception: %s.\n",e.what());
    return ECMC_PLUGIN_SCOPE_ERROR_CODE;
  }  
  return 0;
}
