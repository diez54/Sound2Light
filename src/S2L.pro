TARGET = s2l-multipiste
LANGUAGE = C++

TEMPLATE = app

QT += qml quick quickwidgets network widgets

CONFIG += c++11 thread

QMAKE_CXXFLAGS += -Wall

SOURCES += main.cpp \
    FFTAnalyzer.cpp \
    MainController.cpp \
    MonoAudioBuffer.cpp \
    MultiChannelInput.cpp \
    Track.cpp \
    rtaudio/RtAudio.cpp \
    ScaledSpectrum.cpp \
    TriggerFilter.cpp \
    OSCParser.cpp \
    TriggerGenerator.cpp \
    TriggerGuiController.cpp \
    TriggerOscParameters.cpp \
    OSCMessage.cpp \
    OSCMapping.cpp \
    OSCNetworkManager.cpp \
    BPMDetector.cpp \
    BPMOscControler.cpp \
    BPMTapDetector.cpp

RESOURCES += qml.qrc \
    images.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
# include(deployment.pri)

HEADERS += \
    ffft/Array.h \
    ffft/Array.hpp \
    ffft/def.h \
    ffft/DynArray.h \
    ffft/DynArray.hpp \
    ffft/FFTReal.h \
    ffft/FFTReal.hpp \
    ffft/FFTRealFixLen.h \
    ffft/FFTRealFixLen.hpp \
    ffft/FFTRealFixLenParam.h \
    ffft/FFTRealPassDirect.h \
    ffft/FFTRealPassDirect.hpp \
    ffft/FFTRealPassInverse.h \
    ffft/FFTRealPassInverse.hpp \
    ffft/FFTRealSelect.h \
    ffft/FFTRealSelect.hpp \
    ffft/FFTRealUseTrigo.h \
    ffft/FFTRealUseTrigo.hpp \
    ffft/OscSinCos.h \
    ffft/OscSinCos.hpp \
    BasicFFTInterface.h \
    FFTAnalyzer.h \
    FFTRealWrapper.h \
    MainController.h \
    MonoAudioBuffer.h \
    MultiChannelInput.h \
    Track.h \
    rtaudio/RtAudio.h \
    ScaledSpectrum.h \
    TriggerGeneratorInterface.h \
    TriggerFilter.h \
    OSCParser.h \
    utils.h \
    TriggerGenerator.h \
    TriggerGuiController.h \
    TriggerOscParameters.h \
    versionInfo.h \
    OSCMessage.h \
    OSCMapping.h \
    OSCNetworkManager.h \
    QCircularBuffer.h \
    BPMDetector.h \
    BPMOscControler.h \
    BPMTapDetector.h

# ------------------------ Audio backend (RtAudio) ------------------------
INCLUDEPATH += rtaudio

macx {
    DEFINES += __MACOSX_CORE__
    LIBS += -framework CoreAudio -framework CoreFoundation -framework AudioToolbox
    QMAKE_INFO_PLIST = $$PWD/macos/Info.plist
    ICON = $$PWD/images/icons/etcicon.icns
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
}

win32 {
    DEFINES += __WINDOWS_WASAPI__
    LIBS += -lole32 -lwinmm -lksuser -lmfplat -lmfuuid -lwmcodecdspuuid -luuid
    RC_ICONS = images/icons/etcicon.ico
    # ASIO (i.e. Dante Virtual Soundcard in ASIO mode): build with "qmake CONFIG+=asio"
    # Note: the Steinberg ASIO SDK files are GPLv3 / Steinberg licensed.
    asio {
        DEFINES += __WINDOWS_ASIO__
        INCLUDEPATH += rtaudio/include
        SOURCES += rtaudio/include/asio.cpp \
                   rtaudio/include/asiodrivers.cpp \
                   rtaudio/include/asiolist.cpp \
                   rtaudio/include/iasiothiscallresolver.cpp
        LIBS += -ladvapi32 -luser32
    }
}

unix:!macx {
    DEFINES += __LINUX_ALSA__
    LIBS += -lasound -lpthread
    packagesExist(libpulse-simple) {
        DEFINES += __LINUX_PULSE__
        LIBS += -lpulse-simple -lpulse
    }
}

DISTFILES += \
    LICENSE.txt \
    README.md
