######################################################################
# Generated by Jens Krueger
######################################################################

TEMPLATE          = app
win32:TEMPLATE    = vcapp
CONFIG           += exceptions largefile link_prl qt static stl warn_on
macx:DEFINES     += QT_MAC_USE_COCOA=1
TARGET            = ../Build/ImageVis3D
RCC_DIR           = ../Build/rcc
OBJECTS_DIR       = ../Build/objects
UI_DIR            = UI/AutoGen
MOC_DIR           = UI/AutoGen
DEPENDPATH       += . ../Tuvok/Basics ../Tuvok/Controller Tuvok/DebugOut
DEPENDPATH       += ../Tuvok/IO ../Tuvok/Renderer ../Tuvok/Scripting
DEPENDPATH       += ../Tuvok/Build
DEPENDPATH       += DebugOut IO UI UI/AutoGen
INCLUDEPATH      += . ../Tuvok/IO/3rdParty/boost ../Tuvok/3rdParty/GLEW ../Tuvok
QT               += opengl network
LIBS              = -L../Tuvok/Build -lTuvok
RESOURCES         = ImageVis3D.qrc
RC_FILE 	  = Resources/ImageVis3D.rc
QMAKE_INFO_PLIST  = ../IV3D.plist
ICON              = Resources/ImageVis3D.icns
macx:LIBS        +=-framework CoreFoundation
unix:QMAKE_CXXFLAGS += -fno-strict-aliasing
unix:QMAKE_CFLAGS += -fno-strict-aliasing

# Find the location of QtGui's prl file, and include it here so we can look at
# the QMAKE_PRL_CONFIG variable.
TEMP = $$[QT_INSTALL_LIBS] libQtGui.prl
PRL  = $$[QT_INSTALL_LIBS] QtGui.framework/QtGui.prl
TEMP = $$join(TEMP, "/")
PRL  = $$join(PRL, "/")
exists($$TEMP) {
  include($$TEMP)
}
exists($$PRL) {
  include($$PRL)
}

### Should we link Qt statically or as a shared lib?
# If the PRL config contains the `shared' configuration, then the installed
# Qt is shared.  In that case, disable the image plugins.
contains(QMAKE_PRL_CONFIG, shared) {
  message("Shared build, ensuring there will be image plugins linked in.")
  QTPLUGIN -= qgif qjpeg qtiff
} else {
  message("Static build, forcing image plugins to get loaded.")
  QTPLUGIN += qgif qjpeg qtiff
}

# Input
HEADERS += StdDefines.h \
           UI/SettingsDlg.h \
           UI/BrowseData.h \
           UI/ImageVis3D.h \
           UI/PleaseWait.h \
           UI/FTPDialog.h \
           UI/QTransferFunction.h \
           UI/Q1DTransferFunction.h \
           UI/Q2DTransferFunction.h \
           UI/QDataRadioButton.h \
           UI/QLightPreview.h \
           UI/RenderWindow.h \
           UI/RenderWindowGL.h \
           UI/RAWDialog.h \
           UI/MIPRotDialog.h \ 
           UI/Welcome.h \
           UI/MetadataDlg.h \
           UI/I3MDialog.h \           
           UI/AboutDlg.h \
           UI/URLDlg.h \
           UI/BugRepDlg.h \           
           UI/LODDlg.h \
           UI/MergeDlg.h \
           UI/CrashDetDlg.h \
           UI/DatasetServerDialog.h \
           DatasetServer/DatasetServer.h \          
           DebugOut/QTOut.h \
           DebugOut/QTLabelOut.h \
           IO/DialogConverter.h

FORMS += UI/UI/BrowseData.ui \
         UI/UI/ImageVis3D.ui \
         UI/UI/PleaseWait.ui \
         UI/UI/SettingsDlg.ui \
         UI/UI/RAWDialog.ui \
         UI/UI/FTPDialog.ui \
         UI/UI/Welcome.ui \
         UI/UI/Metadata.ui \
         UI/UI/CrashDetDlg.ui \
         UI/UI/About.ui \
         UI/UI/I3MDialog.ui \
         UI/UI/URLDlg.ui \
         UI/UI/LODDlg.ui \
         UI/UI/BugRepDlg.ui \
         UI/UI/MIPRotDialog.ui \
         UI/UI/MergeDlg.ui

SOURCES += UI/BrowseData.cpp \
           UI/ImageVis3D.cpp \
           UI/ImageVis3D_Capturing.cpp \
           UI/ImageVis3D_Progress.cpp \
           UI/ImageVis3D_1DTransferFunction.cpp \
           UI/ImageVis3D_2DTransferFunction.cpp \
           UI/ImageVis3D_FileHandling.cpp \
           UI/ImageVis3D_WindowHandling.cpp \
           UI/ImageVis3D_DebugWindow.cpp \
           UI/ImageVis3D_Settings.cpp \
           UI/ImageVis3D_Locking.cpp \
           UI/ImageVis3D_Stereo.cpp \
           UI/ImageVis3D_Scripting.cpp \
           UI/ImageVis3D_Help.cpp \
           UI/ImageVis3D_I3M.cpp \
           UI/PleaseWait.cpp \
           UI/Welcome.cpp \
           UI/MetadataDlg.cpp \
           UI/I3MDialog.cpp \
           UI/AboutDlg.cpp \
           UI/URLDlg.cpp \
           UI/FTPDialog.cpp \
           UI/BugRepDlg.cpp \
           UI/LODDlg.cpp \           
           UI/QTransferFunction.cpp \
           UI/Q1DTransferFunction.cpp \
           UI/Q2DTransferFunction.cpp \
           UI/QDataRadioButton.cpp \
           UI/QLightPreview.cpp \          
           UI/RenderWindowGL.cpp \
           UI/RenderWindow.cpp \
           UI/SettingsDlg.cpp \
           UI/RAWDialog.cpp \
           UI/MIPRotDialog.cpp \           
           UI/MergeDlg.cpp \
           UI/CrashDetDlg.cpp \
           UI/DatasetServerDialog.cpp \
           DatasetServer/DatasetServer.cpp \           
           DebugOut/QTOut.cpp \
           DebugOut/QTLabelOut.cpp \
           IO/DialogConverter.cpp \
           main.cpp

win32 {
  HEADERS += UI/RenderWindowDX.h
  SOURCES += UI/RenderWindowDX.cpp
}
