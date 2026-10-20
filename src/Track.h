// Sound2Light Multitrack
// Copyright (c) 2016 Electronic Theatre Controls, Inc. (original Sound2Light)
// Multitrack extension (c) 2026 - released under the same MIT license.
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

#ifndef TRACK_H
#define TRACK_H

#include "MonoAudioBuffer.h"
#include "FFTAnalyzer.h"
#include "BPMDetector.h"
#include "BPMTapDetector.h"
#include "BPMOscControler.h"
#include "TriggerGenerator.h"
#include "TriggerGuiController.h"

#include <QString>
#include <QSettings>
#include <QVector>

class OSCNetworkManager;

// maximum number of tracks (= analysis chains) the application can run
static const int MAX_TRACKS = 16;

// number of trigger generators per track
static const int NUM_TRIGGERS = 6;

// A Track is one complete Sound2Light analysis chain:
// audio buffer -> FFT -> 6 trigger generators (bass, lo mid, hi mid, high, level, silence)
// plus an optional BPM detector.
// Every track listens to one channel of the multichannel audio input.
class Track
{
public:
	Track(int index, OSCNetworkManager* osc);
	~Track();

	// ------------------- identity ---------------------
	int getIndex() const { return m_index; }  // 0-based
	QString getName() const { return m_name; }
	void setName(const QString& name);
	QString getDefaultName() const;

	// input channel of the audio device (0-based), -1 = mix of all channels
	int getInputChannel() const { return m_inputChannel; }
	void setInputChannel(int ch) { m_inputChannel = qMax(-1, ch); }

	// ------------------- processing ---------------------
	MonoAudioBuffer& getBuffer() { return m_buffer; }
	FFTAnalyzer& getFft() { return m_fft; }
	const FFTAnalyzer& getFft() const { return m_fft; }

	// runs the FFT and checks all triggers (called ~44 times per second)
	void updateFFT();

	// runs the BPM detection if it is active (called ~20 times per second)
	void updateBPM();

	// sends the per-track OSC stream (/s2l/chN/...) if values changed
	void sendMultitrackOutput(OSCNetworkManager& osc, bool forceAll = false);

	// ------------------- triggers ---------------------
	TriggerGenerator* getTrigger(int i) const { return m_triggers[i]; }
	TriggerGuiController* getController(int i) const { return m_controllers[i]; }
	// returns the current level of trigger i [0...1]
	qreal getLevel(int i) const { return m_triggers[i]->getCurrentLevel(); }
	// returns whether trigger i is currently active (after delays)
	bool getTriggerActive(int i) const { return m_controllers[i]->getActive(); }

	// name of trigger i as used in OSC paths (bass, lo_mid, hi_mid, high, level, silence)
	static QString oscBandName(int i);

	// ------------------- settings ---------------------
	bool getLowSoloMode() const { return m_lowSoloMode; }
	void setLowSoloMode(bool v) { m_lowSoloMode = v; }

	bool m_bpmActive;  // true if the bpm detection is active
	bool m_autoBpm;  // true if BPM should be set automatically
	bool m_waveformVisible;  // true if the waveform is visible

	BPMOscControler& getBpmOsc() { return m_bpmOSC; }
	BPMDetector& getBpm() { return m_bpm; }
	BPMTapDetector& getBpmTap() { return m_bpmTap; }

	// current BPM value (detected or tapped)
	float getCurrentBpm();

	// ------------------- save / restore ---------------------
	// saves all track parameters (the caller selects the group in QSettings)
	void save(QSettings& settings);
	// restores all track parameters (the caller selects the group in QSettings)
	void restore(QSettings& settings);
	// resets all parameters to default values
	void reset();

protected:
	const int m_index;
	QString m_name;
	int m_inputChannel;

	// order matters for initialization:
	MonoAudioBuffer m_buffer;
	QVector<TriggerGeneratorInterface*> m_triggerContainer;
	FFTAnalyzer m_fft;
	BPMOscControler m_bpmOSC;
	BPMDetector m_bpm;
	BPMTapDetector m_bpmTap;

	bool m_lowSoloMode;

	TriggerGenerator* m_triggers[NUM_TRIGGERS];
	TriggerGuiController* m_controllers[NUM_TRIGGERS];

	// last values sent via the multitrack OSC stream:
	float m_lastSentLevel[NUM_TRIGGERS];
	int m_lastSentTrig[NUM_TRIGGERS];
	int m_lastSentBpm;
};

#endif // TRACK_H
