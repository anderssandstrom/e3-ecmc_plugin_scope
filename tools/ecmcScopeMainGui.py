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
        self.pvPrefixStr = prefix
        self.scopePluginId = scopePluginId

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

        self.pause = 0        
        # Data Arrays
        self.missTriggCnt = None
        self.triggCnt = None
        self.rawdataY = None
        #self.rawdataX = None

        self.enable = None

        self.figure = plt.figure()                            
        #self.plottedLineSpect = None
        self.plottedLineRaw = None
        #self.axSpect = None
        self.axRaw = None
        self.canvas = FigureCanvas(self.figure)   
        self.toolbar = NavigationToolbar(self.canvas, self) 
        self.pauseBtn = QPushButton(text = 'pause')
        self.pauseBtn.setFixedSize(100, 50)
        self.pauseBtn.clicked.connect(self.pauseBtnAction)        
        self.pauseBtn.setStyleSheet("background-color: green")
        #self.pauseBtn.setCheckable(True) 

        self.openBtn = QPushButton(text = 'open data')
        self.openBtn.setFixedSize(100, 50)
        self.openBtn.clicked.connect(self.openBtnAction)


        self.enableBtn = QPushButton(text = 'enable Scope')
        self.enableBtn.setFixedSize(100, 50)
        self.enableBtn.clicked.connect(self.enableBtnAction)
        #self.enableBtn.setCheckable(True) 

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

        #self.triggBtn = QPushButton(text = 'trigg FFT')
        #self.triggBtn.setFixedSize(100, 50)
        #self.triggBtn.clicked.connect(self.triggBtnAction)            
       
        # Pv names based on structure:  <prefix>Plugin-FFT<scopePluginId>-<suffixname>
        self.pvNameTriggCnt = self.buildPvName('TriggCntAct') # "IOC_TEST:Plugin-FFT1-Spectrum-Amp-Act"
        print("self.pvNameTriggCnt=" + self.pvNameTriggCnt)
        self.pvNameMissTriggCnt = self.buildPvName('MissTriggCntAct') # "IOC_TEST:Plugin-FFT1-Spectrum-X-Axis-Act"
        print("self.pvNameMissTriggCnt=" + self.pvNameMissTriggCnt)        
        self.pvNameRawDataY = self.buildPvName('Data-Act') # IOC_TEST:Plugin-FFT0-Raw-Data-Act
        print("self.pvNameRawDataY=" + self.pvNameRawDataY)        
        self.pvnNameEnable = self.buildPvName('Enable') # IOC_TEST:Plugin-FFT0-Enable
        print("self.pvnNameEnable=" + self.pvnNameEnable)
        #self.pvnNameTrigg = self.buildPvName('Trigg') # IOC_TEST:Plugin-FFT0-Trigg
        #print("self.pvnNameTrigg=" + self.pvnNameTrigg)
        self.pvnNameSource = self.buildPvName('DataSource') # IOC_TEST:Plugin-FFT0-Source
        print("self.pvnNameSource=" + self.pvnNameSource)
        self.pvNameNextTimeSource = self.buildPvName('NextTimeSource') # IOC_TEST:Plugin-FFT0-Source
        print("self.pvNameNextTimeSource=" + self.pvNameNextTimeSource)
        self.pvNameTriggSource = self.buildPvName('TriggSource') # IOC_TEST:Plugin-FFT0-Source
        print("self.pvNameTriggSource=" + self.pvNameTriggSource)
        #self.pvnNameSampleRate = self.buildPvName('SampleRate-Act') # IOC_TEST:Plugin-FFT0-SampleRate-Act
        #print("self.pvnNameSampleRate=" + self.pvnNameSampleRate)
        #self.pvnNameNFFT = self.buildPvName('NFFT') # IOC_TEST:Plugin-FFT0-NFFT
        #print("self.pvnNameNFFT=" + self.pvnNameNFFT)
        self.pvNameScanToTriggSamples = self.buildPvName('ScanToTriggSamples') # IOC_TEST:Plugin-FFT0-Mode-RB
        print("self.pvNameScanToTriggSamples=" + self.pvNameScanToTriggSamples)

        self.connectPvs()
        
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
        
        # Fix layout
        self.setGeometry(300, 300, 900, 700)
        self.setWindowTitle("ecmc Scope Main plot: prefix=" + self.pvPrefixStr + " , scopeId=" + str(self.scopePluginId) + 
                            ", source="  + self.sourceStr + ', nexttime=' + self.nextTimeSourceStr + 
                            ', trigg=' + self.triggSourceStr) # + ", rate=" + str(self.sampleRate))

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

        #layoutControl.addWidget(self.triggBtn)
    
        frameControl = QFrame(self)
        frameControl.setFixedHeight(70)
        frameControl.setLayout(layoutControl)

        layoutVert.addWidget(frameControl) 

        self.setLayout(layoutVert)                
        return

    def buildPvName(self, suffixname):
        return self.pvPrefixStr + 'Plugin-Scope' + str(self.scopePluginId) + '-' + suffixname 

    def connectPvs(self):        

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

        #if self.pvnNameTrigg is None:
        #    raise RuntimeError("pvname trigg must not be 'None'")
        #if len(self.pvnNameTrigg)==0:
        #    raise RuntimeError("pvname trigg must not be ''")

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

        #if self.pvnNameSampleRate is None:
        #    raise RuntimeError("pvname sample rate must not be 'None'")
        #if len(self.pvnNameSampleRate)==0:
        #    raise RuntimeError("pvname sample rate must not be ''")
                
        if self.pvNameScanToTriggSamples is None:
            raise RuntimeError("pvname ScanToTriggSamples must not be 'None'")
        if len(self.pvNameScanToTriggSamples)==0:
            raise RuntimeError("pvname ScanToTriggSamples must not be ''")
        
        self.pvMissTriggCnt = epics.PV(self.pvNameMissTriggCnt)
        #print('self.pvMissTriggCnt: ' + self.pvMissTriggCnt.info)

        self.pvTriggCnt = epics.PV(self.pvNameTriggCnt)
        #print('self.pvTriggCnt: ' + self.pvTriggCnt.info)

        self.pvRawData = epics.PV(self.pvNameRawDataY)
        #print('self.pvRawData: ' + self.pvTriggCnt.info)

        self.pvEnable = epics.PV(self.pvnNameEnable)
        #print('self.pvEnable: ' + self.pvEnable.info)
        
        #self.pvTrigg = epics.PV(self.pvnNameTrigg)
        #print('self.pvTrigg: ' + self.pvTrigg.info)

        self.pvSource = epics.PV(self.pvnNameSource)
        #print('self.pvSource: ' + self.pvSource.info)

        self.pvNextTimeSource = epics.PV(self.pvNameNextTimeSource)
        #print('self.pvNextTimeSource: ' + self.pvNextTimeSource.info)

        self.pvTriggSource = epics.PV(self.pvNameTriggSource)
        #print('self.pvTriggSource: ' + self.pvTriggSource.info)
        
        #self.pvSampleRate = epics.PV(self.pvnNameSampleRate)
        #print('self.pvSampleRate: ' + self.pvSampleRate.info)

        self.pvScanToTriggSamples = epics.PV(self.pvNameScanToTriggSamples)
        #print('self.pvScanToTriggSamples: ' + self.pvScanToTriggSamples.info)        

        self.pvMissTriggCnt.add_callback(self.onChangepvMissTriggCnt)
        self.pvTriggCnt.add_callback(self.onChangepvTriggCnt)
        self.pvRawData.add_callback(self.onChangePvrawData)
        self.pvEnable.add_callback(self.onChangePvEnable)
        self.pvScanToTriggSamples.add_callback(self.onChangePVScanToTriggSamples)

        QCoreApplication.processEvents()
    
    ###### Pv monitor callbacks
    def onChangePVScanToTriggSamples(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalScanToTriggSamples.data_signal.emit(value)

    def onChangePvEnable(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalEnable.data_signal.emit(value)

    def onChangepvMissTriggCnt(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalMissTriggCnt.data_signal.emit(value)

    def onChangepvTriggCnt(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalTriggCnt.data_signal.emit(value)        

    def onChangePvrawData(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
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
            # print("Size X,Y: " + str(np.size(self.rawdataX))+ ", " +str(np.size(self.rawdataY)))
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
            self.comSignalTriggCnt.data_signal.emit(self.triggCnt)
            self.comSignalRawData.data_signal.emit(self.rawdataY)
        return

    def openBtnAction(self):
        self.pause = 0  # pause while open
        self.pauseBtnAction()
        fname = QFileDialog.getOpenFileName(self, 'Open file', '.', "Data files (*.npz)")
        if fname is None:
            return
        if np.size(fname) != 2:            
            return
        if len(fname[0])<=0:
            return        
        
        npzfile = np.load(fname[0])

        # verify scope plugin
        if npzfile['plugin']!="Scope":
            print ("Invalid data type (wrong plugin type)")
        # File valid 
        self.rawdataY           = npzfile['rawdataY']
        self.triggCnt           = npzfile['triggCnt']
        self.missTriggCnt       = npzfile['missTriggCnt']
        self.scanToTriggSamples = npzfile['scanToSample']
        # trigg draw 
        self.comSignalScanToTriggSamples.data_signal.emit(self.scanToTriggSamples)
        self.comSignalMissTriggCnt.data_signal.emit(self.missTriggCnt)
        self.comSignalTriggCnt.data_signal.emit(self.triggCnt)        
        self.comSignalRawData.data_signal.emit(self.rawdataY)        

        return

    def enableBtnAction(self):
        self.enable = not self.enable
        self.pvEnable.put(self.enable)
        if self.enable:
          self.enableBtn.setStyleSheet("background-color: green")
        else:
          self.enableBtn.setStyleSheet("background-color: red")
        return

    def saveBtnAction(self):
        fname = QFileDialog.getSaveFileName(self, 'Save file', '.', "Data files (*.npz)")
        if fname is None:
            return
        if np.size(fname) != 2:            
            return
        if len(fname[0])<=0:
            return
        np.savez(fname[0],plugin="Scope", rawdataY=self.rawdataY,triggCnt=self.triggCnt,missTriggCnt=self.missTriggCnt,scanToSample=self.scanToTriggSamples)
        return

    #def triggBtnAction(self):
    #    self.pvTrigg.put(True)
    #    return

    def plotRaw(self):
        if self.pause:            
            return
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
        self.axRaw.set_ylabel(self.pvNameRawDataY +' [' + self.pvRawData.units + ']') 

        # refresh canvas 
        self.canvas.draw()

        self.axRaw.autoscale(enable=True)

def printOutHelp():
  print("ecmcScopeMainGui: Plots waveforms of FFT data (updates on Y data callback). ")
  print("python ecmcScopeMainGui.py <prefix> <fftId>")
  print("<prefix>  : Ioc prefix ('IOC_TEST:')")
  print("<scopeId> : Id of scope plugin ('0')")
  print("example   : python ecmcScopeMainGui.py 'IOC_TEST:' '0'")
  print("Will connect to Pvs: <prefix>Plugin-Scope<scopeId>-*")

if __name__ == "__main__":
    import sys    
    if len(sys.argv)!=3:
        printOutHelp()
        sys.exit()
    prefix=sys.argv[1]
    scopeId=int(sys.argv[2])
    app = QtWidgets.QApplication(sys.argv)
    window=ecmcScopeMainGui(prefix=prefix,scopePluginId=scopeId)
    window.show()
    sys.exit(app.exec_())
