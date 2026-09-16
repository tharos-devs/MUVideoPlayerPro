#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cstdint>

// Ring buffer lock-free (SPSC) de frames vidéo décodées, converties en RGBA.
// Producteur : thread de décodage FFmpeg. Consommateur : thread de rendu GUI
// (Phase 4). Capacité fixe allouée une fois, pas d'allocation dans push/pop.
class VideoFrameQueue
{
public:
    struct Frame
    {
        double ptsSeconds = 0.0;
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba; // width * height * 4 octets

        // Incrémenté par le décodeur à chaque seek effectué (VideoDecoder::
        // performSeek). Permet au consommateur de repérer et d'ignorer les
        // frames décodées avant un seek déjà pris en compte (PTS non
        // monotone dans la file juste après un saut arrière).
        uint32_t generation = 0;
    };

    explicit VideoFrameQueue(int capacity)
        : fifo(capacity), slots(static_cast<size_t>(capacity))
    {
    }

    // Thread de décodage uniquement. Retourne false si la file est pleine
    // (le décodeur doit alors attendre avant de décoder la frame suivante).
    bool push(double ptsSeconds, int width, int height, const uint8_t* rgbaData, uint32_t generation)
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 + size2 == 0)
            return false;

        const int index = size1 > 0 ? start1 : start2;
        Frame& slot = slots[static_cast<size_t>(index)];

        slot.ptsSeconds = ptsSeconds;
        slot.width = width;
        slot.height = height;
        slot.generation = generation;

        const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        slot.rgba.resize(byteCount);
        std::memcpy(slot.rgba.data(), rgbaData, byteCount);

        fifo.finishedWrite(size1 + size2);
        return true;
    }

    // Thread de rendu GUI uniquement.
    bool pop(Frame& out)
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

    int getNumReady() const { return fifo.getNumReady(); }
    void reset() { fifo.reset(); }

private:
    juce::AbstractFifo fifo;
    std::vector<Frame> slots;
};
