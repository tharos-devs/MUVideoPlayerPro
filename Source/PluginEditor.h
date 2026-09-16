#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI/HitPointsSidebar.h"
#include "UI/MUVideoPlayerProLookAndFeel.h"
#include "UI/TimelineComponent.h"
#include "UI/VideoInformationPanel.h"
#include "UI/VideoSettingsPanel.h"
#include "Video/VideoGLComponent.h"

class MUVideoPlayerProAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit MUVideoPlayerProAudioProcessorEditor(MUVideoPlayerProAudioProcessor&);
    ~MUVideoPlayerProAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Simple rectangle de couleur unie -- fond de la barre d'outils (pour
    // qu'elle se distingue du reste, plutôt que des icônes flottant sur le
    // même noir que la vidéo) et liseré sous l'onglet actif de la sidebar.
    struct ColourPanel : public juce::Component
    {
        explicit ColourPanel(juce::Colour c) : colour(c) {}
        void paint(juce::Graphics& g) override { g.fillAll(colour); }
        juce::Colour colour;
    };

    void timerCallback() override;

    // Reflète l'état réel de MuseScore (PositionCache::isPlaying()), pas un
    // simple drapeau interne au bouton : si l'utilisateur pilote la lecture
    // depuis MuseScore lui-même plutôt que depuis ce bouton, l'icône doit
    // quand même refléter l'état réel au prochain tick du Timer.
    void updatePlayPauseIcon(bool isPlaying);
    void updateMuteButtonState(bool isMuted);

    // Layout des contrôles de lecture (playerPane) / de la sidebar à onglets
    // (sidebarRoot), séparés de resized() : appelés à chaque redimensionnement.
    void layoutPlayerPane();
    void layoutSidebar();
    void selectSidebarTab(int index);

    // Rafraîchit les deux vues qui partagent le même HitPointList
    // (timelineComponent, hitPointsSidebar) après une modification survenue
    // dans l'une ou l'autre -- évite qu'elles divergent visuellement.
    void refreshHitPointViews();

    // Chemin commun après un chargement réussi (bouton Charger ou menu
    // "fichiers récents") : évite de dupliquer cette séquence à chaque appelant.
    void onVideoFileLoaded();
    void onVideoCleared();
    void showRecentFilesMenu();
    void showZoomPresetsMenu();
    void setTimelineZoom(double newZoom);

    MUVideoPlayerProAudioProcessor& audioProcessor;

    // Appliqué à tout l'éditeur : sans lui, juce::PopupMenu/TextEditor/
    // ComboBox... gardent le thème clair par défaut de LookAndFeel_V4,
    // détonnant avec le reste du plugin (cf. MUVideoPlayerProLookAndFeel.h).
    MUVideoPlayerProLookAndFeel lookAndFeel;

    // Contient tous les contrôles de lecture (vidéo, timeline, transport) --
    // séparé de l'éditeur lui-même pour partager la largeur avec sidebarRoot
    // via un StretchableLayoutManager (redimensionnable/repliable, §"c" du
    // plan de portage -- cf. PROJECT_CONTEXT.md).
    juce::Component playerPane;

    VideoGLComponent videoComponent;
    TimelineComponent timelineComponent;
    juce::Viewport timelineViewport;
    double timelineZoom = 1.0;
    static constexpr double timelineZoomMax = 10.0;

    juce::Label positionLabel;
    juce::Label volumeLabel;
    ColourPanel iconRowBackground { juce::Colour(MUVideoPlayerProLookAndFeel::panelColourArgb) };

    // ImageOnButtonBackground (pas ImageFitted) : DrawableButton::
    // shouldDrawButtonBackground() ne renvoie true, et donc n'appelle
    // LookAndFeel::drawButtonBackground() (fond + survol), que pour ce style
    // -- ImageFitted appelle drawDrawableButton() à la place, qu'on n'a pas
    // surchargé, d'où l'absence totale de fond ET de survol malgré
    // MUVideoPlayerProLookAndFeel. Ce style applique aussi automatiquement
    // une marge de getWidth()/4 autour de l'icône (voir
    // DrawableButton::getImageBounds()), qui pilote réellement la taille de
    // l'icône affichée -- pas le scale interne de filledIcon(), qui n'a
    // aucun effet sur la taille finale puisque Drawable::setTransformToFit()
    // redimensionne de toute façon l'icône pour remplir cette zone.
    juce::DrawableButton loadButton { "load", juce::DrawableButton::ImageOnButtonBackground };
    // Exception délibérée : reste un simple glyphe sans fond ni survol,
    // satellite du bouton Charger plutôt qu'un bouton à part entière.
    juce::DrawableButton recentFilesButton { "recentFiles", juce::DrawableButton::ImageFitted };
    juce::TextButton addHitPointButton { "#" };
    juce::DrawableButton rewindButton { "rewind", juce::DrawableButton::ImageOnButtonBackground };
    juce::DrawableButton playPauseButton { "playPause", juce::DrawableButton::ImageOnButtonBackground };
    juce::DrawableButton stopButton { "stop", juce::DrawableButton::ImageOnButtonBackground };
    juce::TextButton muteButton { "M" };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    juce::TextButton zoomOutButton { "-" };
    juce::Label zoomField;
    juce::TextButton zoomPresetsButton { juce::String::fromUTF8("\xE2\x96\xBE") }; // "▾"
    juce::TextButton zoomInButton { "+" };

    bool lastKnownIsPlaying = false;
    bool muted = false;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Sidebar à onglets "faite main" (deux boutons + liseré de couleur sous
    // l'actif) plutôt que juce::TabbedComponent : donne un contrôle total sur
    // l'indicateur d'onglet actif (demandé explicitement -- le fond de
    // juce::TabbedButtonBar par défaut ne suffisait pas à le distinguer).
    juce::Component sidebarRoot;
    HitPointsSidebar hitPointsSidebar;
    VideoSettingsPanel videoSettingsPanel;
    VideoInformationPanel videoInformationPanel;
    juce::TextButton hitPointsTabButton { "Hit points" };
    juce::TextButton settingsTabButton { "Settings" };
    juce::TextButton informationTabButton { "Information" };
    ColourPanel tabUnderline { juce::Colour(MUVideoPlayerProLookAndFeel::accentColourArgb) };
    int selectedSidebarTab = 0;

    juce::StretchableLayoutManager sidebarLayout;
    juce::StretchableLayoutResizerBar sidebarResizer { &sidebarLayout, 1, true };
    juce::TextButton sidebarToggleButton { juce::String::fromUTF8("\xC2\xBB") }; // "»"
    bool sidebarCollapsed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MUVideoPlayerProAudioProcessorEditor)
};
