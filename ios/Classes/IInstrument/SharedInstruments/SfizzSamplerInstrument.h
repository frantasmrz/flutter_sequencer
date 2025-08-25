#ifndef SFIZZ_SAMPLER_INSTRUMENT_H
#define SFIZZ_SAMPLER_INSTRUMENT_H

#ifdef __cplusplus
#include "IInstrument.h"
#include "sfizz.hpp"
#include <vector>
#include <cstring>
#include <atomic>
#include <mutex>

enum class MidiType : uint8_t { NoteOn, NoteOff, CC, Pitch };

struct MidiEvent {
    MidiType type;
    uint8_t d1;
    uint8_t d2;
};

constexpr uint32_t kMidiQueueSize = 512;

class SfizzSamplerInstrument : public IInstrument {
public:
    SfizzSamplerInstrument() {
        mSampler = std::make_unique<sfz::Sfizz>();
    }

    std::atomic<bool> mReady{false};     // rendering allowed
    std::atomic<bool> mLoading{false};   // sampler is mutating / loading
    std::mutex mNonRtMutex;

    std::atomic<uint32_t> mMidiWrite{0};
    std::atomic<uint32_t> mMidiRead{0};
    MidiEvent mMidiQueue[kMidiQueueSize];
    int mMaxFramesPerBlock = 512;
    int mSampleRate = 44100;          // set by setOutputFormat
    std::atomic<int> mMuteFrames{0};  // how many output frames to force to silence

    bool setOutputFormat(int32_t sampleRate, bool isStereo) override {
        std::lock_guard<std::mutex> lk(mNonRtMutex);
        mLoading = true;
        mReady = false;

        mIsStereo = isStereo;
        mSampler->setSampleRate(sampleRate);
        mSampleRate = sampleRate;

        mLoading = false;
        mReady = true;
        return true;
    }

    void setSamplesPerBlock(int samplesPerBlock) {
        std::lock_guard<std::mutex> lk(mNonRtMutex);
        mLoading = true;
        mReady = false;

        mSampler->setSamplesPerBlock(samplesPerBlock);
        mMaxFramesPerBlock = samplesPerBlock;
        // (optional) reserve scratch
        mScratchL.reserve(samplesPerBlock);
        mScratchR.reserve(samplesPerBlock);

        mLoading = false;
        mReady = true;
    }

    bool loadSfzString(const char* sampleRoot, const char* sfzString, const char* tuningString) {
        std::lock_guard<std::mutex> lk(mNonRtMutex);
        mLoading = true;
        mReady = false;

        const bool okSfz = mSampler->loadSfzString(sampleRoot, sfzString);
        if (tuningString) mSampler->loadScalaString(tuningString);

        mLoading = false;
        mReady = okSfz && (mSampler->getNumRegions() > 0);
        return mReady.load();
    }

    void stopAllNotes() override {
        // Enqueue note-offs to the MIDI queue (non-RT safe)
        for (int note = 0; note < 128; ++note) {
            MidiEvent ev{};
            ev.type = MidiType::NoteOff;
            ev.d1 = static_cast<uint8_t>(note);
            ev.d2 = 0;

            const uint32_t w = mMidiWrite.load(std::memory_order_relaxed);
            const uint32_t r = mMidiRead.load(std::memory_order_acquire);
            if ((uint32_t)(w - r) < kMidiQueueSize) {
                mMidiQueue[w % kMidiQueueSize] = ev;
                mMidiWrite.store(w + 1, std::memory_order_release);
            }
        }

        // Also send sustain off + All Notes Off / All Sound Off as safety
        auto enqueueCC = [&](uint8_t cc, uint8_t val) {
            MidiEvent ev{}; ev.type = MidiType::CC; ev.d1 = cc; ev.d2 = val;
            const uint32_t w = mMidiWrite.load(std::memory_order_relaxed);
            const uint32_t r = mMidiRead.load(std::memory_order_acquire);
            if ((uint32_t)(w - r) < kMidiQueueSize) {
                mMidiQueue[w % kMidiQueueSize] = ev;
                mMidiWrite.store(w + 1, std::memory_order_release);
            }
        };
        enqueueCC(64, 0);   // Sustain off
        enqueueCC(120, 0);  // All Sound Off
        enqueueCC(123, 0);  // All Notes Off

        // Request a short mute window so tails are flushed silently by the audio thread
        // e.g., ~250ms of silence:
        const int muteMs = 250;
        const int framesToMute = (mSampleRate * muteMs) / 1000;
        mMuteFrames.store(framesToMute, std::memory_order_release);
    }

    bool loadSfzFile(const char* path, const char* tuningPath) {
        std::lock_guard<std::mutex> lk(mNonRtMutex);
        mLoading = true;
        mReady = false;

        const bool okFile = mSampler->loadSfzFile(path);
        if (tuningPath) mSampler->loadScalaFile(tuningPath);

        mLoading = false;
        mReady = okFile && (mSampler->getNumRegions() > 0);
        return mReady.load();
}

    std::vector<float> mScratchL, mScratchR;

    void ensureScratch(size_t n) {
        if (mScratchL.size() < n) mScratchL.resize(n);
        if (mScratchR.size() < n) mScratchR.resize(n);
    }

    void renderAudio(float* audioData, int32_t numFrames) override {
        // safety
        if (numFrames <= 0) return;

        // clear destination (we may skip copying when muted)
        const int outCount   = mIsStereo ? 2 : 1;
        const int outSamples = numFrames * outCount;
        if (audioData) std::memset(audioData, 0, sizeof(float) * outSamples);

        // if not ready / loading, output silence
        if (!mSampler || !mReady || mLoading) return;

        // drain queued MIDI on the audio thread
        {
            uint32_t r = mMidiRead.load(std::memory_order_relaxed);
            const uint32_t w = mMidiWrite.load(std::memory_order_acquire);
            while (r != w) {
                const MidiEvent& ev = mMidiQueue[r % kMidiQueueSize];
                switch (ev.type) {
                    case MidiType::NoteOn:  mSampler->noteOn(0, ev.d1, ev.d2); break;
                    case MidiType::NoteOff: mSampler->noteOff(0, ev.d1, ev.d2); break;
                    case MidiType::CC:      mSampler->cc(0, ev.d1, ev.d2); break;
                    case MidiType::Pitch: {
                        const int pitch = ((int(ev.d2) << 7) | int(ev.d1)) - 8192;
                        mSampler->pitchWheel(0, pitch);
                        break;
                    }
                }
                ++r;
            }
            mMidiRead.store(r, std::memory_order_release);
        }

        // render in safe chunks (never exceed setSamplesPerBlock)
        int framesDone = 0;
        while (framesDone < numFrames) {
            const int framesLeft  = numFrames - framesDone;
            const int chunkFrames = (mMaxFramesPerBlock > 0)
                ? std::min(framesLeft, mMaxFramesPerBlock)
                : framesLeft;

            // make sure our scratch fits the chunk
            ensureScratch(static_cast<size_t>(chunkFrames));

            // deinterleaved scratch for Sfizz (always provide 2 channels)
            float* outsStereo[2] = { mScratchL.data(), mScratchR.data() };
            std::memset(outsStereo[0], 0, sizeof(float) * chunkFrames);
            std::memset(outsStereo[1], 0, sizeof(float) * chunkFrames);

            // ONE render call for this chunk (audio thread)
            mSampler->renderBlock(outsStereo, chunkFrames);

            // honor mute window: if active, skip copying this chunk (leave zeros)
            int muteLeft = mMuteFrames.load(std::memory_order_acquire);
            if (muteLeft > 0) {
                const int consume = std::min(muteLeft, chunkFrames);
                mMuteFrames.store(muteLeft - consume, std::memory_order_release);
            } else {
                if (mIsStereo) {
                    // interleave scratch into destination
                    const int base = framesDone * 2;
                    for (int f = 0; f < chunkFrames; ++f) {
                        audioData[base + 2 * f]     = outsStereo[0][f];
                        audioData[base + 2 * f + 1] = outsStereo[1][f];
                    }
                } else {
                    // mix down to mono
                    const int base = framesDone;
                    for (int f = 0; f < chunkFrames; ++f) {
                        audioData[base + f] = 0.5f * (outsStereo[0][f] + outsStereo[1][f]);
                    }
                }
            }

            framesDone += chunkFrames;
        }
    }

    void handleMidiEvent(uint8_t status, uint8_t data1, uint8_t data2) override {
        const uint8_t statusCode = status >> 4;
        MidiEvent ev{};

        switch (statusCode) {
            case 0x9: // Note On
                ev.type = MidiType::NoteOn;
                ev.d1 = data1; ev.d2 = data2;
                break;
            case 0x8: // Note Off
                ev.type = MidiType::NoteOff;
                ev.d1 = data1; ev.d2 = data2;
                break;
            case 0xB: // CC
                ev.type = MidiType::CC;
                ev.d1 = data1; ev.d2 = data2;
                break;
            case 0xE: { // Pitch bend
                // Repack to 14-bit value in d1/d2 for now; we’ll decode in render
                ev.type = MidiType::Pitch;
                ev.d1 = data1; ev.d2 = data2;
                break;
            }
            default:
                return; // ignore others for now
        }

        // SPSC enqueue (drop if full)
        const uint32_t w = mMidiWrite.load(std::memory_order_relaxed);
        const uint32_t r = mMidiRead.load(std::memory_order_acquire);
        if ((uint32_t)(w - r) >= kMidiQueueSize) {
            // queue full -> drop oldest or just drop this event; we drop this event
            return;
        }
        mMidiQueue[w % kMidiQueueSize] = ev;
        mMidiWrite.store(w + 1, std::memory_order_release);
    }

    void reset() override {
    }

private:
    bool mIsStereo = true;
    std::unique_ptr<sfz::Sfizz> mSampler;
};

#endif
#endif //SFIZZ_SAMPLER_INSTRUMENT_H