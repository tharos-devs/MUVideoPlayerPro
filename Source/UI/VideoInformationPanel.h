#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Video/VideoDecoder.h"

// Métadonnées en lecture seule de la vidéo courante -- port minimal du tab
// "Information" de VideoHitPointsPanel.qml (panel vidéo MuseScore, PR
// musescore/MuseScore#34744 -- cf. PROJECT_CONTEXT.md) : fichier, résolution,
// durée, débit (fps) et codecs vidéo/audio seulement, comme prévu à l'étape
// 6d du plan de portage. Les champs supplémentaires de l'original (bitrate,
// canaux/sample rate audio, format conteneur...) ne sont pas exposés par
// VideoDecoder aujourd'hui et restent hors périmètre de cette étape.
//
// Thread GUI uniquement.
class VideoInformationPanel : public juce::Component
{
public:
    VideoInformationPanel();

    void setSource(const VideoDecoder* decoder);

    // Interrogé par refresh() -- pas une copie mise en cache, pour ne
    // jamais afficher un chemin périmé après un rechargement de fichier.
    std::function<juce::File()> getCurrentFile;

    // À appeler après tout chargement/déchargement de vidéo pour refléter
    // les nouvelles métadonnées.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    const VideoDecoder* videoDecoder = nullptr;

    juce::Label fileNameValue;
    juce::Label resolutionValue;
    juce::Label durationValue;
    juce::Label frameRateValue;
    juce::Label videoCodecValue;
    juce::Label audioCodecValue;

    // { légende dessinée dans paint(), composant valeur associé } -- ordre
    // d'affichage des lignes.
    std::array<std::pair<juce::String, juce::Label*>, 6> rows;

    static constexpr int rowHeight = 22;
    static constexpr int labelColumnWidth = 90;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoInformationPanel)
};
