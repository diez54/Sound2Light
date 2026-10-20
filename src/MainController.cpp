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

#include "MainController.h"

#include "TriggerGenerator.h"
#include "TriggerGuiController.h"
#include "OSCNetworkManager.h"

#include <QtMath>
#include <QQmlContext>
#include <QSettings>
#include <QQuickItem>
#include <QQmlComponent>
#include <QStandardPaths>
#include <QDateTime>
#include <QScreen>
#include <QFileDialog>


MainController::MainController(QQmlApplicationEngine* qmlEngine, QObject *parent)
	: QObject(parent)
	, m_bassController(nullptr)
	, m_loMidController(nullptr)
	, m_hiMidController(nullptr)
	, m_highController(nullptr)
	, m_envelopeController(nullptr)
	, m_silenceController(nullptr)
	, m_qmlEngine(qmlEngine)
	, m_osc()
	, m_tracks()
	, m_trackCount(5)
	, m_currentTrack(0)
	, m_multitrackOscEnabled(true)
	, m_audioInput(nullptr)
	, m_consoleType("Eos")
	, m_presetChangedButNotSaved(false)
	, m_oscMapping(this)
{
	m_audioInput = new MultiChannelInput(this);

	// create all tracks up front (only the first m_trackCount are processed):
	for (int i=0; i<MAX_TRACKS; ++i) {
		Track* t = new Track(i, &m_osc);
		m_tracks.append(t);
		for (int k=0; k<NUM_TRIGGERS; ++k) {
			connect(t->getController(k), SIGNAL(presetChanged()), this, SLOT(onPresetChanged()));
		}
	}

	bindCurrentTrackToGui();
}

MainController::~MainController()
{
	m_fftUpdateTimer.stop();
	m_bpmUpdatetimer.stop();
	delete m_audioInput; m_audioInput = nullptr;
	qDeleteAll(m_tracks);
	m_tracks.clear();
}

void MainController::initBeforeQmlIsLoaded()
{
	// init everything that can or must be done before QML file is loaded:
	connect(&m_osc, SIGNAL(messageReceived(OSCMessage)), this, SIGNAL(messageReceived(OSCMessage)));
	connect(&m_osc, SIGNAL(packetSent()), this, SIGNAL(packetSent()));
	connect(&m_osc, SIGNAL(useTcpChanged()), this, SIGNAL(useTcpChanged()));
	connect(&m_osc, SIGNAL(isConnectedChanged()), this, SIGNAL(isConnectedChanged()));
	connect(&m_osc, SIGNAL(isConnectedChanged()), this, SLOT(onConnectedChanged()));
	connect(&m_osc, SIGNAL(addressChanged()), this, SIGNAL(addressChanged()));
	connect(&m_osc, SIGNAL(logChanged()), this, SIGNAL(oscLogChanged()));
	connect(&m_osc, SIGNAL(messageReceived(OSCMessage)), &m_oscMapping, SLOT(handleMessage(OSCMessage)));
	connect(&m_oscUpdateTimer, SIGNAL(timeout()), &m_oscMapping, SLOT(sendLevelFeedback()));

	// load settings that may be used while QML file is loaded:
	loadPresetIndependentSettings();
	restorePreset();
}

void MainController::initAfterQmlIsLoaded()
{
	// init things that depend on the loaded QML file:
	initAudioInput();
	restoreWindowGeometry();
	autosave();

	// create global QML property that points to the main Window:
	m_qmlEngine->rootContext()->setContextProperty("mainWindow", QVariant::fromValue<QQuickWindow*>(getMainWindow()));

	connect(this, SIGNAL(presetChanged()), this, SLOT(onPresetChanged()));
	connect(getMainWindow(), SIGNAL(visibilityChanged(QWindow::Visibility)), this, SLOT(onVisibilityChanged()));
}

// ------------------------------- Multitrack -----------------------------------

void MainController::bindCurrentTrackToGui()
{
	Track* t = cur();
	m_bassController = t->getController(0);
	m_loMidController = t->getController(1);
	m_hiMidController = t->getController(2);
	m_highController = t->getController(3);
	m_envelopeController = t->getController(4);
	m_silenceController = t->getController(5);

	// make the controllers accessible in QML, i.e. as "bassController":
	QQmlContext* ctx = m_qmlEngine->rootContext();
	ctx->setContextProperty("bassController", m_bassController);
	ctx->setContextProperty("loMidController", m_loMidController);
	ctx->setContextProperty("hiMidController", m_hiMidController);
	ctx->setContextProperty("highController", m_highController);
	ctx->setContextProperty("envelopeController", m_envelopeController);
	ctx->setContextProperty("silenceController", m_silenceController);
}

void MainController::notifyGuiOfTrackSettings()
{
	emit decibelConversionChanged();
	emit agcEnabledChanged();
	emit gainChanged();
	emit compressionChanged();
	emit lowSoloModeChanged();
	emit bpmActiveChanged();
	emit autoBpmChanged();
	emit bpmRangeChanged();
	emit waveformVisibleChanged();
	emit bpmMuteChanged();

	for (int k=0; k<NUM_TRIGGERS; ++k) {
		TriggerGuiController* c = cur()->getController(k);
		emit c->parameterChanged();
		emit c->oscLabelTextChanged();
		emit c->muteChanged();
		emit c->activeChanged();
	}
}

void MainController::setTrackCount(int value)
{
	value = qBound(1, value, MAX_TRACKS);
	if (value == m_trackCount) return;
	m_trackCount = value;
	if (m_currentTrack >= m_trackCount) setCurrentTrack(m_trackCount - 1);
	emit trackCountChanged();
	emit presetChanged();
	m_osc.sendMessage("/s2l/out/track_count", QString::number(m_trackCount), true);
}

void MainController::setCurrentTrack(int index)
{
	index = qBound(0, index, m_trackCount - 1);
	if (index == m_currentTrack) return;
	m_currentTrack = index;
	bindCurrentTrackToGui();
	notifyGuiOfTrackSettings();
	emit currentTrackChanged();
	m_osc.sendMessage("/s2l/out/track", QString::number(m_currentTrack + 1), true);
}

QString MainController::getInputInfo() const
{
	if (!m_audioInput || m_audioInput->getChannelCount() <= 0) {
		QString err = m_audioInput ? m_audioInput->getLastError() : QString();
		return err.isEmpty() ? QString("Pas d'entrée audio") : QString("Erreur audio : %1").arg(err);
	}
	QString info = QString("%1 · %2 canaux · %3 Hz")
			.arg(m_audioInput->getApiName())
			.arg(m_audioInput->getChannelCount())
			.arg(m_audioInput->getDeviceSampleRate());
	if (m_audioInput->getDeviceSampleRate() != ANALYSIS_SAMPLE_RATE) info += " → 44100";
	return info;
}

QString MainController::getTrackName(int index) const
{
	Track* t = track(index);
	if (!t) return QString();
	return t->getName().isEmpty() ? t->getDefaultName() : t->getName();
}

void MainController::setTrackName(int index, const QString& name)
{
	Track* t = track(index);
	if (!t) return;
	t->setName(name);
	emit trackNamesChanged();
	emit presetChanged();
}

int MainController::getTrackChannel(int index) const
{
	Track* t = track(index);
	return t ? t->getInputChannel() : 0;
}

void MainController::setTrackChannel(int index, int channel)
{
	Track* t = track(index);
	if (!t) return;
	t->setInputChannel(channel);
	emit trackChannelsChanged();
	emit presetChanged();
}

QList<qreal> MainController::getTrackLevels(int index) const
{
	QList<qreal> levels;
	Track* t = track(index);
	if (!t) return levels;
	for (int k=0; k<NUM_TRIGGERS; ++k) levels.append(t->getLevel(k));
	return levels;
}

QList<bool> MainController::getTrackActives(int index) const
{
	QList<bool> actives;
	Track* t = track(index);
	if (!t) return actives;
	for (int k=0; k<NUM_TRIGGERS; ++k) actives.append(t->getTriggerActive(k));
	return actives;
}

QList<qreal> MainController::getChannelPeaks()
{
	QList<qreal> peaks;
	for (float p : m_audioInput->takeChannelPeaks()) peaks.append(p);
	return peaks;
}

int MainController::getTrackBpm(int index) const
{
	Track* t = track(index);
	if (!t || !(t->m_bpmActive || t->getBpmTap().hasBpm())) return 0;
	return qRound(t->getCurrentBpm());
}

void MainController::setMultitrackOscEnabled(bool value)
{
	if (value == m_multitrackOscEnabled) return;
	m_multitrackOscEnabled = value;
	emit multitrackOscEnabledChanged();
	if (value) {
		for (int i=0; i<m_trackCount; ++i) m_tracks[i]->sendMultitrackOutput(m_osc, true);
	}
}

// ------------------------------- Processing -----------------------------------

void MainController::updateFFT()
{
	const QVector<Track*> active = m_tracks.mid(0, m_trackCount);
	m_audioInput->pump(active);
	for (Track* t : active) {
		t->updateFFT();
	}
	if (m_multitrackOscEnabled && m_osc.getEnabled()) {
		for (Track* t : active) {
			t->sendMultitrackOutput(m_osc);
		}
	}
}

void MainController::updateBPM()
{
	for (int i=0; i<m_trackCount; ++i) {
		m_tracks[i]->updateBPM();
	}
}

QQuickWindow *MainController::getMainWindow() const
{
	QQuickWindow* window = qobject_cast<QQuickWindow*>(m_qmlEngine->rootObjects()[0]);
	return window;
}

bool MainController::settingsFormatIsValid(QSettings &settings) const
{
	// check format version:
	int formatVersion = settings.value("formatVersion").toInt();
	if (formatVersion == 0) {
		// this is the first start of the software, nothing to restore
		qDebug() << "this is the first start of the software, nothing to restore";
		return false;
	} else if (formatVersion < SETTINGS_FORMAT_VERSION) {
		// the format of the settings is too old, cannot restore
		qDebug() << "the format of the settings is too old, cannot restore";
		return false;
	}
	return true;
}

void MainController::onConnectedChanged()
{
	// send current state via OSC if a new connection was made:
	if (m_osc.isConnected()) {
		m_oscMapping.sendCurrentState();
	}
}

void MainController::initAudioInput()
{
	QString inputDeviceName;
	// get input device name from settings:
	QSettings settings;
	if (settingsFormatIsValid(settings)) {
		inputDeviceName = settings.value("inputDeviceName").toString();
	}
	// if it is empty, set it to default input:
	if (inputDeviceName.isEmpty()) {
		inputDeviceName = m_audioInput->getDefaultInputName();
	}
	if (inputDeviceName.isEmpty()) {
		// if there is no default input, open NoInputDeviceDialog:
		openDialog("qrc:/qml/NoInputDeviceDialog.qml");
	} else {
		// otherwise set the new input by its name:
		m_audioInput->setInputByName(inputDeviceName);
		emit inputChanged();
	}

	// start FFT update timer:
	connect(&m_fftUpdateTimer, SIGNAL(timeout()), this, SLOT(updateFFT()), Qt::UniqueConnection);
	m_fftUpdateTimer.setTimerType(Qt::PreciseTimer);
	m_fftUpdateTimer.start(1000.0 / FFT_UPDATE_RATE);

	// the BPM timer runs all the time, each track decides if it detects BPM:
	connect(&m_bpmUpdatetimer, SIGNAL(timeout()), this, SLOT(updateBPM()), Qt::UniqueConnection);
	m_bpmUpdatetimer.start(1000.0 / BPM_UPDATE_RATE);
}

void MainController::setInputByName(const QString& name)
{
	m_audioInput->setInputByName(name);
	emit inputChanged();
	emit presetChanged();
}

// ------------------------------- BPM (current track) -----------------------------------

void MainController::triggerBeat()
{
	setAutoBpm(false);
	cur()->getBpmTap().triggerBeat();
}

void MainController::setBPM(float value)
{
	setAutoBpm(false);
	cur()->getBpmTap().setBpm(value);
}

void MainController::activateBPM()
{
	cur()->getBpm().resetCache();
	cur()->getBpmTap().reset();
}

void MainController::deactivateBPM()
{
	// nothing to do: the track stops detecting when m_bpmActive is false
}

void MainController::setConsoleType(QString value)
{
	if (value.isEmpty()) return;
	if (value == "EOS") value = "Eos";
	if (value == "Cobalt") value = "Cobalt 7.2";
	m_consoleType = value;
	emit settingsChanged();
	emit presetChanged();
}

QList<qreal> MainController::getSpectrumPoints()
{
	// convert const QVector<float>& to QList<qreal> to be used in GUI:
	QList<qreal> points;
	const QVector<float>& spectrum = cur()->getFft().getNormalizedSpectrum();
	for (int i=0; i<spectrum.size(); ++i) {
		points.append(spectrum[i]);
	}
	return points;
}

QList<qreal> MainController::getWavePoints()
{
	// convert const QVector<float>& to QList<qreal> to be used in GUI:
	QList<qreal> points;
	const Qt3DCore::QCircularBuffer<float>& wave = cur()->getBpm().getWaveDisplay();
	const qreal gain = cur()->getFft().getScaledSpectrum().getGain();
	for (int i = 0; i < wave.size(); ++i) {
		points.append(wave.at(i) / 350 * gain);
	}
	return points;
}

QList<bool> MainController::getWaveOnsets()
{
	QList<bool> points;
	const QVector<bool>& peaks = cur()->getBpm().getOnsets();
	for (int i = 0; i < peaks.size(); ++i) {
		points.append(peaks[i]);
	}
	return points;
}

QList<QString> MainController::getWaveColors()
{
	QList<QString> points;
	const Qt3DCore::QCircularBuffer<QColor>& colors = cur()->getBpm().getWaveColors();
	for (int i = 0; i < colors.size(); ++i) {
		points.append(colors.at(i).name());
	}
	return points;
}

void MainController::setOscEnabled(bool value) {
	m_osc.setEnabled(value); emit settingsChanged();
	m_osc.sendMessage(QString("/s2l/out/enabled=").append(value ? "1" : "0"), true);
}

// enable or disables bpm detection of the current track
void MainController::setBPMActive(bool value) {
	Track* t = cur();
	if (value == t->m_bpmActive) return;
	t->m_bpmActive = value;
	if (value) activateBPM();
	emit bpmActiveChanged();
	emit waveformVisibleChanged(); // because wavformvisible is only true if bpm is active
	m_osc.sendMessage(QString("/s2l/out/ch%1/bpm/enabled").arg(t->getIndex() + 1), (value ? "1" : "0"), true);
}

void MainController::setAutoBpm(bool value) {
	Track* t = cur();
	if (value == t->m_autoBpm) return;
	t->m_autoBpm = value;
	t->getBpm().setTransmitBpm(value);
	if (t->m_autoBpm && !t->m_bpmActive) {
		setBPMActive(true);
	} else if (!t->m_autoBpm && !t->m_waveformVisible && t->m_bpmActive) {
		setBPMActive(false);
	}
	emit autoBpmChanged();
}

// sets the minium bpm of the range
void MainController::setMinBPM(int value) {
	cur()->getBpm().setMinBPM(value);
	cur()->getBpmTap().setMinBPM(value);
	emit bpmRangeChanged();
	m_osc.sendMessage(QString("/s2l/out/ch%1/bpm/range").arg(cur()->getIndex() + 1), QString::number(value), true);
}

void MainController::setWaveformVisible(bool value) {
	Track* t = cur();
	t->m_waveformVisible = value;
	if (t->m_waveformVisible && !t->m_bpmActive) {
		setBPMActive(true);
	} else if (!t->m_autoBpm && !t->m_waveformVisible && t->m_bpmActive) {
		setBPMActive(false);
	}
	emit waveformVisibleChanged();
}

void MainController::onExit()
{
	savePresetIndependentSettings();
	autosave();
}

void MainController::onVisibilityChanged()
{
	// if main Window is minimized:
	if (getMainWindow()->visibility() == QWindow::Minimized) {
		// close all open dialogs (QML dialogs can't be minimized):
		QMap<QString, QObject*>::iterator i = m_dialogs.begin();
		for (; i != m_dialogs.end(); ++i) {
			if (!i.value()) continue;
			QMetaObject::invokeMethod(i.value(), "close");
		}
	}
}

void MainController::savePresetIndependentSettings() const
{
	QSettings independentSettings;
	// save preset independent settings:
	independentSettings.setValue("version", VERSION_STRING);
	independentSettings.setValue("formatVersion", SETTINGS_FORMAT_VERSION);
	independentSettings.setValue("changedAt", QDateTime::currentDateTime().toString());
	independentSettings.setValue("oscIpAddress", getOscIpAddress());
	independentSettings.setValue("oscTxPort", getOscUdpTxPort());
	independentSettings.setValue("oscRxPort", getOscUdpRxPort());
	independentSettings.setValue("oscTcpPort", getOscTcpPort());
	independentSettings.setValue("oscIsEnabled", getOscEnabled());
	independentSettings.setValue("oscUseTcp", getUseTcp());
	independentSettings.setValue("oscUse_1_1", getUseOsc_1_1());
	independentSettings.setValue("multitrackOscEnabled", m_multitrackOscEnabled);
	QRect windowGeometry = getMainWindow()->geometry();
	if (windowGeometry.width() < 300) {
		// -> minimal mode, save default geometry
		windowGeometry.setWidth(1200);
		windowGeometry.setHeight(860);
	}
	independentSettings.setValue("windowGeometry", windowGeometry);
	bool maximized = (getMainWindow()->width() == QGuiApplication::primaryScreen()->availableSize().width());
	independentSettings.setValue("maximized", maximized);
	independentSettings.setValue("inputDeviceName", getActiveInputName());
	independentSettings.setValue("presetFileName", m_currentPresetFilename);
	independentSettings.setValue("presetChangedButNotSaved", m_presetChangedButNotSaved);
	independentSettings.setValue("oscLogSettingsValid", true);
	independentSettings.setValue("oscLogIncomingIsEnabled", getOscLogIncomingIsEnabled());
	independentSettings.setValue("oscLogOutgoingIsEnabled", getOscLogOutgoingIsEnabled());
	independentSettings.setValue("oscInputEnabledValid", true);
	independentSettings.setValue("oscInputEnabled", getOscInputEnabled());
}

void MainController::loadPresetIndependentSettings()
{
	QSettings independentSettings;
	// do not restore anything if the format is not valid (or the file is new):
	if (!settingsFormatIsValid(independentSettings)) return;

	// restore preset independent settings:
	setOscIpAddress(independentSettings.value("oscIpAddress").toString());
	setOscUdpTxPort(independentSettings.value("oscTxPort").toInt());
	setOscUdpRxPort(independentSettings.value("oscRxPort", 8000).toInt());
	setOscTcpPort(independentSettings.value("oscTcpPort", 3032).toInt());
	setOscEnabled(independentSettings.value("oscIsEnabled").toBool());
	setUseTcp(independentSettings.value("oscUseTcp").toBool());
	setUseOsc_1_1(independentSettings.value("oscUse_1_1").toBool());
	setMultitrackOscEnabled(independentSettings.value("multitrackOscEnabled", true).toBool());
	if (independentSettings.value("oscLogSettingsValid").toBool()) {
		enableOscLogging(independentSettings.value("oscLogIncomingIsEnabled").toBool(), independentSettings.value("oscLogOutgoingIsEnabled").toBool());
	} else {
		enableOscLogging(true, true);
	}
	if (independentSettings.value("oscInputEnabledValid").toBool()) {
		setOscInputEnabled(independentSettings.value("oscInputEnabled").toBool());
	} else {
		setOscInputEnabled(true);
	}
}

void MainController::restoreWindowGeometry()
{
	QSettings independentSettings;
	// do not restore anything if the format is not valid (or the file is new):
	if (!settingsFormatIsValid(independentSettings)) return;

	QRect windowGeometry = independentSettings.value("windowGeometry").toRect();
	bool maximized = independentSettings.value("maximized").toBool();
	QQuickWindow* window = getMainWindow();
	if (!window) return;
	if (!windowGeometry.isNull()) window->setGeometry(windowGeometry);
	if (maximized) window->showMaximized();
	connect(window, SIGNAL(closing(QQuickCloseEvent*)), m_qmlEngine, SIGNAL(quit()));
}

// ------------------------------- Presets -----------------------------------

void MainController::loadPreset(const QString &constFileName, bool createIfNotExistent)
{
	// prepare the filename:
	QString fileName(constFileName);
	// fileName returned from FileDialog starts with "file:///"
	if (fileName.startsWith("file:///")) {
#ifdef Q_OS_WIN
		fileName = fileName.remove(0, 8);
#else
		fileName = fileName.remove(0, 7);
#endif
	}

	// check if file exists:
	if (!createIfNotExistent && !QFile(fileName).exists()) {
		m_osc.sendMessage("/s2l/out/error", QString("Preset does not exist: ").append(fileName), true);
		return;
	}

	// loads the file or creates it if not existent:
	QSettings settings(fileName, QSettings::IniFormat);
	// do not restore anything if the format is not valid (or the file is new):
	if (!settingsFormatIsValid(settings)) return;

	setConsoleType(settings.value("consoleType").toString());

	if (settings.value("multitrackFormat", 0).toInt() >= 1) {
		// --- multitrack preset ---
		m_trackCount = qBound(1, settings.value("trackCount", 5).toInt(), MAX_TRACKS);
		for (int i=0; i<MAX_TRACKS; ++i) {
			const QString group = QString("track%1").arg(i + 1);
			if (settings.childGroups().contains(group)) {
				settings.beginGroup(group);
				m_tracks[i]->restore(settings);
				settings.endGroup();
			} else {
				m_tracks[i]->reset();
			}
		}
		const int current = qBound(0, settings.value("currentTrack", 0).toInt(), m_trackCount - 1);
		m_currentTrack = current;
	} else {
		// --- preset of the original single track Sound2Light: goes to track 1 ---
		for (int i=1; i<MAX_TRACKS; ++i) m_tracks[i]->reset();
		m_tracks[0]->restore(settings);
		m_tracks[0]->setInputChannel(-1);  // original behaviour: mix of all channels
		m_trackCount = 1;
		m_currentTrack = 0;
	}

	bindCurrentTrackToGui();

	// this is now the loaded preset, update the preset name:
	m_currentPresetFilename = fileName; emit presetNameChanged();
	m_presetChangedButNotSaved = false; emit presetChangedButNotSavedChanged();
	QString baseName = QFileInfo(m_currentPresetFilename).baseName();
	// give feedback over OSC:
	m_osc.sendMessage("/s2l/out/active_preset", baseName, true);
	m_osc.sendMessage("/s2l/out/error", "-", true);

	// notify the GUI of the changes:
	emit trackCountChanged();
	emit currentTrackChanged();
	emit trackNamesChanged();
	emit trackChannelsChanged();
	notifyGuiOfTrackSettings();
}

void MainController::savePresetAs(const QString &constFileName, bool isAutosave)
{
	if (constFileName.isEmpty()) return;
	// prepare the filename:
	QString fileName(constFileName);
	if (fileName.startsWith("file:///")) {
#ifdef Q_OS_WIN
		fileName = fileName.remove(0, 8);
#else
		fileName = fileName.remove(0, 7);
#endif
	}
	// all preset files have to end on ".s2l":
	if (!fileName.toLower().endsWith(".s2l") && !isAutosave) {
		fileName = fileName + ".s2l";
	}

	QSettings settings(fileName, QSettings::IniFormat);
	settings.clear();  // remove keys of older formats
	settings.setValue("version", VERSION_STRING);
	settings.setValue("formatVersion", SETTINGS_FORMAT_VERSION);
	settings.setValue("multitrackFormat", 1);
	settings.setValue("changedAt", QDateTime::currentDateTime().toString());
	settings.setValue("consoleType", getConsoleType());
	settings.setValue("trackCount", m_trackCount);
	settings.setValue("currentTrack", m_currentTrack);

	// all tracks are saved, also the inactive ones, so that nothing is lost
	// when the number of tracks is reduced temporarily:
	for (int i=0; i<MAX_TRACKS; ++i) {
		settings.beginGroup(QString("track%1").arg(i + 1));
		m_tracks[i]->save(settings);
		settings.endGroup();
	}

	if (!isAutosave) {
		// this is now the loaded preset, update the preset name:
		m_currentPresetFilename = fileName; emit presetNameChanged();
		m_presetChangedButNotSaved = false; emit presetChangedButNotSavedChanged();
	}
}

void MainController::saveCurrentPreset()
{
	if (m_currentPresetFilename.isEmpty()) {
		openSavePresetAsDialog();
	} else {
		savePresetAs(m_currentPresetFilename);
	}
}

void MainController::autosave()
{
	savePresetAs(getPresetDirectory() + "/autosave.ats", true);
}

void MainController::restorePreset()
{
	loadPreset(getPresetDirectory() + "/autosave.ats");

	QSettings independentSettings;
	QString presetFileName = independentSettings.value("presetFileName").toString();

	if (presetFileName.isEmpty()) {
		// there wasn't a preset in last session
		m_currentPresetFilename = "";
		m_presetChangedButNotSaved = independentSettings.value("presetChangedButNotSaved").toBool();
	} else if (QFile(presetFileName).exists()) {
		// there was a preset and it does exist
		m_currentPresetFilename = presetFileName;
		m_presetChangedButNotSaved = independentSettings.value("presetChangedButNotSaved").toBool();
	} else {
		// there was a preset but it doesn't anymore exist
		m_currentPresetFilename = "";
		m_presetChangedButNotSaved = true;
	}

	emit presetNameChanged();
	emit presetChangedButNotSavedChanged();
}

void MainController::resetPreset()
{
	// reset all tracks to default:
	for (Track* t : m_tracks) t->reset();
	m_trackCount = 5;
	m_currentTrack = 0;
	bindCurrentTrackToGui();

	emit trackCountChanged();
	emit currentTrackChanged();
	emit trackNamesChanged();
	emit trackChannelsChanged();
	notifyGuiOfTrackSettings();

	// clear currentPresetFilename:
	m_currentPresetFilename = ""; emit presetNameChanged();
	m_presetChangedButNotSaved = false; emit presetChangedButNotSavedChanged();
}

void MainController::deletePreset(const QString &fileName)
{
	// if the preset to delete is currently loaded, reset to default settings:
	if (m_currentPresetFilename == fileName) {
		resetPreset();
	}

	QFile file(fileName);
	if (file.exists()) {
		file.remove();
	}
}

QString MainController::getPresetDirectory() const
{
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString MainController::getPresetName() const
{
	if (m_currentPresetFilename.isEmpty()) return "";
	return QFileInfo(m_currentPresetFilename).baseName();
}

void MainController::onPresetChanged()
{
	if (!m_presetChangedButNotSaved) {
		m_presetChangedButNotSaved = true;
		emit presetChangedButNotSavedChanged();
	}
}

void MainController::sendOscTestMessage(QString message)
{
	m_osc.sendMessage(message, true);
}

void MainController::openDialog(const QString &qmlDialogFile, QString propertyName, QVariant propertyValue)
{
	// check if dialog is already opened:
	if (m_dialogs.find(qmlDialogFile) != m_dialogs.end()) {
		QObject* dialog = m_dialogs[qmlDialogFile];
		if (dialog && dialog->property("visible").toBool()) {
			dialog->setProperty("visible", false);
			dialog->setProperty("visible", true);
			return;
		}
	}
	// create new dialog from QML file:
	QQmlComponent* component = new QQmlComponent(m_qmlEngine, QUrl(qmlDialogFile));
	QObject* dialog = component->beginCreate(m_qmlEngine->rootContext());
	if (!propertyName.isEmpty()) {
		dialog->setProperty(propertyName.toLatin1(), propertyValue);
	}
	component->completeCreate();
	QMetaObject::invokeMethod(dialog, "open");
	m_dialogs[qmlDialogFile] = dialog;
}

void MainController::dialogIsClosed(QObject *dialog)
{
	if (!dialog) return;
	QString qmlDialogFile = m_dialogs.key(dialog);
	m_dialogs.remove(qmlDialogFile);
	dialog->deleteLater();
}

void MainController::setPropertyWithoutChangingBindings(const QVariant &item, QString name, QVariant value)
{
	QQuickItem* qitem = item.value<QQuickItem*>();
	qitem->setProperty(name.toLatin1().data(), value);
}

bool MainController::oscLevelFeedbackIsEnabled()
{
	return m_oscUpdateTimer.isActive();
}

void MainController::enableOscLevelFeedback(bool value)
{
	if (value) {
		m_oscUpdateTimer.start(1000.0 / OSC_LEVEL_FEEDBACK_RATE);
	} else {
		m_oscUpdateTimer.stop();
	}
	m_osc.sendMessage(QString("/s2l/out/level_feedback=").append(value ? "1" : "0"), true);
}

void MainController::openSavePresetAsDialog()
{
	QString fileName = QFileDialog::getSaveFileName(0, tr("Save Preset As"),
							   getPresetDirectory(),
							   tr("Sound2Light Presets (*.s2l)"));
	savePresetAs(fileName);
}

void MainController::openLoadPresetDialog()
{
	QString fileName = QFileDialog::getOpenFileName(0, tr("Open Preset"),
							   getPresetDirectory(),
							   tr("Sound2Light Presets (*.s2l)"));
	loadPreset(fileName);
}
