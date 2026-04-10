#pragma once

#include "audio/ProceduralMusic.h"

#include <SDL.h>

namespace valcraft {

class GameMusic {
public:
    auto initialize() -> bool;
    void shutdown() noexcept;

    void sync_environment(const EnvironmentState& environment,
                          const CreatureCycleState& cycle,
                          bool has_active_session,
                          bool front_end_visible) noexcept;
    void pump();

    [[nodiscard]] auto available() const noexcept -> bool;

private:
    static void audio_callback(void* userdata, Uint8* stream, int len) noexcept;
    void render_callback_stream(Uint8* stream, int len) noexcept;

    SDL_AudioDeviceID device_id_ = 0;
    SDL_AudioSpec obtained_spec_ {};
    bool owns_audio_subsystem_ = false;
    ProceduralMusicComposer composer_ {};
};

} // namespace valcraft
