// Sound2Light Multitrack
// Copyright (c) 2016 Electronic Theatre Controls, Inc. (original Sound2Light)
// Multitrack extension (c) 2026 - released under the same MIT license.
// See Track.h for the full license text.

#include "Track.h"
#include "OSCNetworkManager.h"

#include <QtMath>

Track::Track(int index, OSCNetworkManager* osc)
	: m_bpmActive(false)
	, m_autoBpm(false)
	, m_waveformVisible(true)
	, m_index(index)
	, m_name()
	, m_inputChannel(index)
	, m_buffer(NUM_SAMPLES*4)  // more buffer so the bpm detector can get overlapping data
	, m_triggerContainer()
	, m_fft(m_buffer, m_triggerContainer)
	, m_bpmOSC(*osc)
	, m_bpm(m_buffer, &m_bpmOSC)
	, m_bpmTap(&m_bpmOSC)
	, m_lowSoloMode(false)
	, m_lastSentBpm(-1)
{
	// the names are used as keys in the preset files and must not change:
	m_triggers[0] = new TriggerGenerator("bass", osc, true, false, 80);
	m_triggers[1] = new TriggerGenerator("loMid", osc, true, false, 400);
	m_triggers[2] = new TriggerGenerator("hiMid", osc, true, false, 1000);
	m_triggers[3] = new TriggerGenerator("high", osc, true, false, 5000);
	m_triggers[4] = new TriggerGenerator("envelope", osc, false);
	m_triggers[5] = new TriggerGenerator("silence", osc, false, true);

	const QString prefix = QString("/s2l/out/ch%1/").arg(m_index + 1);
	m_bpmOSC.setOutPrefix(prefix);

	for (int i=0; i<NUM_TRIGGERS; ++i) {
		m_triggers[i]->setOutPrefix(prefix);
		// order is important because of lowSoloMode:
		m_triggerContainer.append(m_triggers[i]);
		m_controllers[i] = new TriggerGuiController(m_triggers[i]);
		m_lastSentLevel[i] = -1.0f;
		m_lastSentTrig[i] = -1;
	}
}

Track::~Track()
{
	for (int i=0; i<NUM_TRIGGERS; ++i) {
		delete m_controllers[i]; m_controllers[i] = nullptr;
		delete m_triggers[i]; m_triggers[i] = nullptr;
	}
}

void Track::setName(const QString& name)
{
	m_name = name.trimmed();
}

QString Track::getDefaultName() const
{
	return QString("Piste %1").arg(m_index + 1);
}

void Track::updateFFT()
{
	m_fft.calculateFFT(m_lowSoloMode);
}

void Track::updateBPM()
{
	if (m_bpmActive) {
		m_bpm.detectBPM();
	}
}

QString Track::oscBandName(int i)
{
	static const char* names[NUM_TRIGGERS] = {"bass", "lo_mid", "hi_mid", "high", "level", "silence"};
	if (i < 0 || i >= NUM_TRIGGERS) return QString();
	return QString(names[i]);
}

void Track::sendMultitrackOutput(OSCNetworkManager& osc, bool forceAll)
{
	const QString base = QString("/s2l/ch%1/").arg(m_index + 1);

	for (int i=0; i<NUM_TRIGGERS; ++i) {
		if (m_triggers[i]->getMute()) continue;

		// continuous level [0...1]:
		const float level = float(m_triggers[i]->getCurrentLevel());
		if (forceAll || qAbs(level - m_lastSentLevel[i]) > 0.002f) {
			osc.sendFloat(base + oscBandName(i), level);
			m_lastSentLevel[i] = level;
		}

		// trigger state (after on / off delays and max hold):
		const int trig = m_controllers[i]->getActive() ? 1 : 0;
		if (forceAll || trig != m_lastSentTrig[i]) {
			osc.sendInt(base + oscBandName(i) + "/trig", trig);
			m_lastSentTrig[i] = trig;
		}
	}

	// BPM (only if detection or tap tempo gives a value):
	if (m_bpmActive || m_bpmTap.hasBpm()) {
		const int bpm = qRound(getCurrentBpm());
		if (bpm > 0 && (forceAll || bpm != m_lastSentBpm)) {
			osc.sendFloat(base + "bpm", float(bpm));
			m_lastSentBpm = bpm;
		}
	}
}

float Track::getCurrentBpm()
{
	const bool manual = m_bpmTap.hasBpm() && !m_autoBpm;
	if (manual || m_bpm.getBPM() == 0.0f) return m_bpmTap.getBpm();
	return m_bpm.getBPM();
}

void Track::save(QSettings& settings)
{
	ScaledSpectrum& spectrum = m_fft.getScaledSpectrum();
	settings.setValue("name", m_name);
	settings.setValue("inputChannel", m_inputChannel);
	settings.setValue("dbConversion", spectrum.getDecibelConversion());
	settings.setValue("fftGain", spectrum.getGain());
	settings.setValue("fftCompression", spectrum.getCompression());
	settings.setValue("agcEnabled", spectrum.getAgcEnabled());
	settings.setValue("lowSoloMode", m_lowSoloMode);
	settings.setValue("bpm/Active", m_bpmActive);
	settings.setValue("autoBpm", m_autoBpm);
	settings.setValue("waveformVisible", m_waveformVisible);

	for (int i=0; i<m_triggerContainer.size(); ++i) {
		m_triggerContainer[i]->save(settings);
	}

	settings.setValue("bpm/Min", m_bpm.getMinBPM());
	m_bpmOSC.save(settings);
	settings.setValue("bpm/tapvalue", m_bpmTap.getBpm());
	settings.setValue("bpm/mute", m_bpmOSC.getBPMMute());
}

void Track::restore(QSettings& settings)
{
	// start from the defaults, so that keys missing in the file get sensible values:
	reset();

	ScaledSpectrum& spectrum = m_fft.getScaledSpectrum();
	setName(settings.value("name", "").toString());
	setInputChannel(settings.value("inputChannel", m_index).toInt());
	spectrum.setDecibelConversion(settings.value("dbConversion", false).toBool());
	spectrum.setGain(settings.value("fftGain", 1.0).toReal());
	spectrum.setCompression(settings.value("fftCompression", 1.0).toReal());
	spectrum.setAgcEnabled(settings.value("agcEnabled", true).toBool());
	m_lowSoloMode = settings.value("lowSoloMode", false).toBool();

	const bool bpmActive = settings.value("bpm/Active", false).toBool();
	if (bpmActive && !m_bpmActive) {
		m_bpm.resetCache();
		m_bpmTap.reset();
	}
	m_bpmActive = bpmActive;
	m_autoBpm = settings.value("autoBpm", false).toBool();
	m_bpm.setTransmitBpm(m_autoBpm);
	m_waveformVisible = settings.value("waveformVisible", true).toBool();

	for (int i=0; i<m_triggerContainer.size(); ++i) {
		m_triggerContainer[i]->restore(settings);
	}

	const int minBpm = settings.value("bpm/Min", 75).toInt();
	m_bpm.setMinBPM(minBpm);
	m_bpmTap.setMinBPM(minBpm);
	m_bpmOSC.restore(settings);
	m_bpmTap.setBpm(settings.value("bpm/tapvalue", 60).toInt());
	m_bpmOSC.setBPMMute(settings.value("bpm/mute", false).toBool());

	// force a full resend of the multitrack stream:
	for (int i=0; i<NUM_TRIGGERS; ++i) {
		m_lastSentLevel[i] = -1.0f;
		m_lastSentTrig[i] = -1;
	}
	m_lastSentBpm = -1;
}

void Track::reset()
{
	ScaledSpectrum& spectrum = m_fft.getScaledSpectrum();
	m_name.clear();
	m_inputChannel = m_index;
	spectrum.setGain(1.0);
	spectrum.setCompression(1.0);
	spectrum.setAgcEnabled(true);
	spectrum.setDecibelConversion(false);
	m_lowSoloMode = false;
	m_bpmActive = false;
	m_autoBpm = false;
	m_bpm.setTransmitBpm(false);
	m_waveformVisible = true;
	m_bpm.setMinBPM(75);
	m_bpmTap.setMinBPM(75);
	m_bpmOSC.setCommands(QStringList());

	for (int i=0; i<NUM_TRIGGERS; ++i) {
		m_controllers[i]->resetParameters();
	}
}
