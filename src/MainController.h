// Copyright (c) 2016 Electronic Theatre Controls, Inc., http://www.etcconnect.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef MAINCONTROLLER_H
#define MAINCONTROLLER_H

#include "Track.h"
#include "MultiChannelInput.h"
#include "OSCNetworkManager.h"
#include "OSCMapping.h"
#include "versionInfo.h"

#include <QObject>
#include <QDebug>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QFileInfo>
#include <QQmlComponent>
#include <QUrl>
#include <QGuiApplication>
#include <QQuickItem>
#include <QTimer>
#include <QVector>


// the rate in Hz the FFT and the triggers are updated
static const int FFT_UPDATE_RATE = 44; // Hz

// the rate in Hz the OSC level feedback is sent
static const int OSC_LEVEL_FEEDBACK_RATE = 15; // Hz


class TriggerGuiController;

// The MainController creates and manages all tracks (analysis chains),
// the multichannel audio input, OSC and presets.
// The GUI always shows and edits the "current track"; all getters / setters
// that existed in the single track version operate on this track.
class MainController : public QObject
{
	Q_OBJECT

	Q_PROPERTY(bool decibelConversion READ getDecibelConversion NOTIFY decibelConversionChanged)
	Q_PROPERTY(bool agcEnabled READ getAgcEnabled NOTIFY agcEnabledChanged)
	Q_PROPERTY(QString presetName READ getPresetName NOTIFY presetNameChanged)
	Q_PROPERTY(bool presetChangedButNotSaved READ getPresetChangedButNotSaved NOTIFY presetChangedButNotSavedChanged)
	Q_PROPERTY(bool lowSoloMode READ getLowSoloMode WRITE setLowSoloMode NOTIFY lowSoloModeChanged)
	Q_PROPERTY(bool autoBpm READ getAutoBpm WRITE setAutoBpm NOTIFY autoBpmChanged)
	Q_PROPERTY(bool waveformVisible READ getWaveformVisible WRITE setWaveformVisible NOTIFY waveformVisibleChanged)

	// ---- multitrack ----
	Q_PROPERTY(int trackCount READ getTrackCount WRITE setTrackCount NOTIFY trackCountChanged)
	Q_PROPERTY(int currentTrack READ getCurrentTrack WRITE setCurrentTrack NOTIFY currentTrackChanged)
	Q_PROPERTY(int maxTracks READ getMaxTracks CONSTANT)
	Q_PROPERTY(int inputChannelCount READ getInputChannelCount NOTIFY inputChanged)
	Q_PROPERTY(bool multitrackOscEnabled READ getMultitrackOscEnabled WRITE setMultitrackOscEnabled NOTIFY multitrackOscEnabledChanged)
	Q_PROPERTY(QString inputInfo READ getInputInfo NOTIFY inputChanged)

public:
	explicit MainController(QQmlApplicationEngine* m_qmlEngine, QObject* parent = nullptr);
	~MainController();

	// initializes everything that can or must be done before the QML file is loaded
	void initBeforeQmlIsLoaded();
	// initializes everything that depends on the loaded QML file
	void initAfterQmlIsLoaded();

signals:
	void inputChanged();
	void settingsChanged();
	void decibelConversionChanged();
	void agcEnabledChanged();
	void presetChangedButNotSavedChanged();
	void presetNameChanged();
	void gainChanged();
	void compressionChanged();
	void presetChanged();
	void lowSoloModeChanged();
	void bpmActiveChanged();
	void autoBpmChanged();
	void bpmRangeChanged();
	void waveformVisibleChanged();
	void bpmMuteChanged();

	void trackCountChanged();
	void currentTrackChanged();
	void trackNamesChanged();
	void trackChannelsChanged();
	void multitrackOscEnabledChanged();

	// forwarded signals from OSCNetworkManager:
	void messageReceived(OSCMessage msg);
	void packetSent();
	void useTcpChanged();
	void isConnectedChanged();
	void addressChanged();
	void oscLogChanged();

public slots:

	void initAudioInput();
	void activateBPM();
	void deactivateBPM();

	void onExit();
	void onVisibilityChanged();
	void savePresetIndependentSettings() const;
	void loadPresetIndependentSettings();
	void restoreWindowGeometry();

	// pumps the audio input, runs FFT + triggers on all active tracks and sends the multitrack OSC stream
	void updateFFT();
	// runs the BPM detection on all active tracks
	void updateBPM();

	// ------------------- Presets ---------------------
	void loadPreset(const QString& fileName, bool createIfNotExistent = false);
	void savePresetAs(const QString& fileName, bool isAutosave = false);
	void saveCurrentPreset();
	void autosave();
	void restorePreset();
	void resetPreset();
	void deletePreset(const QString& fileName);
	QString getPresetDirectory() const;
	QString getPresetName() const;
	bool getPresetChangedButNotSaved() const { return m_presetChangedButNotSaved; }
	void onPresetChanged();
	void openSavePresetAsDialog();
	void openLoadPresetDialog();

	// ------------------- Multitrack ---------------------
	int getTrackCount() const { return m_trackCount; }
	void setTrackCount(int value);
	int getCurrentTrack() const { return m_currentTrack; }
	void setCurrentTrack(int index);
	int getMaxTracks() const { return MAX_TRACKS; }
	int getInputChannelCount() const { return m_audioInput ? m_audioInput->getChannelCount() : 0; }
	QString getInputInfo() const;

	QString getTrackName(int index) const;
	void setTrackName(int index, const QString& name);
	int getTrackChannel(int index) const;
	void setTrackChannel(int index, int channel);

	// levels of the 6 triggers of a track [0...1]
	QList<qreal> getTrackLevels(int index) const;
	// trigger states (after delays) of the 6 triggers of a track
	QList<bool> getTrackActives(int index) const;
	// peak level of each input channel since the last call [0...1]
	QList<qreal> getChannelPeaks();
	// BPM of a track as a rounded number, 0 if none
	int getTrackBpm(int index) const;

	bool getMultitrackOscEnabled() const { return m_multitrackOscEnabled; }
	void setMultitrackOscEnabled(bool value);

	// pointer to a track (nullptr if out of range)
	Track* track(int index) const { return (index >= 0 && index < m_tracks.size()) ? m_tracks[index] : nullptr; }

	// ------------------- Console Type ---------------------
	QString getConsoleType() const { return m_consoleType; }
	void setConsoleType(QString value);

	// ------------------- Current track settings ---------------------
	bool getLowSoloMode() const { return cur()->getLowSoloMode(); }
	void setLowSoloMode(bool value) { cur()->setLowSoloMode(value); emit lowSoloModeChanged(); emit presetChanged(); }

	QList<qreal> getSpectrumPoints();
	QString getVersionString() { return VERSION_STRING; }

	// ------------------- BPM (current track) ---------------------
	void triggerBeat();
	void setBPM(float value);
	bool getBPMActive() { return cur()->m_bpmActive; }
	bool getBPMManual() { return cur()->getBpmTap().hasBpm() && !cur()->m_autoBpm; }
	void setBPMActive(bool value);
	bool getAutoBpm() const { return cur()->m_autoBpm; }
	void setAutoBpm(bool value);
	float getBPM() { return cur()->getCurrentBpm(); }
	bool bpmIsOld() { return cur()->getBpm().bpmIsOld(); }
	void setMinBPM(int value);
	int getMinBPM() { return cur()->getBpm().getMinBPM(); }
	bool getBPMMute() { return cur()->getBpmOsc().getBPMMute(); }
	void toggleBPMMute() { cur()->getBpmOsc().toggleBPMMute(); emit bpmMuteChanged(); }
	bool getWaveformVisible() { return cur()->m_waveformVisible; }
	void setWaveformVisible(bool value);
	QList<qreal> getWavePoints();
	QList<bool> getWaveOnsets();
	QList<QString> getWaveColors();
	QStringList getBPMOscCommands() { return cur()->getBpmOsc().getCommands(); }
	void setBPMOscCommands(const QStringList commands) { cur()->getBpmOsc().setCommands(commands); }

	// ------------------- Audio Input ---------------------
	QStringList getAvailableInputs() const { return m_audioInput->getAvailableInputs(); }
	QString getActiveInputName() const { return m_audioInput->getActiveInputName(); }
	void setInputByName(const QString& name);
	qreal getVolume() const { return m_audioInput->getVolume(); }
	void setVolume(const qreal& value) { m_audioInput->setVolume(value); emit presetChanged(); }

	// ------------------- FFT parameters (current track) ---------------------
	qreal getFftGain() const { return cur()->getFft().getScaledSpectrum().getGain(); }
	void setFftGain(const qreal& value) { cur()->getFft().getScaledSpectrum().setGain(value); emit gainChanged(); emit presetChanged(); }
	qreal getFftCompression() const { return cur()->getFft().getScaledSpectrum().getCompression(); }
	void setFftCompression(const qreal& value) { cur()->getFft().getScaledSpectrum().setCompression(value); emit compressionChanged(); emit presetChanged(); }
	bool getDecibelConversion() const { return cur()->getFft().getScaledSpectrum().getDecibelConversion(); }
	void setDecibelConversion(bool value) { cur()->getFft().getScaledSpectrum().setDecibelConversion(value); emit decibelConversionChanged(); emit presetChanged(); }
	bool getAgcEnabled() const { return cur()->getFft().getScaledSpectrum().getAgcEnabled(); }
	void setAgcEnabled(bool value) { cur()->getFft().getScaledSpectrum().setAgcEnabled(value); emit agcEnabledChanged(); emit presetChanged(); }

	// ------------------- OSC ---------------------
	QString getOscIpAddress() const { return m_osc.getIpAddress().toString(); }
	void setOscIpAddress(const QString& value) { m_osc.setIpAddress(QHostAddress(value)); emit settingsChanged(); }
	quint16 getOscUdpTxPort() const { return m_osc.getUdpTxPort(); }
	void setOscUdpTxPort(const quint16& value) { m_osc.setUdpTxPort(value); emit settingsChanged(); }
	quint16 getOscUdpRxPort() const { return m_osc.getUdpRxPort(); }
	void setOscUdpRxPort(const quint16& value) { m_osc.setUdpRxPort(value); emit settingsChanged(); }
	quint16 getOscTcpPort() const { return m_osc.getTcpPort(); }
	void setOscTcpPort(const quint16& value) { m_osc.setTcpPort(value); emit settingsChanged(); }
	bool getOscEnabled() const { return m_osc.getEnabled(); }
	void setOscEnabled(bool value);
	bool getUseTcp() const { return m_osc.getUseTcp(); }
	void setUseTcp(bool value) { m_osc.setUseTcp(value); }
	bool getUseOsc_1_1() const { return m_osc.getUseOsc_1_1(); }
	void setUseOsc_1_1(bool value) { m_osc.setUseOsc_1_1(value); }
	bool isConnected() const { return m_osc.isConnected(); }
	QStringList getOscLog() const { return m_osc.getLog(); }
	bool getOscLogIncomingIsEnabled() const { return m_osc.getLogIncomingIsEnabled(); }
	bool getOscLogOutgoingIsEnabled() const { return m_osc.getLogOutgoingIsEnabled(); }
	void enableOscLogging(bool incoming, bool outgoing) { m_osc.enableLogging(incoming, outgoing); }
	void sendOscMessage(QString message, bool forced) { m_osc.sendMessage(message, forced); }
	void sendOscMessage(QString path, QString argument, bool forced) { m_osc.sendMessage(path, argument, forced); }
	void clearOscLog() const { m_osc.clearLog(); }
	bool getOscInputEnabled() const { return m_oscMapping.getInputEnabled(); }
	void setOscInputEnabled(bool value) { m_oscMapping.setInputEnabled(value); }
	void sendOscTestMessage(QString message);

	// ------------------- GUI helpers ---------------------
	void openDialog(const QString& qmlDialogFile, QString propertyName = "", QVariant propertyValue = QVariant());
	void dialogIsClosed(QObject* object);
	bool controlIsPressed() const { return QGuiApplication::keyboardModifiers() & Qt::ControlModifier; }
	QRect getWindowGeometry() const { return getMainWindow()->geometry(); }
	QRect getWindowGeometryOfItem(const QVariant& item) const { return item.value<QQuickItem*>()->window()->geometry(); }
	void setPropertyWithoutChangingBindings(const QVariant& item, QString name, QVariant value);

	// ------------------- OSC Level Feedback ---------------------
	bool oscLevelFeedbackIsEnabled();
	void enableOscLevelFeedback(bool value);

private:
	Track* cur() const { return m_tracks[m_currentTrack]; }
	// publishes the trigger controllers of the current track to QML and notifies the GUI
	void bindCurrentTrackToGui();
	void notifyGuiOfTrackSettings();
	QQuickWindow* getMainWindow() const;
	bool settingsFormatIsValid(QSettings& settings) const;

private slots:
	void onConnectedChanged();

public:  // to allow access from OSCMapping class without getters
	// controllers of the CURRENT track:
	TriggerGuiController* m_bassController;
	TriggerGuiController* m_loMidController;
	TriggerGuiController* m_hiMidController;
	TriggerGuiController* m_highController;
	TriggerGuiController* m_envelopeController;
	TriggerGuiController* m_silenceController;

protected:
	QQmlApplicationEngine*		m_qmlEngine;
	OSCNetworkManager			m_osc;
	QVector<Track*>				m_tracks;  // all MAX_TRACKS tracks (only the first m_trackCount are processed)
	int							m_trackCount;
	int							m_currentTrack;
	bool						m_multitrackOscEnabled;
	MultiChannelInput*			m_audioInput;
	QString						m_consoleType;
	QTimer						m_fftUpdateTimer;
	QString						m_currentPresetFilename;
	bool						m_presetChangedButNotSaved;
	QMap<QString, QObject*>		m_dialogs;
	OSCMapping					m_oscMapping;
	QTimer						m_oscUpdateTimer;
	QTimer						m_bpmUpdatetimer;
};

#endif // MAINCONTROLLER_H
