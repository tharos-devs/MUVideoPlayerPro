#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cstdint>
#include <cstring>

// Ring buffer lock-free (SPSC) de blocs audio décodés, déjà resamplés au
// sample rate hôte et entrelacés en stéréo float32. Producteur : thread de
// décodage FFmpeg (VideoDecoder). Consommateur : processBlock() (thread
// audio), qui doit en extraire des échantillons sample-accurate, d'où des
// blocs de taille variable (issus de swr_convert()) plutôt qu'un seul
// échantillon à la fois comme VideoFrameQueue.
class AudioFrameQueue
{
public:
    static constexpr int numChannels = 2;

    struct Chunk
    {
        double ptsSeconds = 0.0;
        int numFrames = 0; // échantillons par canal
        std::vector<float> interleaved; // numFrames * numChannels

        // Même sémantique que VideoFrameQueue::Frame::generation : incrémenté
        // par le décodeur à chaque seek, pour que le consommateur repère et
        // ignore les blocs décodés avant un seek déjà pris en compte.
        uint32_t generation = 0;
    };

    explicit AudioFrameQueue(int capacity)
        : fifo(capacity), slots(static_cast<size_t>(capacity))
    {
    }

    // Thread de décodage uniquement. Retourne false si la file est pleine (le
    // décodeur doit alors attendre avant de pousser le bloc suivant).
    bool push(double ptsSeconds, int numFrames, const float* interleavedData, uint32_t generation)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 + size2 == 0)
            return false;

        const int index = size1 > 0 ? start1 : start2;
        Chunk& slot = slots[static_cast<size_t>(index)];

        slot.ptsSeconds = ptsSeconds;
        slot.numFrames = numFrames;
        slot.generation = generation;

        const size_t sampleCount = static_cast<size_t>(numFrames) * static_cast<size_t>(numChannels);
        slot.interleaved.resize(sampleCount);
        std::memcpy(slot.interleaved.data(), interleavedData, sampleCount * sizeof(float));

        fifo.finishedWrite(size1 + size2);
        return true;
    }

    // Thread audio (processBlock) uniquement.
    bool pop(Chunk& out)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead(1, start1, size1, start2, size2);

        if (size1 + size2 == 0)
            return false;

        const int index = size1 > 0 ? start1 : start2;
        out = slots[static_cast<size_t>(index)];

        fifo.finishedRead(size1 + size2);
        return true;
    }

    void reset() { fifo.reset(); }

private:
    juce::AbstractFifo fifo;
    std::vector<Chunk> slots;
};
