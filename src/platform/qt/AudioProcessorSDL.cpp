/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "AudioProcessorSDL.h"

#include "LogController.h"

extern "C" {
#include "gba/supervisor/thread.h"
}

using namespace QGBA;

AudioProcessorSDL::AudioProcessorSDL(QObject* parent)
	: AudioProcessor(parent)
	, m_audio{ 2048, 44100 }
{
}

AudioProcessorSDL::~AudioProcessorSDL() {
	mSDLDeinitAudio(&m_audio);
}

bool AudioProcessorSDL::start() {
	if (!input()) {
		LOG(QT, WARN) << tr("Can't start an audio processor without input");
		return false;
	}

	if (m_audio.thread) {
		mSDLResumeAudio(&m_audio);
		return true;
	} else {
		if (!m_audio.samples) {
			m_audio.samples = 2048; // TODO?
		}
		if (mSDLInitAudio(&m_audio, nullptr)) {
			m_audio.core = input()->core;
			m_audio.sync = &input()->sync;
			mSDLResumeAudio(&m_audio);
			return true;
		}
		return false;
	}
}

void AudioProcessorSDL::pause() {
	mSDLPauseAudio(&m_audio);
}

void AudioProcessorSDL::setBufferSamples(int samples) {
	AudioProcessor::setBufferSamples(samples);
	m_audio.samples = samples;
	if (m_audio.thread) {
		mSDLDeinitAudio(&m_audio);
		mSDLInitAudio(&m_audio, nullptr);
		m_audio.core = input()->core;
		m_audio.sync = &input()->sync;
		mSDLResumeAudio(&m_audio);
	}
}

void AudioProcessorSDL::inputParametersChanged() {
}

void AudioProcessorSDL::requestSampleRate(unsigned rate) {
	m_audio.sampleRate = rate;
	if (m_audio.thread) {
		mSDLDeinitAudio(&m_audio);
		mSDLInitAudio(&m_audio, nullptr);
		m_audio.core = input()->core;
		m_audio.sync = &input()->sync;
		mSDLResumeAudio(&m_audio);
	}
}

unsigned AudioProcessorSDL::sampleRate() const {
	if (m_audio.thread) {
		return m_audio.obtainedSpec.freq;
	} else {
		return 0;
	}
}
