#pragma once

#include "audio/ProceduralSfx.h"
#include "audio/ProceduralMusic.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace valcraft {

struct GameMusicContextInput {
    bool has_active_session = false;
    bool front_end_visible = false;
    bool maritime_gameplay_active = false;
    float voyage_motion = 0.0F;
    float danger = 0.0F;
    int world_seed = 0;
};

[[nodiscard]] inline auto make_game_music_context(const GameMusicContextInput& input) noexcept
    -> ProceduralMusicContext {
    constexpr std::uint32_t kMaritimeSeedSalt = 0xA17E5EA5U;
    constexpr std::uint32_t kSeedFallback = 0x9E3779B9U;

    ProceduralMusicContext context {};
    context.seed = static_cast<std::uint32_t>(input.world_seed) ^ kMaritimeSeedSalt;
    if (context.seed == 0U) {
        context.seed = kSeedFallback;
    }

    if (!input.has_active_session || input.front_end_visible || !input.maritime_gameplay_active) {
        return context;
    }

    // Je borne les signaux applicatifs avant de les transmettre au syntheseur.
    const auto sanitize_unit = [](float value) noexcept {
        return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
    };

    context.scene = ProceduralMusicScene::SeaAdventure;
    context.voyage_motion = sanitize_unit(input.voyage_motion);
    context.danger = sanitize_unit(input.danger);
    return context;
}

class GameMusic {
public:
    auto initialize() -> bool;
    void shutdown() noexcept;

    void sync_environment(const EnvironmentState& environment,
                          const CreatureCycleState& cycle,
                          bool has_active_session,
                          bool front_end_visible,
                          const ProceduralMusicContext& context) noexcept;
    void pump();
    void play_sfx(GameSfxKind kind,
                  float volume = 1.0F,
                  float pan = 0.0F,
                  float attenuation = 1.0F,
                  std::uint32_t deterministic_seed = 0U) noexcept;

    [[nodiscard]] auto available() const noexcept -> bool;

private:
    static void audio_callback(void* userdata, Uint8* stream, int len) noexcept;
    void render_callback_stream(Uint8* stream, int len) noexcept;

    SDL_AudioDeviceID device_id_ = 0;
    SDL_AudioSpec obtained_spec_ {};
    bool owns_audio_subsystem_ = false;
    ProceduralMusicComposer composer_ {};
    ProceduralSfxMixer sfx_mixer_ {};
};

} // namespace valcraft
