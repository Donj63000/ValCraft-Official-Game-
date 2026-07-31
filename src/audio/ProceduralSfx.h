#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace valcraft {

enum class GameSfxKind : std::uint8_t {
    SwordSwing = 0,
    CreatureHit = 1,
    CreatureDeath = 2,
    CreatureAttack = 3,
    MusketShot = 4,
    HeavySwing = 5,
    BoneImpact = 6,
    MetalImpact = 7,
    PerfectGuard = 8,
    ChainBreak = 9,
    ColossusRoar = 10,
    Crowd = 11,
    SeaLeviathan = 12,
    JackBootStep = 13,
    JackPegStep = 14,
    JackNotice = 15,
    JackChase = 16,
    JackScreamer = 17,
};

struct ProceduralSfxRequest {
    GameSfxKind kind = GameSfxKind::SwordSwing;
    float volume = 1.0F;
    float pan = 0.0F;
    float attenuation = 1.0F;
    // Je peux imposer une graine pour rejouer exactement un evenement audite.
    // La valeur nulle laisse le mixeur produire sa sequence deterministe.
    std::uint32_t seed = 0U;
};

class ProceduralSfxMixer {
public:
    explicit ProceduralSfxMixer(int sample_rate = 48'000) noexcept;

    void set_sample_rate(int sample_rate) noexcept;
    void reset() noexcept;
    void play(const ProceduralSfxRequest& request) noexcept;
    void mix_interleaved(std::span<float> output, std::size_t channel_count) noexcept;

    [[nodiscard]] auto active_voice_count() const noexcept -> std::size_t;
    [[nodiscard]] static auto effect_duration(GameSfxKind kind) noexcept -> float;
    [[nodiscard]] static auto maximum_voice_count() noexcept -> std::size_t;

private:
    struct Voice {
        GameSfxKind kind = GameSfxKind::SwordSwing;
        float age = 0.0F;
        float duration = 0.10F;
        float volume = 0.0F;
        float pan = 0.0F;
        float phase = 0.0F;
        float secondary_phase = 0.0F;
        float previous_noise = 0.0F;
        float filtered_noise = 0.0F;
        std::uint32_t seed = 1U;
        bool active = false;
    };

    static constexpr std::size_t kMaximumVoices = 24U;

    [[nodiscard]] static auto next_noise_unit(std::uint32_t& seed) noexcept -> float;
    [[nodiscard]] auto render_voice_sample(Voice& voice) const noexcept -> float;
    [[nodiscard]] static auto soft_limit(float sample) noexcept -> float;

    Voice voices_[kMaximumVoices] {};
    int sample_rate_ = 48'000;
    std::uint32_t next_seed_ = 0x9E3779B9U;
};

} // namespace valcraft
