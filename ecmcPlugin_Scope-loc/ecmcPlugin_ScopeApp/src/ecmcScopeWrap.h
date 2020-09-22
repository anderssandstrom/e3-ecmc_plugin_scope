/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcScopeWrap.h
*
*  Created on: Sept 21, 2020
*      Author: anderssandstrom
*
\*************************************************************************/
#ifndef ECMC_SCOPE_WRAP_H_
#define ECMC_SCOPE_WRAP_H_
#include "ecmcScopeDefs.h"

# ifdef __cplusplus
extern "C" {
# endif  // ifdef __cplusplus

int         createScope(char *configStr);

int         enableScope(int scopeIndex, int enable);

int         clearScope(int scopeIndex);

int         triggScope(int scopeIndex);


int         executeScopes();

/** \brief Link data to _all_ scope objects
 *
 *  This tells the Scope lib to connect to ecmc to find it's data source.\n
 *  This function should be called just before entering realtime since then all\n
 *  data sources in ecmc will be definded (plc sources are compiled just before runtime\n
 *  so are only fist accesible now).\n
 *  \return 0 if success or otherwise an error code.\n
 */
int  linkDataToScopes();

/** \brief Deletes all created scope objects\n
 *
 * Should be called when destructs.\n
 */

void deleteAllScopes();



# ifdef __cplusplus
}
# endif  // ifdef __cplusplus

#endif  /* ECMC_SCOPE_WRAP_H_ */
