#ifndef SFIZZ_SAMPLER_INSTRUMENT_H
#define SFIZZ_SAMPLER_INSTRUMENT_H

#ifdef __cplusplus
#include "IInstrument.h"
#include "sfizz.hpp"

class SfizzSamplerInstrument : public IInstrument {
public:
    SfizzSamplerInstrument() {
        mSampler = std::make_unique<sfz::Sfizz>();
    }

    bool setOutputFormat(int32_t sampleRate, bool isStereo) override {
        mIsStereo = isStereo;
        mSampler->setSampleRate(sampleRate);

        return true;
    }

    void setSamplesPerBlock(int samplesPerBlock) {
        mSampler->setSamplesPerBlock(samplesPerBlock);
    }

    bool loadSfzString(const char* sampleRoot, const char* sfzString, const char* tuningString) {
        auto loadResult = mSampler->loadSfzString(sampleRoot, sfzString);
        auto loadTuningResult = true;

        if (tuningString != nullptr) {
            mSampler->loadScalaString(tuningString);
        }

        return loadResult && loadTuningResult && mSampler->getNumRegions();
    }

    void stopAllNotes() {
        if (!mSampler) return;

        // Stop all voices
        mSampler->allNotesOff(0);

        // Optionally render silence to flush DSP tails
        const int frames = 1024;
        const int channels = 2;
        std::vector<float> silent(frames * channels, 0.0f);
        float* buffers[2] = { silent.data(), silent.data() + frames };

        for (int i = 0; i < 10; ++i) {
            mSampler->renderBlock(buffers, frames);
        }
    }

    bool loadSfzFile(const char* path, const char* tuningPath) {
        auto loadResult = mSampler->loadSfzFile(path);
        auto loadTuningResult = true;

        if (tuningPath != nullptr) {
            mSampler->loadScalaFile(tuningPath);
        }

        return loadResult && loadTuningResult && mSampler->getNumRegions();
    }

    void renderAudio(float *audioData, int32_t numFrames) override {
        float leftBuffer[numFrames];
        float rightBuffer[numFrames];
        float* buffers[2];

        buffers[0] = leftBuffer;
        buffers[1] = rightBuffer;

        for (int f = 0; f < numFrames; f++) {
            leftBuffer[f] = 0.;
            rightBuffer[f] = 0.;
        }

        mSampler->renderBlock(buffers, numFrames);

        for (int f = 0; f < numFrames; f++) {
            if (mIsStereo) {
                for (int c = 0; c < 2; c++) {
                    audioData[f * 2 + c] = buffers[c][f];
                }
            } else {
                audioData[f] = (buffers[0][f] + buffers[1][f]) / 2;
            }
        }
    }

    void handleMidiEvent(uint8_t status, uint8_t data1, uint8_t data2) override {
        auto statusCode = status >> 4;

        if (statusCode == 0x9) {
            // Note On
            mSampler->noteOn(0, data1, data2);
        } else if (statusCode == 0x8) {
            // Note Off
            mSampler->noteOff(0, data1, data2);
        } else if (statusCode == 0xB) {
            // CC
            mSampler->cc(0, data1, data2);
        } else if (statusCode == 0xE) {
            // Pitch bend
            // get 14-bit number from data1 and data2, subtract 8192
            auto pitch = ((data2 << 7) | data1) - 8192;
            mSampler->pitchWheel(0, pitch);
        }
    }

    void reset() override {
    }

private:
    bool mIsStereo;
    std::unique_ptr<sfz::Sfizz> mSampler;
};

#endif
#endif //SFIZZ_SAMPLER_INSTRUMENT_H
