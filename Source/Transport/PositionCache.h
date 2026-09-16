#pragma once

#include <atomic>
#include <cstdint>

// Cache atomique de la position/état de lecture MuseScore, écrit depuis
// processBlock() (thread audio) et lu depuis le thread GUI. Pas de lock.
class PositionCache
{
public:
    void update(int64_t samplePositionIn, double sampleRateIn, bool isPlayingIn) noexcept
    {
        samplePosition.store(samplePositionIn, std::memory_order_relaxed);
        sampleRate.store(sampleRateIn, std::memory_order_relaxed);
        playing.store(isPlayingIn, std::memory_order_relaxed);
    }

    double positionSeconds() const noexcept
    {
        const double rate = sampleRate.load(std::memory_order_relaxed);
        if (rate <= 0.0)
            return 0.0;

        return static_cast<double>(samplePosition.load(std::memory_order_relaxed)) / rate;
    }

    bool isPlaying() const noexcept
    {
        return playing.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int64_t> samplePosition { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<bool> playing { false };
};
