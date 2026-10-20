// Sound2Light Multitrack
// Multitrack extension (c) 2026 - released under the MIT license (see Track.h).

#include "MultiChannelInput.h"
#include "Track.h"
#include "rtaudio/RtAudio.h"

#include <QDebug>
#include <QMutexLocker>
#include <QtMath>
#include <vector>
#include <cmath>
#include <algorithm>

namespace {

struct DeviceEntry {
	RtAudio::Api api;
	unsigned int id;
	QString name;  // display name
	unsigned int inputChannels;
	bool isDefault;
};

QString apiLabel(RtAudio::Api api)
{
	return QString::fromStdString(RtAudio::getApiDisplayName(api));
}

// lists all input devices of all compiled audio APIs
std::vector<DeviceEntry> listDevices()
{
	std::vector<DeviceEntry> result;
	std::vector<RtAudio::Api> apis;
	RtAudio::getCompiledApi(apis);

	// ignore the dummy API if a real one exists:
	std::vector<RtAudio::Api> realApis;
	for (RtAudio::Api api : apis) {
		if (api != RtAudio::RTAUDIO_DUMMY) realApis.push_back(api);
	}
	const bool multipleApis = realApis.size() > 1;

	for (RtAudio::Api api : realApis) {
		RtAudio audio(api, [](RtAudioErrorType, const std::string&) { /* silence enumeration warnings */ });
		const unsigned int defaultId = audio.getDefaultInputDevice();
		for (unsigned int id : audio.getDeviceIds()) {
			RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
			if (info.inputChannels == 0) continue;
			DeviceEntry e;
			e.api = api;
			e.id = id;
			e.inputChannels = info.inputChannels;
			e.isDefault = (id == defaultId);
			QString name = QString::fromStdString(info.name);
			e.name = multipleApis ? QString("[%1] %2").arg(apiLabel(api), name) : name;
			result.push_back(e);
		}
	}
	return result;
}

int rtCallback(void* /*out*/, void* in, unsigned int nFrames, double /*streamTime*/,
			   RtAudioStreamStatus /*status*/, void* userData)
{
	MultiChannelInput* self = static_cast<MultiChannelInput*>(userData);
	if (in && self) {
		self->onAudioCallback(static_cast<const float*>(in), nFrames);
	}
	return 0;
}

}  // namespace


MultiChannelInput::MultiChannelInput(QObject* parent)
	: QObject(parent)
	, m_audio(nullptr)
	, m_channelCount(0)
	, m_deviceSampleRate(ANALYSIS_SAMPLE_RATE)
	, m_testSignal(false)
	, m_volume(1.0)
	, m_resamplePos(0.0)
	, m_haveLastFrame(false)
	, m_testFramesGenerated(0)
{
}

MultiChannelInput::~MultiChannelInput()
{
	closeStream();
}

QStringList MultiChannelInput::getAvailableInputs() const
{
	QStringList names;
	for (const DeviceEntry& e : listDevices()) {
		names.append(QString("%1 (%2 ch)").arg(e.name).arg(e.inputChannels));
	}
	names.append(TEST_SIGNAL_DEVICE_NAME);
	return names;
}

QString MultiChannelInput::getDefaultInputName() const
{
	for (const DeviceEntry& e : listDevices()) {
		if (e.isDefault) return QString("%1 (%2 ch)").arg(e.name).arg(e.inputChannels);
	}
	QStringList all = getAvailableInputs();
	return all.isEmpty() ? QString() : all.first();
}

void MultiChannelInput::closeStream()
{
	if (m_audio) {
		try {
			if (m_audio->isStreamRunning()) m_audio->stopStream();
			if (m_audio->isStreamOpen()) m_audio->closeStream();
		} catch (...) {}
		delete m_audio;
		m_audio = nullptr;
	}
	QMutexLocker lock(&m_mutex);
	m_pending.clear();
}

bool MultiChannelInput::setInputByName(const QString& name)
{
	closeStream();
	m_lastError.clear();
	m_testSignal = false;
	m_resamplePos = 0.0;
	m_haveLastFrame = false;

	// --- built-in test signal ---
	if (name == TEST_SIGNAL_DEVICE_NAME) {
		m_testSignal = true;
		m_channelCount = 8;
		m_deviceSampleRate = ANALYSIS_SAMPLE_RATE;
		m_apiName = "Test";
		m_activeInputName = name;
		m_testClock.start();
		m_testFramesGenerated = 0;
		m_lastFrame = QVector<float>(m_channelCount, 0.0f);
		m_peaks = QVector<float>(m_channelCount, 0.0f);
		return true;
	}

	// --- find device ---
	std::vector<DeviceEntry> devices = listDevices();
	const DeviceEntry* entry = nullptr;
	for (const DeviceEntry& e : devices) {
		const QString display = QString("%1 (%2 ch)").arg(e.name).arg(e.inputChannels);
		// accept the display name and the plain name (from older settings):
		if (display == name || e.name == name) { entry = &e; break; }
	}
	if (!entry) {
		// fall back to the default device:
		for (const DeviceEntry& e : devices) {
			if (e.isDefault) { entry = &e; break; }
		}
	}
	if (!entry && !devices.empty()) entry = &devices.front();
	if (!entry) {
		m_lastError = "Aucune entrée audio trouvée";
		m_channelCount = 0;
		m_activeInputName.clear();
		return false;
	}

	m_audio = new RtAudio(entry->api, [this](RtAudioErrorType type, const std::string& msg) {
		if (type != RTAUDIO_WARNING) m_lastError = QString::fromStdString(msg);
		qWarning() << "RtAudio:" << QString::fromStdString(msg);
	});
	m_apiName = apiLabel(entry->api);

	RtAudio::DeviceInfo info = m_audio->getDeviceInfo(entry->id);
	const int channels = int(qMin<unsigned int>(info.inputChannels, MAX_INPUT_CHANNELS));

	// choose the sample rate: device preferred / current rate, else 48k or 44.1k
	unsigned int rate = info.currentSampleRate ? info.currentSampleRate : info.preferredSampleRate;
	if (rate == 0) {
		rate = 48000;
		for (unsigned int r : info.sampleRates) {
			if (r == 44100) { rate = 44100; break; }
		}
	}

	RtAudio::StreamParameters params;
	params.deviceId = entry->id;
	params.nChannels = static_cast<unsigned int>(channels);
	params.firstChannel = 0;

	RtAudio::StreamOptions options;
	options.flags = RTAUDIO_MINIMIZE_LATENCY;
	options.streamName = "Sound2Light Multitrack";

	unsigned int bufferFrames = 256;
	RtAudioErrorType err = m_audio->openStream(nullptr, &params, RTAUDIO_FLOAT32, rate,
											   &bufferFrames, &rtCallback, this, &options);
	if (err != RTAUDIO_NO_ERROR) {
		qWarning() << "Could not open audio input" << entry->name << m_lastError;
		delete m_audio; m_audio = nullptr;
		m_channelCount = 0;
		m_activeInputName = name;
		return false;
	}

	m_channelCount = channels;
	m_deviceSampleRate = int(m_audio->getStreamSampleRate());
	if (m_deviceSampleRate <= 0) m_deviceSampleRate = int(rate);
	m_lastFrame = QVector<float>(m_channelCount, 0.0f);
	m_peaks = QVector<float>(m_channelCount, 0.0f);
	m_activeInputName = QString("%1 (%2 ch)").arg(entry->name).arg(entry->inputChannels);

	if (m_audio->startStream() != RTAUDIO_NO_ERROR) {
		qWarning() << "Could not start audio input" << m_lastError;
		return false;
	}
	qDebug() << "Audio input:" << m_activeInputName << m_apiName << m_channelCount << "ch @" << m_deviceSampleRate << "Hz";
	return true;
}

void MultiChannelInput::setVolume(const qreal& value)
{
	m_volume.store(qBound(0.0, double(value), 1.0));
}

void MultiChannelInput::onAudioCallback(const float* interleaved, unsigned int frames)
{
	const int nch = m_channelCount;
	if (nch <= 0) return;
	const int count = int(frames) * nch;

	QMutexLocker lock(&m_mutex);
	// never let the queue grow beyond ~2 seconds (i.e. if the GUI stalls):
	const int maxSamples = m_deviceSampleRate * 2 * nch;
	if (m_pending.size() + count > maxSamples) {
		const int drop = qMin(m_pending.size(), m_pending.size() + count - maxSamples);
		m_pending.remove(0, drop - (drop % nch));
	}
	const int old = m_pending.size();
	m_pending.resize(old + count);
	std::copy(interleaved, interleaved + count, m_pending.begin() + old);
}

void MultiChannelInput::generateTestSignal(QVector<float>& out, int frames)
{
	// 8 channels, each one a tone burst with its own frequency and rhythm,
	// so that every track shows a clearly different behaviour:
	static const float freqs[8]   = {60.f, 400.f, 1000.f, 5000.f, 80.f, 250.f, 2500.f, 9000.f};
	static const float periods[8] = {0.50f, 0.75f, 0.33f, 0.25f, 1.00f, 0.60f, 0.40f, 1.50f};  // seconds
	const int nch = 8;
	out.resize(frames * nch);
	for (int f=0; f<frames; ++f) {
		const double t = double(m_testFramesGenerated + f) / ANALYSIS_SAMPLE_RATE;
		for (int ch=0; ch<nch; ++ch) {
			const double phaseInPeriod = std::fmod(t, double(periods[ch])) / periods[ch];
			// short attack, exponential decay: like a percussive hit
			const double env = std::exp(-phaseInPeriod * 8.0);
			out[f*nch + ch] = float(0.8 * env * qSin(2.0 * M_PI * freqs[ch] * t));
		}
	}
	m_testFramesGenerated += frames;
}

QVector<float> MultiChannelInput::takeChannelPeaks()
{
	QVector<float> p = m_peaks;
	m_peaks.fill(0.0f);
	return p;
}

void MultiChannelInput::pump(const QVector<Track*>& tracks)
{
	const int nch = m_channelCount;
	if (nch <= 0) return;

	// ---- collect the new interleaved data ----
	if (m_testSignal) {
		const qint64 due = m_testClock.nsecsElapsed() * ANALYSIS_SAMPLE_RATE / 1000000000LL;
		int frames = int(due - m_testFramesGenerated);
		if (frames <= 0) return;
		frames = qMin(frames, ANALYSIS_SAMPLE_RATE);  // max 1s at once
		generateTestSignal(m_work, frames);
	} else {
		QMutexLocker lock(&m_mutex);
		m_work.swap(m_pending);
		m_pending.clear();
	}
	const int inFrames = m_work.size() / nch;
	if (inFrames <= 0) return;

	// ---- metering ----
	if (m_peaks.size() != nch) m_peaks = QVector<float>(nch, 0.0f);
	for (int f=0; f<inFrames; ++f) {
		const float* frame = m_work.constData() + f*nch;
		for (int ch=0; ch<nch; ++ch) {
			const float a = qAbs(frame[ch]);
			if (a > m_peaks[ch]) m_peaks[ch] = a;
		}
	}

	// ---- resample to the analysis sample rate (linear interpolation) ----
	// output frame k is read at input position m_resamplePos + k*step,
	// where position -1 is the last frame of the previous block.
	const double step = double(m_deviceSampleRate) / ANALYSIS_SAMPLE_RATE;
	const bool resample = (m_deviceSampleRate != ANALYSIS_SAMPLE_RATE);
	const float gain = float(m_volume.load());

	auto sampleAt = [&](int frameIndex, int ch) -> float {
		if (frameIndex < 0) return m_haveLastFrame ? m_lastFrame[ch] : 0.0f;
		return m_work[frameIndex*nch + ch];
	};

	// build the list of output positions once:
	QVector<int> idx;
	QVector<float> frac;
	if (resample) {
		double pos = m_resamplePos;
		while (pos <= double(inFrames - 1)) {
			const int i0 = int(std::floor(pos));
			idx.append(i0);
			frac.append(float(pos - i0));
			pos += step;
		}
		m_resamplePos = pos - inFrames;
	}
	const int outFrames = resample ? idx.size() : inFrames;

	auto channelSample = [&](int k, int ch) -> float {
		if (!resample) return m_work[k*nch + ch];
		const float a = sampleAt(idx[k], ch);
		const float b = sampleAt(idx[k] + 1, ch);
		return a + (b - a) * frac[k];
	};

	// ---- distribute to the tracks ----
	QVector<qreal> data(outFrames);
	for (Track* track : tracks) {
		if (!track) continue;
		const int ch = track->getInputChannel();
		if (ch >= 0 && ch < nch) {
			for (int k=0; k<outFrames; ++k) data[k] = channelSample(k, ch) * gain;
		} else if (ch < 0) {
			// mix of all channels (like the original stereo -> mono behaviour)
			for (int k=0; k<outFrames; ++k) {
				float sum = 0.0f;
				for (int c=0; c<nch; ++c) sum += channelSample(k, c);
				data[k] = sum / nch * gain;
			}
		} else {
			// channel not present on this device: silence
			data.fill(0.0);
		}
		QVector<qreal> copy = data;  // putSamples may modify the vector
		track->getBuffer().putSamples(copy, 1);
	}

	// remember the last frame for interpolation across blocks:
	for (int ch=0; ch<nch; ++ch) m_lastFrame[ch] = m_work[(inFrames-1)*nch + ch];
	m_haveLastFrame = true;
	m_work.clear();
}
