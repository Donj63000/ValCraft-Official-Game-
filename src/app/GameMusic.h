#pragma once

#include "audio/ProceduralMusic.h"

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace valcraft {

enum class GameSfxKind : std::uint8_t {
    SwordSwing = 0,
    CreatureHit = 1,
    CreatureDeath = 2,
    CreatureAttack = 3,
};

class GameMusic {
public:
    auto initialize() -> bool;
    void shutdown() noexcept;

    void sync_environment(const EnvironmentState& environment,
                          const CreatureCycleState& cycle,
                          bool has_active_session,
                          bool front_end_visible) noexcept;
    void pump();
    void play_sfx(GameSfxKind kind, float volume = 1.0F) noexcept;

    [[nodiscard]] auto available() const noexcept -> bool;

private:
    struct SfxVoice {
        GameSfxKind kind = GameSfxKind::SwordSwing;
        float age = 0.0F;
        float duration = 0.10F;
        float volume = 0.0F;
        float phase = 0.0F;
        std::uint32_t seed = 1U;
        bool active = false;
    };

    static constexpr std::size_t kMaxSfxVoices = 16U;

    static void audio_callback(void* userdata, Uint8* stream, int len) noexcept;
    void render_callback_stream(Uint8* stream, int len) noexcept;
    void mix_sfx(float* output, std::size_t frame_count, std::size_t channel_count) noexcept;

    [[nodiscard]] static auto sfx_duration(GameSfxKind kind) noexcept -> float;
    [[nodiscard]] static auto next_noise_unit(std::uint32_t& seed) noexcept -> float;
    [[nodiscard]] static auto render_sfx_sample(SfxVoice& voice, float sample_rate) noexcept -> float;

    SDL_AudioDeviceID device_id_ = 0;
    SDL_AudioSpec obtained_spec_ {};
    bool owns_audio_subsystem_ = false;
    ProceduralMusicComposer composer_ {};
    std::array<SfxVoice, kMaxSfxVoices> sfx_voices_ {};
    std::uint32_t sfx_seed_ = 0x9E3779B9U;
};

} // namespace valcraft
