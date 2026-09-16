#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Transport/HitPointList.h"
#include "../Transport/PositionCache.h"
#include "../Video/VideoDecoder.h"

// Timeline vidéo : bandeau de marqueurs (hit points) au-dessus d'une piste
// graduée avec la tête de lecture. Port du "Zone 1 / Zone 2" de VideoPanel.qml
// (panel vidéo MuseScore, PR musescore/MuseScore#34744 — jamais fusionnée en
// amont, cf. PROJECT_CONTEXT.md), sans le zoom/défilement horizontal (hors
// périmètre de cette étape) ni la ligne de numéros de mesure (pas de
// partition côté plugin) : la durée totale occupe toute la largeur du
// composant.
//
// Thread GUI uniquement. setSource() doit être appelé avant tout affichage ;
// les pointeurs passés doivent rester valides tant qu'ils sont attachés
// (passer nullptr pour détacher avant destruction).
class TimelineComponent : public juce::Component
{
public:
    TimelineComponent();
    ~TimelineComponent() override;

    void setSource(const VideoDecoder* decoder, const PositionCache* newPositionCache, HitPointList* newHitPoints);

    // À appeler périodiquement (ex : depuis le Timer 30Hz déjà utilisé par
    // l'éditeur pour VideoGLComponent::pumpFromQueue()) pour faire avancer la
    // tête de lecture affichée.
    void updatePlayhead();

    // Piloté par le clic/glisser sur la piste : la timeline ne fait jamais
    // elle-même de seek, elle se contente de le demander (le transport réel
    // est piloté via MMC, la vidéo suit ensuite la position réelle de
    // MuseScore comme partout ailleurs dans ce plugin).
    std::function<void(double seconds)> onSeek;

    // Appelé après toute modification des hit points déclenchée depuis cette
    // timeline (ajout/suppression/renommage/déplacement), pour que
    // l'appelant persiste l'état ou rafraîchisse une autre vue.
    std::function<void()> onHitPointsChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    static constexpr int rulerHeight = 20;
    static constexpr int rulerTrackGap = 2;
    static constexpr int scrubSeekIntervalMs = 80; // même throttle que VideoPanel.qml

    juce::Rectangle<int> rulerBounds() const;
    juce::Rectangle<int> trackBounds() const;

    double durationSeconds() const noexcept;
    double xToSeconds(float x, const juce::Rectangle<int>& bounds) const noexcept;
    float secondsToX(double seconds, const juce::Rectangle<int>& bounds) const noexcept;

    // Étiquette "#N"/rectangle d'un marqueur dans le bandeau, pour le
    // dessin et le hit-test (partagés pour rester cohérents).
    struct MarkerLayout
    {
        int id;
        float lineX;
        juce::Rectangle<float> labelBounds;
        juce::String label;
    };
    std::vector<MarkerLayout> layoutMarkers(const juce::Rectangle<int>& bounds) const;
    int markerAt(juce::Point<float> position) const;

    void beginRenameEditor(int hitPointId);
    void endRenameEditor(bool commit);
    void showContextMenu(int hitPointId);
    void seekTo(double seconds);
    void notifyHitPointsChanged();

    const VideoDecoder* videoDecoder = nullptr;
    const PositionCache* positionCache = nullptr;
    HitPointList* hitPoints = nullptr;

    double displayedPositionSeconds = 0.0;

    // Marqueur pressé (bouton gauche), pas encore forcément en cours de
    // glissement : un simple clic sans mouvement ne doit pas re-snapper le
    // marqueur sur sa propre position (même souci que VideoPanel.qml, cf. son
    // commentaire sur onPressed) — seul un vrai mouvement (mouseDrag) le
    // promeut en glissement réel (draggingHitPointId).
    int pressedHitPointId = -1;

    // Glissement en cours d'un hit point (bandeau) : suit l'id déplacé et sa
    // position provisoire, appliquée à HitPointList seulement au relâchement
    // (même principe que draggingHitPointId/draggingHitPointTimeMs côté QML :
    // évite un flot d'écritures — et donc de notifyHitPointsChanged() — à
    // chaque pixel parcouru pendant le glissement).
    int draggingHitPointId = -1;
    double draggingHitPointSeconds = 0.0;

    bool scrubbing = false;
    int64_t lastScrubSeekMs = 0;

    std::unique_ptr<juce::TextEditor> renameEditor;
    int renamingHitPointId = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};
