// Sound2Light Multitrack
// Multitrack extension (c) 2026 - released under the MIT license (see Track.h).

#ifndef MULTICHANNELINPUT_H
#define MULTICHANNELINPUT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMutex>
#include <QElapsedTimer>
#include <atomic>

class RtAudio;
class Track;

// Sample rate the whole analysis chain (FFT scaling, BPM detection) is designed for.
// Input at any other rate (i.e. 48 kHz from Dante) is resampled to this rate.
static const int ANALYSIS_SAMPLE_RATE = 44100;

// maximum number of input channels that are opened on a device
static const int MAX_INPUT_CHANNELS = 64;

// Name of the built-in test signal "device"
#define TEST_SIGNAL_DEVICE_NAME "Signal de test (8 canaux)"

// A multichannel audio input based on RtAudio (CoreAudio / WASAPI / ASIO / ALSA ...).
// The audio callback only stores the interleaved samples.
// pump() is called from the GUI thread before each FFT and distributes the
// samples of each channel to the tracks listening to it.
class MultiChannelInput : public QObject
{
	Q_OBJECT

public:
	explicit MultiChannelInput(QObject* parent = nullptr);
	~MultiChannelInput() override;

	// returns a list of the names of all available input devices (+ test signal)
	QStringList getAvailableInputs() const;
	// returns the name of the default input device
	QString getDefaultInputName() const;
	// returns the name of the active input device
	QString getActiveInputName() const { return m_activeInputName; }
	// opens the input device with the given name (all its input channels)
	// returns false if the device could not be opened
	bool setInputByName(const QString& name);

	// number of channels of the active input
	int getChannelCount() const { return m_channelCount; }
	// sample rate of the active input
	int getDeviceSampleRate() const { return m_deviceSampleRate; }
	// name of the audio API in use (i.e. "CoreAudio", "WASAPI")
	QString getApiName() const { return m_apiName; }
	// last error message (empty if none)
	QString getLastError() const { return m_lastError; }

	// software input gain [0...1]
	qreal getVolume() const { return m_volume.load(); }
	void setVolume(const qreal& value);

	// distributes all audio received since the last call to the tracks
	// - must be called from the GUI thread (same thread as the FFT)
	void pump(const QVector<Track*>& tracks);

	// peak level per input channel since last call [0...1], for metering
	QVector<float> takeChannelPeaks();

	// called from the RtAudio callback (audio thread)
	void onAudioCallback(const float* interleaved, unsigned int frames);

protected:
	void closeStream();
	void generateTestSignal(QVector<float>& out, int frames);

	RtAudio* m_audio;
	QString m_activeInputName;
	QString m_apiName;
	QString m_lastError;
	int m_channelCount;
	int m_deviceSampleRate;
	bool m_testSignal;
	std::atomic<double> m_volume;

	// interleaved samples written by the audio thread:
	QMutex m_mutex;
	QVector<float> m_pending;
	// working copy used by pump() (GUI thread):
	QVector<float> m_work;

	// resampler state (one per channel + mix):
	double m_resamplePos;  // fractional read position in the incoming stream
	QVector<float> m_lastFrame;  // last frame of the previous block (for interpolation)
	bool m_haveLastFrame;

	QVector<float> m_peaks;

	// test signal state:
	QElapsedTimer m_testClock;
	qint64 m_testFramesGenerated;
};

#endif // MULTICHANNELINPUT_H
