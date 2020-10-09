#*************************************************************************
# Copyright (c) 2020 European Spallation Source ERIC
# ecmc is distributed subject to a Software License Agreement found
# in file LICENSE that is included with this distribution. 
#
#   ecmcScopeMainGui.py
#
#  Created on: October 6, 2020
#      Author: Anders Sandström
#    
#  Plots two waveforms (x vs y) updates for each callback on the y-pv
#  
#*************************************************************************

import sys
import epics
from PyQt5.QtWidgets import *
from PyQt5 import QtWidgets
from PyQt5.QtCore import *
from PyQt5.QtGui import *
import numpy as np
import matplotlib
matplotlib.use("Qt5Agg")
from matplotlib.figure import Figure
from matplotlib.animation import TimedAnimation
from matplotlib.lines import Line2D
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT as NavigationToolbar
import matplotlib.pyplot as plt 
import threading

# Scope object pvs <prefix>Plugin-FFT<scopePluginId>-<suffixname>
# IOC_TEST:Plugin-Scope0-MissTriggCntAct x
# IOC_TEST:Plugin-Scope0-ScanToTriggSamples x
# IOC_TEST:Plugin-Scope0-TriggCntAct x
# IOC_TEST:Plugin-Scope0-Enable x
# IOC_TEST:Plugin-Scope0-DataSource x
# IOC_TEST:Plugin-Scope0-TriggSource
# IOC_TEST:Plugin-Scope0-NextTimeSource
# IOC_TEST:Plugin-Scope0-Data-Act x

class comSignal(QObject):
    data_signal = pyqtSignal(object)

class ecmcScopeMainGui(QtWidgets.QDialog):
    def __init__(self,prefix=None,scopePluginId=None):
        super(ecmcScopeMainGui, self).__init__()
        self.offline = False
        self.pvPrefixStr = prefix
        self.scopePluginId = scopePluginId
        self.allowSave = False
        if prefix is None or scopePluginId is None:
            self.offline = True
            self.pause = True
            self.enable = False           
        else:
            self.buildPvNames()
            self.offline = False
            self.pause = False            

        # Callbacks through signals
        self.comSignalMissTriggCnt = comSignal()
        self.comSignalMissTriggCnt.data_signal.connect(self.callbackFuncMissTriggCnt)
        self.comSignalTriggCnt = comSignal()
        self.comSignalTriggCnt.data_signal.connect(self.callbackFuncTriggCnt)
        self.comSignalRawData = comSignal()
        self.comSignalRawData.data_signal.connect(self.callbackFuncrawData)
        self.comSignalEnable = comSignal()
        self.comSignalEnable.data_signal.connect(self.callbackFuncEnable)
        self.comSignalScanToTriggSamples = comSignal()
        self.comSignalScanToTriggSamples.data_signal.connect(self.callbackFuncScanToTriggSamples)

        # Data
        self.missTriggCnt = None
        self.triggCnt = None
        self.rawdataY = None
        self.enable = None
        
        self.createWidgets()
        self.connectPvs()
        self.setStatusOfWidgets()
        return

    def buildPvName(self, suffixname):
        return self.pvPrefixStr + 'Plugin-Scope' + str(self.scopePluginId) + '-' + suffixname 

    def buildPvNames(self):
        if self.offline:
           self.pvNameTriggCnt = None
           self.pvNameMissTriggCnt = None
           self.pvNameRawDataY = None
           self.pvnNameEnable = None
           self.pvnNameSource = None
           self.pvNameNextTimeSource = None
           self.pvNameTriggSource = None
           self.pvNameScanToTriggSamples = None
        else:
           self.pvNameTriggCnt = self.buildPvName('TriggCntAct') # "IOC_TEST:Plugin-FFT1-Spectrum-Amp-Act"
           self.pvNameMissTriggCnt = self.buildPvName('MissTriggCntAct') # "IOC_TEST:Plugin-FFT1-Spectrum-X-Axis-Act"
           self.pvNameRawDataY = self.buildPvName('Data-Act') # IOC_TEST:Plugin-FFT0-Raw-Data-Act
           self.pvnNameEnable = self.buildPvName('Enable') # IOC_TEST:Plugin-FFT0-Enable
           self.pvnNameSource = self.buildPvName('DataSource') # IOC_TEST:Plugin-FFT0-Source
           self.pvNameNextTimeSource = self.buildPvName('NextTimeSource') # IOC_TEST:Plugin-FFT0-Source
           self.pvNameTriggSource = self.buildPvName('TriggSource') # IOC_TEST:Plugin-FFT0-Source
           self.pvNameScanToTriggSamples = self.buildPvName('ScanToTriggSamples') # IOC_TEST:Plugin-FFT0-Mode-RB
    
    def createWidgets(self):
        self.figure = plt.figure()
        self.plottedLineRaw = None
        self.axRaw = None
        self.canvas = FigureCanvas(self.figure)   
        self.toolbar = NavigationToolbar(self.canvas, self) 
        self.pauseBtn = QPushButton(text = 'on/off-line')
        self.pauseBtn.setFixedSize(100, 50)
        self.pauseBtn.clicked.connect(self.pauseBtnAction)        
        self.pauseBtn.setStyleSheet("background-color: green")        
        self.openBtn = QPushButton(text = 'open data')
        self.openBtn.setFixedSize(100, 50)
        self.openBtn.clicked.connect(self.openBtnAction)
        self.enableBtn = QPushButton(text = 'enable Scope')
        self.enableBtn.setFixedSize(100, 50)
        self.enableBtn.clicked.connect(self.enableBtnAction)
        self.saveBtn = QPushButton(text = 'save data')
        self.saveBtn.setFixedSize(100, 50)
        self.saveBtn.clicked.connect(self.saveBtnAction)
        self.triggCntLineEdit = QLineEdit(text = '0')
        self.triggCntLineEdit.setEnabled(False)
        self.triggCntLineEdit.setFixedSize(150, 30)
        self.triggCntLabel = QLabel(text = "triggers []:")
        self.missTriggCntLineEdit = QLineEdit(text = '0')
        self.missTriggCntLineEdit.setEnabled(False)
        self.missTriggCntLineEdit.setFixedSize(150, 30)
        self.missTriggCntLabel = QLabel(text = "missed []:")
        self.scanToTriggSamplesLineEdit = QLineEdit(text = '0')
        self.scanToTriggSamplesLineEdit.setEnabled(False)
        self.scanToTriggSamplesLineEdit.setFixedSize(150, 30)
        self.scanToTriggSamplesLabel = QLabel(text = "Sample offset []:")
               
        # Fix layout
        self.setGeometry(300, 300, 900, 700)        

        layoutVert = QVBoxLayout()
        layoutVert.addWidget(self.toolbar) 
        layoutVert.addWidget(self.canvas) 

        layoutTrigger = QVBoxLayout()
        frameTrigger = QFrame(self)
        frameTrigger.setFixedWidth(100)
        layoutTrigger.addWidget(self.triggCntLabel)
        layoutTrigger.addWidget(self.triggCntLineEdit)
        frameTrigger.setLayout(layoutTrigger)

        layoutMissedTrigger = QVBoxLayout()
        frameMissedTrigger = QFrame(self)
        frameMissedTrigger.setFixedWidth(100)
        layoutMissedTrigger.addWidget(self.missTriggCntLabel)
        layoutMissedTrigger.addWidget(self.missTriggCntLineEdit)
        frameMissedTrigger.setLayout(layoutMissedTrigger)

        layoutSamplesToTrigger = QVBoxLayout()
        frameSamplesToTrigger = QFrame(self)
        frameSamplesToTrigger.setFixedWidth(100)
        layoutSamplesToTrigger.addWidget(self.scanToTriggSamplesLabel)
        layoutSamplesToTrigger.addWidget(self.scanToTriggSamplesLineEdit)
        frameSamplesToTrigger.setLayout(layoutSamplesToTrigger)

        layoutControl = QHBoxLayout() 
        layoutControl.addWidget(self.pauseBtn)
        layoutControl.addWidget(self.enableBtn)
        layoutControl.addWidget(frameTrigger)
        layoutControl.addWidget(frameMissedTrigger)
        layoutControl.addWidget(frameSamplesToTrigger)
        layoutControl.addWidget(self.saveBtn)
        layoutControl.addWidget(self.openBtn)
    
        frameControl = QFrame(self)
        frameControl.setFixedHeight(70)
        frameControl.setLayout(layoutControl)

        layoutVert.addWidget(frameControl) 

        self.setLayout(layoutVert)

    def setStatusOfWidgets(self):
        self.saveBtn.setEnabled(self.allowSave)
        if self.offline:
            self.enableBtn.setStyleSheet("background-color: grey")
            self.enableBtn.setEnabled(False)
            self.pauseBtn.setStyleSheet("background-color: grey")
            self.pauseBtn.setEnabled(False)
            
            self.setWindowTitle("ecmc Scope Main plot: Offline")
            
            #self.sourceStr = "Offline"
            #self.scanToTriggSamples = 0
            #self.nextTimeSourceSt = "Offline"
            #self.triggSourceStr = "Offline"
        else:

            self.enableBtn.setEnabled(True)
            self.pauseBtn.setEnabled(True)
            # Check actual value of pvs
            if(self.pvEnable.get() > 0):
               self.enableBtn.setStyleSheet("background-color: green")
               self.enable = True
            else:
               self.enableBtn.setStyleSheet("background-color: red")
               self.enable = False

            self.sourceStr = self.pvSource.get(as_string=True)
            self.scanToTriggSamples = self.pvScanToTriggSamples.get()
            self.nextTimeSourceStr = self.pvNextTimeSource.get(as_string=True)
            self.triggSourceStr = self.pvTriggSource.get(as_string=True)
            self.setWindowTitle("ecmc Scope Main plot: prefix=" + self.pvPrefixStr + " , scopeId=" + str(self.scopePluginId) + 
                        ", source="  + self.sourceStr + ', nexttime=' + self.nextTimeSourceStr + 
                        ', trigg=' + self.triggSourceStr)


    def connectPvs(self):        
        if self.offline:
            return

        if self.pvNameMissTriggCnt is None:
            raise RuntimeError("pvname missed trigg counter must not be 'None'")
        if len(self.pvNameMissTriggCnt)==0:
            raise RuntimeError("pvname  missed trigg counter must not be ''")

        if self.pvNameTriggCnt is None:
            raise RuntimeError("pvname trigg counter must not be 'None'")
        if len(self.pvNameTriggCnt)==0:
            raise RuntimeError("pvname trigg counter must not be ''")

        if self.pvNameRawDataY is None:
            raise RuntimeError("pvname raw data must not be 'None'")
        if len(self.pvNameRawDataY)==0:
            raise RuntimeError("pvname raw data must not be ''")

        if self.pvnNameEnable is None:
            raise RuntimeError("pvname enable must not be 'None'")
        if len(self.pvnNameEnable)==0:
            raise RuntimeError("pvname enable must not be ''")

        if self.pvnNameSource is None:
            raise RuntimeError("pvname source must not be 'None'")
        if len(self.pvnNameSource)==0:
            raise RuntimeError("pvname source must not be ''")

        if self.pvNameNextTimeSource is None:
            raise RuntimeError("pvname NextTimeSource must not be 'None'")
        if len(self.pvNameNextTimeSource)==0:
            raise RuntimeError("pvname NextTimeSource must not be ''")

        if self.pvNameTriggSource is None:
            raise RuntimeError("pvname TriggSource must not be 'None'")
        if len(self.pvNameTriggSource)==0:
            raise RuntimeError("pvname TriggSource must not be ''")
               
        if self.pvNameScanToTriggSamples is None:
            raise RuntimeError("pvname ScanToTriggSamples must not be 'None'")
        if len(self.pvNameScanToTriggSamples)==0:
            raise RuntimeError("pvname ScanToTriggSamples must not be ''")
        
        self.pvMissTriggCnt = epics.PV(self.pvNameMissTriggCnt)
        if self.pvMissTriggCnt is None:
            print('Failed connect to PV')
            self.offline = 1
            return
        self.pvTriggCnt = epics.PV(self.pvNameTriggCnt)
        if self.pvTriggCnt is None:
            print('Failed connect to PV')
            self.offline = 1
            return
        self.pvRawData = epics.PV(self.pvNameRawDataY)
        if self.pvRawData is None:
            print('Failed connect to PV')
            self.offline = 1
            return
        self.pvEnable = epics.PV(self.pvnNameEnable)
        if self.pvEnable is None:
            print('Failed connect to PV')
            self.offline = 1
            return
        self.pvSource = epics.PV(self.pvnNameSource)
        if self.pvSource is None:
            print('Failed connect to PV')
            self.offline = 1
            return        
        self.pvNextTimeSource = epics.PV(self.pvNameNextTimeSource)
        if self.pvNextTimeSource is None:
            print('Failed connect to PV')
            self.offline = 1
            return
        self.pvTriggSource = epics.PV(self.pvNameTriggSource)
        if self.pvTriggSource is None:
            print('Failed connect to PV')
            self.offline = 1
            return
        self.pvScanToTriggSamples = epics.PV(self.pvNameScanToTriggSamples)
        if self.pvScanToTriggSamples is None:
            print('Failed connect to PV')
            self.offline = 1
            return

        self.pvMissTriggCnt.add_callback(self.onChangepvMissTriggCnt)
        self.pvTriggCnt.add_callback(self.onChangepvTriggCnt)
        self.pvRawData.add_callback(self.onChangePvrawData)
        self.pvEnable.add_callback(self.onChangePvEnable)
        self.pvScanToTriggSamples.add_callback(self.onChangePVScanToTriggSamples)
        QCoreApplication.processEvents()
    
    ###### Pv monitor callbacks
    def onChangePVScanToTriggSamples(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        if self.pause:
            return
        self.comSignalScanToTriggSamples.data_signal.emit(value)

    def onChangePvEnable(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        if self.pause:
            return
        self.comSignalEnable.data_signal.emit(value)

    def onChangepvMissTriggCnt(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        if self.pause:
            return
        self.comSignalMissTriggCnt.data_signal.emit(value)

    def onChangepvTriggCnt(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        if self.pause:
            return
        self.comSignalTriggCnt.data_signal.emit(value)        

    def onChangePvrawData(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        if self.pause:
            return
        self.comSignalRawData.data_signal.emit(value)

    ###### Signal callbacks    
    def callbackFuncScanToTriggSamples(self, value):    
        self.scanToTriggSamples = value
        self.scanToTriggSamplesLineEdit.setText(str(self.scanToTriggSamples))
        return

    def callbackFuncEnable(self, value):
        self.enable = value        
        if self.enable:
          self.enableBtn.setStyleSheet("background-color: green")
        else:
          self.enableBtn.setStyleSheet("background-color: red")
        return

    def callbackFuncMissTriggCnt(self, value):
        if(np.size(value)) > 0:
            self.missTriggCnt = value
            self.missTriggCntLineEdit.setText(str(self.missTriggCnt))            
        return

    def callbackFuncTriggCnt(self, value):
        if(np.size(value)) > 0:
            self.triggCnt = value
            self.triggCntLineEdit.setText(str(self.triggCnt))
        return

    def callbackFuncrawData(self, value):
        if(np.size(value)) > 0:
            #if self.rawdataX is None or np.size(value) != np.size(self.rawdataY):
            #    self.rawdataX = np.arange(-np.size(value)/self.sampleRate, 0, 1/self.sampleRate)
            self.rawdataY = value
            self.plotRaw()
        return

    ###### Widget callbacks
    def pauseBtnAction(self):   
        self.pause = not self.pause
        if self.pause:
            self.pauseBtn.setStyleSheet("background-color: red")
        else:
            self.pauseBtn.setStyleSheet("background-color: green")
        
        # Retrigger plots with newest values
        if not self.offline:
           self.missTriggCnt = self.pvMissTriggCnt.get()
           self.triggCnt     = self.pvTriggCnt.get()
           self.rawdataY     = self.pvRawData.get()
           self.enable       = self.pvEnable.get()
           self.scanToTriggSamples =self.pvScanToTriggSamples.get()

        self.comSignalScanToTriggSamples.data_signal.emit(self.scanToTriggSamples)
        self.comSignalMissTriggCnt.data_signal.emit(self.missTriggCnt)
        self.comSignalTriggCnt.data_signal.emit(self.triggCnt)        
        self.comSignalRawData.data_signal.emit(self.rawdataY)

        return

    def openBtnAction(self):
        if not self.offline:
           self.pause = 1  # pause while open if online
           self.pauseBtn.setStyleSheet("background-color: red")
           QCoreApplication.processEvents()
                   
        fname = QFileDialog.getOpenFileName(self, 'Open file', '.', "Data files (*.npz)")
        if fname is None:
            return
        if np.size(fname) != 2:            
            return
        if len(fname[0])<=0:
            return        
        
        npzfile = np.load(fname[0])

        # verify scope plugin
        if npzfile['plugin'] != "Scope":
            print ("Invalid data type (wrong plugin type)")
            return
        
        # File valid 
        self.rawdataY                 = npzfile['rawdataY']
        self.triggCnt                 = npzfile['triggCnt']
        self.missTriggCnt             = npzfile['missTriggCnt']
        self.scanToTriggSamples       = npzfile['scanToSample']
        self.pvPrefixStr              = npzfile['pvPrefixStr']
        self.scopePluginId            = npzfile['scopePluginId']
        self.sourceStr                = npzfile['sourceStr']
        self.scanToTriggSamples       = npzfile['scanToTriggSamles']
        self.nextTimeSourceStr        = npzfile['nextTimeSourceStr']
        self.triggSourceStr           = npzfile['triggSourceStr']

        if self.offline: # do not overwrite if online mode
           self.buildPvNames()

        # trigg draw
        self.comSignalRawData.data_signal.emit(self.rawdataY)
        self.comSignalScanToTriggSamples.data_signal.emit(self.scanToTriggSamples)
        self.comSignalMissTriggCnt.data_signal.emit(self.missTriggCnt)
        self.comSignalTriggCnt.data_signal.emit(self.triggCnt)        
        
        self.setStatusOfWidgets()
        return

    def saveBtnAction(self):
        fname = QFileDialog.getSaveFileName(self, 'Save file', '.', "Data files (*.npz)")
        if fname is None:
            return
        if np.size(fname) != 2:            
            return
        if len(fname[0])<=0:
            return
        # Save all relevant data
        np.savez(fname[0],
                 plugin                   = "Scope",
                 rawdataY                 = self.rawdataY,
                 triggCnt                 = self.triggCnt,
                 missTriggCnt             = self.missTriggCnt,
                 scanToSample             = self.scanToTriggSamples,
                 pvPrefixStr              = self.pvPrefixStr,
                 scopePluginId            = self.scopePluginId,
                 sourceStr                = self.sourceStr,
                 scanToTriggSamples       = self.scanToTriggSamples,
                 nextTimeSourceStr        = self.nextTimeSourceStr,
                 triggSourceStr           = self.triggSourceStr)

        return

    def enableBtnAction(self):
        self.enable = not self.enable
        self.pvEnable.put(self.enable)
        if self.enable:
          self.enableBtn.setStyleSheet("background-color: green")
        else:
          self.enableBtn.setStyleSheet("background-color: red")
        return

    # Plots 
    def plotRaw(self):
        if self.rawdataY is None:
            return
        
        # create an axis for spectrum
        if self.axRaw is None:
           self.axRaw = self.figure.add_subplot(111)

        # plot data 
        if self.plottedLineRaw is not None:
            self.plottedLineRaw.remove()

        self.plottedLineRaw, = self.axRaw.plot(self.rawdataY, 'b*-') 
        #self.plottedLineRaw, = self.axRaw.plot(self.rawdataX,self.rawdataY, 'b*-') 
        self.axRaw.grid(True)

        self.axRaw.set_xlabel('Samples []')
        if self.offline:
           self.axRaw.set_ylabel(self.pvNameRawDataY)  # No unit in offline mode..
        else:
           self.axRaw.set_ylabel(self.pvNameRawDataY  +' [' + self.pvRawData.units + ']') 
        # refresh canvas 
        self.canvas.draw()
        self.axRaw.autoscale(enable=True)
        self.allowSave = True
        self.saveBtn.setEnabled(self.allowSave)


def printOutHelp():
  print("ecmcScopeMainGui: Plots waveforms of FFT data (updates on Y data callback). ")
  print("python ecmcScopeMainGui.py <prefix> <fftId>")
  print("<prefix>  : Ioc prefix ('IOC_TEST:')")
  print("<scopeId> : Id of scope plugin ('0')")
  print("example   : python ecmcScopeMainGui.py 'IOC_TEST:' '0'")
  print("Will connect to Pvs: <prefix>Plugin-Scope<scopeId>-*")

if __name__ == "__main__":
    import sys
    # Open in offline mode
    prefix = None
    scopeId = None
    if len(sys.argv) == 1:
       prefix = None
       scopeId = None
    elif len(sys.argv) == 3:
       prefix = sys.argv[1]
       scopeId = int(sys.argv[2])
    else:
       printOutHelp()
       sys.exit()    
    app = QtWidgets.QApplication(sys.argv)
    window=ecmcScopeMainGui(prefix=prefix,scopePluginId=scopeId)
    window.show()
    sys.exit(app.exec_())
