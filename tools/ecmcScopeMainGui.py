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
# IOC_TEST:Plugin-Scope0-MissTriggCntAct
# IOC_TEST:Plugin-Scope0-ScanToTriggSamples
# IOC_TEST:Plugin-Scope0-TriggCntAct
# IOC_TEST:Plugin-Scope0-Enable
# IOC_TEST:Plugin-Scope0-DataSource
# IOC_TEST:Plugin-Scope0-TriggSource
# IOC_TEST:Plugin-Scope0-NextTimeSource
# IOC_TEST:Plugin-Scope0-Data-Act


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
        #self.comSignalMode = comSignal()
        #self.comSignalMode.data_signal.connect(self.callbackFuncMode)

        self.pause = 0

        # Data Arrays
        self.missTriggCnt = None
        self.triggCnt = None
        self.rawdataY = None
        self.rawdataX = None

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

        self.enableBtn = QPushButton(text = 'enable Scope')
        self.enableBtn.setFixedSize(100, 50)
        self.enableBtn.clicked.connect(self.enableBtnAction)            

        self.triggCntLineEdit = QLineEdit(text = '0')
        self.triggCntLineEdit.setEnabled(False)
        self.triggCntLineEdit.setFixedSize(100, 50)

        self.missTriggCntLineEdit = QLineEdit(text = '0')
        self.missTriggCntLineEdit.setEnabled(False)
        self.missTriggCntLineEdit.setFixedSize(100, 50)

        #self.triggBtn = QPushButton(text = 'trigg FFT')
        #self.triggBtn.setFixedSize(100, 50)
        #self.triggBtn.clicked.connect(self.triggBtnAction)            

        #self.modeCombo = QComboBox()
        #self.modeCombo.setFixedSize(100, 50)
        #self.modeCombo.currentIndexChanged.connect(self.newModeIndexChanged)
        #self.modeCombo.addItem("CONT")
        #self.modeCombo.addItem("TRIGG")
        
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
        #self.pvnNameSampleRate = self.buildPvName('SampleRate-Act') # IOC_TEST:Plugin-FFT0-SampleRate-Act
        #print("self.pvnNameSampleRate=" + self.pvnNameSampleRate)
        #self.pvnNameNFFT = self.buildPvName('NFFT') # IOC_TEST:Plugin-FFT0-NFFT
        #print("self.pvnNameNFFT=" + self.pvnNameNFFT)
        #self.pvnNameMode = self.buildPvName('Mode-RB') # IOC_TEST:Plugin-FFT0-Mode-RB
        #print("self.pvnNameMode=" + self.pvnNameMode)

        self.connectPvs()
        
        # Check actual value of pvs
        if(self.pvEnable.get() > 0):
          self.enableBtn.setStyleSheet("background-color: green")
          self.enable = True
        else:
          self.enableBtn.setStyleSheet("background-color: red")
          self.enable = False

        self.sourceStr = self.pvSource.get(as_string=True)
        #self.sampleRate = self.pvSampleRate.get()
        #self.NFFT = self.pvNFFT.get()
        
        #self.mode = self.pvMode.get()        

        #self.modeStr = "NO_MODE"
        #self.triggBtn.setEnabled(False) # Only enable if mode = TRIGG = 2
        #if self.mode == 1:
        #    self.modeStr = "CONT"
        #    self.modeCombo.setCurrentIndex(self.mode-1) # Index starta t zero

        #if self.mode == 2:
        #    self.modeStr = "TRIGG"
        #    self.triggBtn.setEnabled(True)
        #    self.modeCombo.setCurrentIndex(self.mode-1) # Index starta t zero
        
        # Fix layout
        self.setGeometry(300, 300, 900, 700)
        self.setWindowTitle("ecmc Scope Main plot: prefix=" + self.pvPrefixStr + " , scopeId=" + str(self.scopePluginId) + 
                            ", source="  + self.sourceStr) # + ", rate=" + str(self.sampleRate))

        layoutVert = QVBoxLayout()
        layoutVert.addWidget(self.toolbar) 
        layoutVert.addWidget(self.canvas) 

        layoutControl = QHBoxLayout() 
        layoutControl.addWidget(self.pauseBtn)
        layoutControl.addWidget(self.enableBtn)
        layoutControl.addWidget(self.triggCntLineEdit)
        layoutControl.addWidget(self.missTriggCntLineEdit)
    
        #layoutControl.addWidget(self.triggBtn)
        #layoutControl.addWidget(self.modeCombo)
    
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

        #if self.pvnNameSampleRate is None:
        #    raise RuntimeError("pvname sample rate must not be 'None'")
        #if len(self.pvnNameSampleRate)==0:
        #    raise RuntimeError("pvname sample rate must not be ''")
        
        #if self.pvnNameNFFT is None:
        #    raise RuntimeError("pvname NFFT must not be 'None'")
        #if len(self.pvnNameNFFT)==0:
        #    raise RuntimeError("pvname NFFT must not be ''")
        
        #if self.pvnNameMode is None:
        #    raise RuntimeError("pvname mode must not be 'None'")
        #if len(self.pvnNameMode)==0:
        #    raise RuntimeError("pvname mode must not be ''")
        
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

        #self.pvSampleRate = epics.PV(self.pvnNameSampleRate)
        #print('self.pvSampleRate: ' + self.pvSampleRate.info)

        #self.pvNFFT = epics.PV(self.pvnNameNFFT)
        #print('self.pvNFFT: ' + self.pvNFFT.info)

        #self.pvMode = epics.PV(self.pvnNameMode)
        #print('self.pvMode: ' + self.pvMode.info)        

        self.pvMissTriggCnt.add_callback(self.onChangepvMissTriggCnt)
        self.pvTriggCnt.add_callback(self.onChangepvTriggCnt)
        self.pvRawData.add_callback(self.onChangePvrawData)
        self.pvEnable.add_callback(self.onChangePvEnable)
        #self.pvMode.add_callback(self.onChangePvMode)

        QCoreApplication.processEvents()
    
    ###### Pv monitor callbacks
    #def onChangePvMode(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
    #    self.comSignalMode.data_signal.emit(value)

    def onChangePvEnable(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalEnable.data_signal.emit(value)

    def onChangepvMissTriggCnt(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalMissTriggCnt.data_signal.emit(value)

    def onChangepvTriggCnt(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalTriggCnt.data_signal.emit(value)        

    def onChangePvrawData(self,pvname=None, value=None, char_value=None,timestamp=None, **kw):
        self.comSignalRawData.data_signal.emit(value)        

    ###### Signal callbacks    
    #def callbackFuncMode(self, value):
    #    if value < 1 or value> 2:
    #        self.modeStr = "NO_MODE"
    #        print('callbackFuncMode: Error Invalid mode.')
    #        return
    #
    #    self.mode = value
    #    self.modeCombo.setCurrentIndex(self.mode-1) # Index starta t zero
    #    
    #    if self.mode == 1:
    #        self.modeStr = "CONT"
    #        self.triggBtn.setEnabled(False) # Only enable if mode = TRIGG = 2
    #                    
    #    if self.mode == 2:
    #       self.modeStr = "TRIGG"
    #       self.triggBtn.setEnabled(True)
    #            
    #    return

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

    def enableBtnAction(self):
        self.enable = not self.enable
        self.pvEnable.put(self.enable)
        if self.enable:
          self.enableBtn.setStyleSheet("background-color: green")
        else:
          self.enableBtn.setStyleSheet("background-color: red")
        return

    #def triggBtnAction(self):
    #    self.pvTrigg.put(True)
    #    return

    #def newModeIndexChanged(self,index):
    #    if index==0 or  index==1:
    #        self.pvMode.put(index+1)
    #    return

    ###### Plotting
    #def plotSpect(self):
    #    if self.pause:            
    #        return
    #    if self.missTriggCnt is None:
    #        return
    #    if self.triggCnt is None:
    #        return
    #    
    #    # print("plotSpect")
    #    # create an axis for spectrum
    #    if self.axSpect is None:
    #       self.axSpect = self.figure.add_subplot(212)
    #
    #    # plot data 
    #    if self.plottedLineSpect is not None:
    #        self.plottedLineSpect.remove()
    #
    #    self.plottedLineSpect, = self.axSpect.plot(self.missTriggCnt,self.triggCnt, 'b*-') 
    #    self.axSpect.grid(True)
    #
    #    self.axSpect.set_xlabel(self.pvNameMissTriggCnt +' [' + self.pvMissTriggCnt.units + ']')
    #    self.axSpect.set_ylabel(self.pvNameTriggCnt +' [' + self.pvTriggCnt.units + ']')
    #
    #    # refresh canvas 
    #    self.canvas.draw()
    #
    #    self.axSpect.autoscale(enable=False)

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

        self.axRaw.set_xlabel('Time [s]')
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
