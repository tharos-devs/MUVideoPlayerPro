#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Video/VideoDecoder.h"

// Onglet "Settings" : fps (éditable + "Detect"), offset vidéo/partition
// (éditable + nudges ±100ms), "Clear video". Port du tab "Settings" de
// VideoHitPointsPanel.qml (panel vidéo MuseScore, PR musescore/MuseScore#34744
// -- cf. PROJECT_CONTEXT.md).
//
// Thread GUI uniquement.
class VideoSettingsPanel : public juce::Component
{
public:
    VideoSettingsPanel();

    void setSource(VideoDecoder* decoder);

    // Interrogés/appelés au clic plutôt que mis en cache, pour ne jamais
    // afficher/écrire une valeur périmée (même raisonnement que
    // HitPointsSidebar::getCurrentPositionSeconds).
    std::function<double()> getOffsetSeconds;
    std::function<void(double)> onOffsetChanged;

    std::function<void()> onClearVideo;

    // À appeler après tout chargement/déchargement de vidéo, ou toute
    // modification de fps/offset survenue ailleurs.
    void refresh();

    void resized() override;

private:
    void applyOffsetMs(int newOffsetMs);

    VideoDecoder* videoDecoder = nullptr;

    juce::Label fpsCaption;
    juce::Label fpsField;
    juce::TextButton detectButton { "Detect" };

    juce::Label offsetCaption;
    juce::Label offsetField;
    juce::Label msCaption;
    juce::TextButton offsetMinusButton { "-100 ms" };
    juce::TextButton offsetPlusButton { "+100 ms" };

    juce::TextButton clearVideoButton { "Clear video" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoSettingsPanel)
};
