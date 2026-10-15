// SDL2 sound output for the RT64 build.
//
// The game's audio thread hands over each buffer the audio microcode mixed
// (osAiSetNextBuffer) and paces itself by how much is still queued
// (osAiGetLength), so the SDL device is used in queue mode: the reported
// remaining frames are what SDL hasn't played yet.
//
// The sound running out (issue #136, crackling at 240 fps): the queue is kept short (get_frames_remaining),
// so a moment the game's audio thread is late (a busy CPU) can let SDL play it dry, a click. Each time the
// queue is found empty as a buffer comes in, soon after the one before (not a stop on purpose, as loading
// or at the start) and after one that wasn't silent (no click then), it's logged, and the queue is kept a
// little longer from then on, up to a buffer more. After a minute without running out it's kept a step
// shorter again: a moment the game stalls (choosing a save file runs it out a few times) costs a little
// delay for a minute or two, and anyone whose sound never runs out has no more delay than before.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

#include <SDL.h>

#include "recompui/config.h"

#include "conker.hpp"

namespace {
    constexpr int channels = 2;

    using clock = std::chrono::steady_clock;

    // A buffer of the game's (frames): func_100095A0 makes 736, or 552 when it finds 249 or more left.
    constexpr uint32_t one_buffer = 736;
    // How much longer the queue is kept each time it runs out, and at most.
    constexpr uint32_t cushion_step = one_buffer / 4;
    constexpr uint32_t cushion_max = one_buffer;
    // A buffer arriving this long after the one before, to an empty queue, is a stop on purpose (loading,
    // the start), not the sound running out.
    constexpr auto late_limit = std::chrono::milliseconds(250);
    // How long without running out before the queue is kept a step shorter again.
    constexpr auto settle_time = std::chrono::seconds(60);

    std::mutex audio_mutex;
    SDL_AudioDeviceID device = 0;
    std::vector<float> convert_buffer;
    uint32_t device_frequency = 0;
    uint32_t cushion = 0;           // frames kept queued beyond the usual
    uint32_t ran_out = 0;           // times the sound ran out
    bool queued_before = false;     // a buffer has been queued since the device opened
    clock::time_point last_queued;
    clock::time_point last_logged;
    clock::time_point last_change;  // the cushion's last change

    // The loudest sample of the last buffer, as played. Running out after silence makes no click (the
    // game's sound starting, a few times in its first seconds), so it doesn't count.
    float last_peak = 0.0f;
    constexpr float silence = 4.0f / 32768.0f;

    uint32_t cushion_ms() {
        return device_frequency != 0 ? cushion * 1000 / device_frequency : 0;
    }

    // As a buffer comes in: whether the sound ran out before it, and if so a longer queue from now on; or,
    // after a while without, a shorter one again.
    void check_ran_out(const clock::time_point now) {
        const bool empty = SDL_GetQueuedAudioSize(device) == 0;
        const bool soon = queued_before && now - last_queued < late_limit;
        last_queued = now;
        queued_before = true;
        if (!empty || !soon || last_peak <= silence) {
            if (cushion > 0 && now - last_change >= settle_time) {
                cushion -= std::min(cushion, cushion_step);
                last_change = now;
                std::printf("[audio] the sound hasn't run out for a minute: %u ms more is kept queued now\n", cushion_ms());
                std::fflush(stdout);
            }
            return;
        }
        ran_out++;
        cushion = std::min(cushion + cushion_step, cushion_max);
        last_change = now;
        if (now - last_logged >= std::chrono::seconds(1)) {
            last_logged = now;
            std::printf("[audio] the sound ran out (%u time%s so far): %u ms more is kept queued now\n",
                ran_out, ran_out == 1 ? "" : "s", cushion_ms());
            std::fflush(stdout);
        }
    }

    void open_device(uint32_t frequency) {
        if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::fprintf(stderr, "[audio] SDL audio init failed: %s\n", SDL_GetError());
            return;
        }
        if (device != 0) {
            SDL_CloseAudioDevice(device);
            device = 0;
        }
        SDL_AudioSpec want{};
        want.freq = (int)frequency;
        want.format = AUDIO_F32SYS;
        want.channels = channels;
        want.samples = 256; // ~12 ms at 22 kHz: SDL pulls from the queue in chunks this size
        // No allowed changes: SDL converts to whatever the output device uses, so the
        // queue stays in the game's sample rate.
        device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (device == 0) {
            std::fprintf(stderr, "[audio] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            return;
        }
        device_frequency = frequency;
        queued_before = false;
        SDL_PauseAudioDevice(device, 0);
    }
}

void conker::audio::set_frequency(uint32_t frequency) {
    std::lock_guard lock{audio_mutex};
    open_device(frequency);
}

void conker::audio::queue_samples(int16_t* samples, size_t sample_count) {
    std::lock_guard lock{audio_mutex};
    if (device == 0) {
        return;
    }
    // RDRAM is kept as native-endian 32-bit words, so each stereo frame's two 16-bit
    // samples read back swapped: [i + 1] is the left channel and [i] the right.
    // Scaled by the main volume from the Sound settings (0-100).
    const float scale = static_cast<float>(recompui::config::sound::get_main_volume()) / (100.0f * 32768.0f);
    check_ran_out(clock::now());
    convert_buffer.resize(sample_count);
    for (size_t i = 0; i + 1 < sample_count; i += channels) {
        convert_buffer[i + 0] = samples[i + 1] * scale;
        convert_buffer[i + 1] = samples[i + 0] * scale;
    }
    last_peak = 0.0f;
    for (float v : convert_buffer) {
        last_peak = std::max(last_peak, v < 0.0f ? -v : v);
    }
    SDL_QueueAudio(device, convert_buffer.data(), (Uint32)(sample_count * sizeof(float)));
}

size_t conker::audio::get_frames_remaining() {
    std::lock_guard lock{audio_mutex};
    if (device == 0) {
        return 0;
    }
    // On hardware AI_LEN only counts what is left of the buffer playing now, not the
    // one queued behind it. Conker's audio thread (func_100095A0) makes its next
    // buffer short (552 frames instead of 736) whenever 249 or more samples are
    // left, so reporting the whole SDL queue makes it underrun and pop. Leave one
    // buffer out, so the queue settles at about a buffer and a half, and the
    // cushion more once the sound has run out (check_ran_out).
    const uint32_t left_out = one_buffer + cushion;
    uint32_t queued = SDL_GetQueuedAudioSize(device) / (channels * sizeof(float));
    return queued > left_out ? queued - left_out : 0;
}
