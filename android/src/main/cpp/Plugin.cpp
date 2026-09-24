#include <thread>
#include "SharedInstruments/SfizzSamplerInstrument.h"
#include "AndroidEngine/AndroidEngine.h"
#include "AndroidInstruments/SoundFontInstrument.h"
#include "Utils/OptionArray.h"
#include "Utils/Logging.h"

std::unique_ptr<AndroidEngine> engine;

bool check_engine(const char* caller = "unknown") {
    if (engine == nullptr) {
        LOGE("❌ check_engine() FAILED in %s: engine is nullptr!", caller);
        return false;
    }
    return true;
}

void setInstrumentOutputFormat(IInstrument* instrument) {
    if (!check_engine("setInstrumentOutputFormat")) return;
    auto sampleRate = engine->getSampleRate();
    auto channelCount = engine->getChannelCount();
    auto isStereo = channelCount > 1;

    instrument->setOutputFormat(sampleRate, isStereo);
}

extern "C" {
    __attribute__((visibility("default"))) __attribute__((used))
    void setup_engine(Dart_Port sampleRateCallbackPort) {
        LOGI("🚀 setup_engine() called with port %lld", (long long)sampleRateCallbackPort);
        engine = std::make_unique<AndroidEngine>(sampleRateCallbackPort);
        LOGI("🚀 setup_engine() completed successfully");
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void destroy_engine() {
        LOGI("🛑 destroy_engine() called");
        engine.reset();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void stop_all_notes() {
        LOGI("⏹️ stop_all_notes() called");
        if (!check_engine("stop_all_notes")) return;

        // Iterate over all known tracks by index
        for (track_index_t i = 0; i < 128; ++i) {
            auto instrument = engine->mSchedulerMixer.getTrack(i);
            if (instrument.has_value()) {
                auto sf2 = dynamic_cast<SoundFontInstrument*>(instrument.value());
                if (sf2) {
                    LOGI("⏹️ stop_all_notes(): stopping notes on track %d", i);
                    sf2->stopAllNotes();
                }
            }
        }
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void add_track_sf2(const char* filename, bool isAsset, int32_t presetIndex, Dart_Port callbackPort) {
        check_engine();

        std::thread([=]() {
            auto sf2Instrument = new SoundFontInstrument();
            setInstrumentOutputFormat(sf2Instrument);

            auto didLoad = sf2Instrument->loadSf2File(filename, isAsset, presetIndex);

            if (didLoad) {
                auto trackIndex = engine->mSchedulerMixer.addTrack(sf2Instrument);

                callbackToDartInt32(callbackPort, trackIndex);
            } else {
                callbackToDartInt32(callbackPort, -1);
            }

        }).detach();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void add_track_sfz(const char* filename, const char* tuningFilename, Dart_Port callbackPort) {
        check_engine();

        std::thread([=]() {
            auto sfzInstrument = new SfizzSamplerInstrument();
            setInstrumentOutputFormat(sfzInstrument);

            auto didLoad = sfzInstrument->loadSfzFile(filename, tuningFilename);

            if (didLoad) {
                auto bufferSize = engine->getBufferSize();
                sfzInstrument->setSamplesPerBlock(bufferSize);
                auto trackIndex = engine->mSchedulerMixer.addTrack(sfzInstrument);

                callbackToDartInt32(callbackPort, trackIndex);
            } else {
                callbackToDartInt32(callbackPort, -1);
            }
        }).detach();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void add_track_sfz_string(const char* sampleRoot, const char* sfzString, const char* tuningString, Dart_Port callbackPort) {
        check_engine();

        std::thread([=]() {
            auto sfzInstrument = new SfizzSamplerInstrument();
            setInstrumentOutputFormat(sfzInstrument);

            auto didLoad = sfzInstrument->loadSfzString(sampleRoot, sfzString, tuningString);

            if (didLoad) {
                auto bufferSize = engine->getBufferSize();
                sfzInstrument->setSamplesPerBlock(bufferSize);
                auto trackIndex = engine->mSchedulerMixer.addTrack(sfzInstrument);

                callbackToDartInt32(callbackPort, trackIndex);
            } else {
                callbackToDartInt32(callbackPort, -1);
            }
        }).detach();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void remove_track(track_index_t trackIndex) {
        LOGI("🗑️ remove_track(%d) called", trackIndex);
        if (!check_engine("remove_track")) return;

        engine->mSchedulerMixer.removeTrack(trackIndex);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void reset_track(track_index_t trackIndex) {
        LOGI("🔄 reset_track(%d) called", trackIndex);
        if (!check_engine("reset_track")) return;

        engine->mSchedulerMixer.resetTrack(trackIndex);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    float get_track_volume(track_index_t trackIndex) {
        if (!check_engine("get_track_volume")) return 0.0f;

        return engine->mSchedulerMixer.getLevel(trackIndex);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    int32_t get_position() {
        if (!check_engine("get_position")) return 0;

        return engine->mSchedulerMixer.getPosition();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    uint64_t get_last_render_time_us() {
        if (!check_engine("get_last_render_time_us")) return 0;

        return engine->mSchedulerMixer.getLastRenderTimeUs();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    uint32_t get_buffer_available_count(track_index_t trackIndex) {
        if (!check_engine("get_buffer_available_count")) return 0;

        return engine->mSchedulerMixer.getBufferAvailableCount(trackIndex);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void handle_events_now(track_index_t trackIndex, const uint8_t* eventData, int32_t eventsCount) {
        if (!check_engine("handle_events_now")) return;

        SchedulerEvent events[eventsCount];

        rawEventDataToEvents(eventData, eventsCount, events);

        engine->mSchedulerMixer.handleEventsNow(trackIndex, events, eventsCount);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    int32_t schedule_events(track_index_t trackIndex, const uint8_t* eventData, int32_t eventsCount) {
        if (!check_engine("schedule_events")) return 0;

        SchedulerEvent events[eventsCount];

        rawEventDataToEvents(eventData, eventsCount, events);

        return engine->mSchedulerMixer.scheduleEvents(trackIndex, events, eventsCount);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void clear_events(track_index_t trackIndex, position_frame_t fromFrame) {
        if (!check_engine("clear_events")) return;

        return engine->mSchedulerMixer.clearEvents(trackIndex, fromFrame);
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void engine_play() {
        LOGI("▶️ engine_play() called");
        if (!check_engine("engine_play")) return;

        engine->play();
    }

    __attribute__((visibility("default"))) __attribute__((used))
    void engine_pause() {
        LOGI("⏸️ engine_pause() called");
        if (!check_engine("engine_pause")) return;

        engine->pause();
    }
}
