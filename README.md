e3-ecmcPlugin_Scope
======
ESS Site-specific EPICS module : ecmcPlugin_Scope

A shared library with Scope functionalities loadable into ecmc:

https://github.com/epics-modules/ecmc (or local ess fork https://github.com/icshwi/ecmc).

Configuration is made through ecmccfg:

https://github.com/paulscherrerinstitute/ecmccfg (ot local ess fork https://github.com/icshwi/ecmccfg)


# Introduction

The main functionality of this plugin is triggerd sampling of ethercat data from oversampled and timestamped ethercat slaves, like:

Data:

* EL3702

* EL3742

* EL5101-0011

* ELM3604

* ....

Trigger:

* EL1252

* EL1252-0050

## Loading of Scope plugin in ecmc: 

NOTE: Do not use "require" to load plugin.

A plugin is loaded by the ecmccfg command loadPlugin:
https://github.com/icshwi/ecmccfg/blob/master/scripts/loadPlugin.cmd

Example:
```
epicsEnvSet(RESULT_NELM, 1024)
epicsEnvSet(ECMC_PLUGIN_FILNAME,"/home/pi/sources/e3-ecmcPlugin_Scope/ecmcPlugin_Scope-loc/O.7.0.4_linux-arm/libecmcPlugin_Scope.so")
epicsEnvSet(ECMC_PLUGIN_CONFIG,"SOURCE=ec0.s${SLAVE_NUM_AI}.mm.CH1_ARRAY;DBG_PRINT=1;TRIGG=ec0.s${SLAVE_NUM_TRIGG}.CH1_LATCH_POS;SOURCE_NEXTTIME=ec0.s${SLAVE_NUM_AI}.NEXT_TIME;RESULT_ELEMENTS=${RESULT_NELM};")
${SCRIPTEXEC} ${ecmccfg_DIR}loadPlugin.cmd, "PLUGIN_ID=1,FILE=${ECMC_PLUGIN_FILNAME},CONFIG='${ECMC_PLUGIN_CONFIG}', REPORT=1"
epicsEnvUnset(ECMC_PLUGIN_FILNAME)
epicsEnvUnset(ECMC_PLUGIN_CONFIG)
dbLoadRecords("../template/ecmcPluginScope.template","P=$(IOC):,PORT=${ECMC_ASYN_PORT},INDEX=0,RESULT_NELM=${RESULT_NELM},RESULT_DTYP=asynInt16ArrayIn,RESULT_FTVL=SHORT")
epicsEnvUnset(RESULT_NELM, 1024)
```

This plugin supports multiple loading. For each load of the plugin a new Scope object will be created. In order to access these plugins, from plc:s or EPICS records, they can be accessed by an index. The first Scope plugin will have index 0. The next loaded Scope plugin will have index 1...

Note: If another plugin is loaded in between the loading of Scope plugins, it will have no affect on these Scope indexes (so the Scope index is _not_ the same as plugin index).


## Configuration:

Three links to ethercat data needs to be defined:
1. Source data

2. Source data timestamp

3. Trigger timestamp

All these three links needs to be defined in the plugin startup configuration string.

Other configurations that can be made:

4. Data elements to collect

5. Debug printouts 

6. Enable

  
### Source data (mandatory)

The source data should normally be a ecmc memmap.

The source is defined by the "SOURCE" configuration string:
``` 
SOURCE=ec0.s2.mm.CH1_ARRAY;
``` 

### Source data timestamp (mandatory)

In order to know which source data elements that correspond to the trigger value, the oversampled ethercat slaves normally have a pdo that contains the value of the next dc sync time which is the dc timestamp of the next acquired data element.  

The source timestamp is defined by the "SOURCE_NEXTTIME" configuration string:
``` 
SOURCE_NEXTTIME=ec0.s2.NEXT_TIME;
``` 
This timestamp can be either in 32bit or 64bit format. If 32 bits then "NEXT_TIME" is always considered to be later than the trigger timestamp.

### Trigger (mandatory)

The trigger should normally be a timestamped digital input, like EL1252.

The trigger timestamp is defined by the "TRIGG" configuration string:
``` 
TRIGG=ec0.s5.CH1_LATCH_POS;
``` 
This timestamp can be either in 32bit or 64bit format. If 32 bits then "NEXT_TIME" is always considered to be later than the trigger timestamp.

### Data elements to collect (optional)

The number of values to be collected after the trigger is defined by setting the option "RESULT_ELEMENTS" in the configurations string. The default value is 1024 data elements of the same type as the choosen source.
``` 
RESULT_ELEMENTS=2048;
``` 

### Debug printouts (optional)

Debug printouts can be enbaled/disabled by the option DBG_PRINT (defaults to 0)
``` 
DBG_PRINT=1;
``` 

### Enable (optional)

The acuiring of data can be enabled/disabled by the "ENABLE" option (defaults to 1)
``` 
ENABLE=0;
``` 

### Example of complete configuration string
``` 
epicsEnvSet(ECMC_PLUGIN_CONFIG,"SOURCE=ec0.s${SLAVE_NUM_AI}.mm.CH1_ARRAY;DBG_PRINT=1;TRIGG=ec0.s${SLAVE_NUM_TRIGG}.CH1_LATCH_POS;SOURCE_NEXTTIME=ec0.s${SLAVE_NUM_AI}.NEXT_TIME;RESULT_ELEMENTS=${RESULT_NELM};")
``` 

## EPICS records
Each Scope plugin object will create a new asyn parameters.
The reason for a dedicated asynport is to disturb ecmc as little as possible.
The plugin contains a template file, "ecmcPluginScope.template", that will make most information availbe from records:

* Enable                           (rw)
* Data Source                      (ro)
* Trigger source                   (ro)
* Resultdata                       (ro)

The available records from this template file can be listed by the cmd:
```
raspberrypi-15269 > dbgrep *Scope*
IOC_TEST:Plugin-Scope0-Enable
IOC_TEST:Plugin-Scope0-DataSource
IOC_TEST:Plugin-Scope0-TriggSource
IOC_TEST:Plugin-Scope0-Data-Act

```
## Example script
An example script can be found in the iocsh directory of this repo.

## Plugin info

```
Plugin info: 
  Index                = 0
  Name                 = ecmcPlugin_Scope
  Description          = Scope plugin for use with ecmc.
  Option description   = 
    DBG_PRINT=<1/0>    : Enables/disables printouts from plugin, default = disabled.
    SOURCE=<source>    : Ec source variable (example: ec0.s1.mm.CH1_ARRAY).
    RESULT_ELEMENS=<Result buffer size>        : Data points to collect, default = 4096.
    SOURCE_NEXTTIME=<nexttime>   : Ec next sync time for source (example: ec0.s1.NEXTTIME)
    TRIGG=<trigger>   : Ec trigg time (example: ec0.s2.LATCH_POS).
    ENABLE=<1/0>   : Enable data acq, defaults to enabled.

  Filename             = /home/pi/sources/e3-ecmcPlugin_Scope/ecmcPlugin_Scope-loc/O.7.0.4_linux-arm/libecmcPlugin_Scope.so
  Config string        = SOURCE=ec0.s11.mm.CH1_ARRAY;DBG_PRINT=1;TRIGG=ec0.s6.CH1_LATCH_POS;SOURCE_NEXTTIME=ec0.s11.NEXT_TIME;RESULT_ELEMENTS=1024;
  Version              = 2
  Interface version    = 65536 (ecmc = 65536)
     max plc funcs     = 64
     max plc func args = 10
     max plc consts    = 64
  Construct func       = @0xb5026400
  Enter realtime func  = @0xb5026450
  Exit realtime func   = @0xb50263f8
  Realtime func        = @0xb502644c
  Destruct func        = @0xb5026428
  dlhandle             = @0xa228d8
  Plc functions:
  Plc constants:

```

