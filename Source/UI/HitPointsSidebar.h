#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Transport/HitPointList.h"
#include "../Video/VideoDecoder.h"

class HitPointRowComponent;

// Liste des hit points de la vidéo courante : bouton d'ajout, en-tête de
// colonnes, lignes éditables (timecode/nom) avec suppression. Port du
// "Hit points" tab de VideoHitPointsPanel.qml/VideoHitPointRow.qml (panel
// vidéo MuseScore, PR musescore/MuseScore#34744 — cf. PROJECT_CONTEXT.md),
// sans la colonne Mesure (pas de partition côté plugin) ni de glissé-déposé
// pour réordonner : les hit points sont toujours triés chronologiquement,
// jamais réordonnés manuellement, ici comme en amont (cf. HitPointList).
//
// Thread GUI uniquement.
class HitPointsSidebar : public juce::Component
{
public:
    HitPointsSidebar();
    ~HitPointsSidebar() override;

    void setSource(const VideoDecoder* decoder, HitPointList* newHitPoints);

    // Appelé quand l'utilisateur clique le timecode d'une ligne, ou après
    // "Add hit point" pour recentrer la vue -- même contrat que
    // TimelineComponent::onSeek (ne pilote jamais le transport directement).
    std::function<void(double seconds)> onSeek;

    // "Add hit point" ajoute au point de lecture courant (comme
    // VideoPanelModel::addHitPoint(video.position)) -- interrogé au clic,
    // pas mis en cache, pour toujours refléter la position réelle.
    std::function<double()> getCurrentPositionSeconds;

    std::function<void()> onHitPointsChanged;

    // À appeler après toute modification des hit points survenue ailleurs
    // (ex : TimelineComponent) pour reconstruire la liste affichée.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildRows();
    void layoutRows();
    void notifyHitPointsChanged();
    double frameRate() const noexcept;

    const VideoDecoder* videoDecoder = nullptr;
    HitPointList* hitPoints = nullptr;

    juce::TextButton addButton { "Add hit point" };
    juce::Viewport rowsViewport;
    juce::Component rowsContainer;
    juce::OwnedArray<HitPointRowComponent> rows;

    static constexpr int headerHeight = 20;
    static constexpr int rowHeight = 24;
    static constexpr int rowSpacing = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HitPointsSidebar)
};
