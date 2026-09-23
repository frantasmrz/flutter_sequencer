/*
See LICENSE folder for this sample’s licensing information.

Abstract:
A DSPKernel subclass implementing the realtime signal processing portion of the AUv3FilterDemo audio unit.
*/
#ifndef SfizzDSPKernel_hpp
#define SfizzDSPKernel_hpp

#ifdef __cplusplus
#import "DSPKernel.hpp"
#import "SfizzSamplerInstrument.h"
#import <vector>
#import <iostream>
#include <cstring>

/*
 SfizzDSPKernel
 Calls Sfizz render code and handles MIDI events.
 As a non-ObjC class, this is safe to use from render thread.
 */
class SfizzDSPKernel : public DSPKernel {
public:

    // MARK: Member Functions

    SfizzDSPKernel() {
        mInstrument = std::make_unique<SfizzSamplerInstrument>();
    }

    void init(int inChannelCount, double inSampleRate) {
        channelCount = inChannelCount;
        sampleRate = float(inSampleRate);
        mInstrument->setOutputFormat(sampleRate, channelCount > 1);
        mInstrument->setSamplesPerBlock(maximumFramesToRender());
    }

    void reset() {
    }

    void startRamp(AUParameterAddress address, AUValue value, AUAudioFrameCount duration) override {
    }

    std::vector<float> mInterleaved;

    inline void ensureInterleaved(size_t frames) {
        const size_t need = static_cast<size_t>(frames) * 2;
        if (mInterleaved.size() < need) mInterleaved.resize(need);
    }

    void setBuffers(AudioBufferList* inBufferList, AudioBufferList* outBufferList) {
        inBufferListPtr = inBufferList;
        outBufferListPtr = outBufferList;
    }

    bool loadFile(const char* sfzPath, const char* tuningPath) {
        return mInstrument->loadSfzFile(sfzPath, tuningPath);
    }

    bool loadString(const char* sfzPath, const char* sfzString, const char* tuningString) {
        return mInstrument->loadSfzString(sfzPath, sfzString, tuningString);
    }

    void stopAllNotes() {
        mInstrument->stopAllNotes();
    }

    void process(AUAudioFrameCount frameCount, AUAudioFrameCount bufferOffset) override {
        if (!mInstrument) {
            // If you prefer silence on null instrument:
            for (UInt32 b = 0; b < outBufferListPtr->mNumberBuffers; ++b) {
                float* out = reinterpret_cast<float*>(outBufferListPtr->mBuffers[b].mData) + bufferOffset;
                std::memset(out, 0, sizeof(float) * frameCount);
            }
            return;
        }

        if (channelCount == 1) {
            // MONO (one channel, one buffer)
            float* outBuffer = reinterpret_cast<float*>(outBufferListPtr->mBuffers[0].mData) + bufferOffset;
            // instrument renders mono when configured with isStereo=false
            mInstrument->renderAudio(outBuffer, static_cast<int32_t>(frameCount));
            return;
        }

        // STEREO
        if (outBufferListPtr->mNumberBuffers >= 2) {
            // iOS typical: non-interleaved L/R
            ensureInterleaved(frameCount);

            // Render interleaved from instrument
            mInstrument->renderAudio(mInterleaved.data(), static_cast<int32_t>(frameCount));

            float* leftOut  = reinterpret_cast<float*>(outBufferListPtr->mBuffers[0].mData) + bufferOffset;
            float* rightOut = reinterpret_cast<float*>(outBufferListPtr->mBuffers[1].mData) + bufferOffset;

            // De-interleave
            for (AUAudioFrameCount i = 0; i < frameCount; ++i) {
                leftOut[i]  = mInterleaved[2 * i];
                rightOut[i] = mInterleaved[2 * i + 1];
            }
        } else if (outBufferListPtr->mNumberBuffers == 1) {
            // Less common: single interleaved stereo buffer
            float* interleavedOut = reinterpret_cast<float*>(outBufferListPtr->mBuffers[0].mData) + (bufferOffset * 2);
            mInstrument->renderAudio(interleavedOut, static_cast<int32_t>(frameCount));
        } else {
            // Safety: nothing to write to — clear nothing or early return
        }
    }
    
    void handleMIDIEvent(AUMIDIEvent const& midiEvent) override {
        if (midiEvent.eventType == 8) {
            auto midiStatus = midiEvent.data[0];
            auto midiData1 = midiEvent.data[1];
            auto midiData2 = midiEvent.data[2];
            
            mInstrument->handleMidiEvent(midiStatus, midiData1, midiData2);
        }
    }

    // MARK: Member Variables

private:
    int channelCount;
    float sampleRate;

    AudioBufferList* inBufferListPtr = nullptr;
    AudioBufferList* outBufferListPtr = nullptr;

public:
    std::unique_ptr<SfizzSamplerInstrument> mInstrument;
};

#endif
#endif /* SfizzDSPKernel_hpp */