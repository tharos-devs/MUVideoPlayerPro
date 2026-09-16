#include "PluginEditor.h"

namespace
{
juce::String formatPosition(double seconds)
{
    const bool negative = seconds < 0.0;
    seconds = std::abs(seconds);

    const int hours = static_cast<int>(seconds / 3600.0);
    const int minutes = static_cast<int>(seconds / 60.0) % 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int centis = static_cast<int>((seconds - std::floor(seconds)) * 100.0);

    return juce::String::formatted("%s%02d:%02d:%02d.%02d",
                                    negative ? "-" : "",
                                    hours, minutes, secs, centis);
}

// Icônes dessinées à la main (pas d'assets image) dans un carré normalisé
// 0..100, pour un rendu net à n'importe quelle taille de bouton.
//
// NOTE : pas de scale/marge appliqué ici -- DrawableButton::resized() appelle
// toujours Drawable::setTransformToFit() pour remplir getImageBounds()
// (l'aire de dessin déterminée par le style du bouton, cf. son commentaire
// dans PluginEditor.h), donc un pré-rétrécissement du Path ici n'a aucun
// effet sur la taille réellement affichée -- seule la marge de
// getImageBounds() (pilotée par le style ImageOnButtonBackground) la
// contrôle. Un ancien scale=0.36 ici était du code mort, gardé par erreur
// après plusieurs tentatives de "réduire l'icône" qui ne pouvaient
// structurellement pas fonctionner.
std::unique_ptr<juce::Drawable> filledIcon(const juce::Path& path)
{
    auto drawable = std::make_unique<juce::DrawablePath>();
    drawable->setPath(path);
    drawable->setFill(juce::Colours::white);
    return drawable;
}

juce::Path playIconPath()
{
    juce::Path p;
    p.addTriangle(26.0f, 16.0f, 26.0f, 84.0f, 84.0f, 50.0f);
    return p;
}

juce::Path pauseIconPath()
{
    juce::Path p;
    p.addRoundedRectangle(22.0f, 16.0f, 20.0f, 68.0f, 4.0f);
    p.addRoundedRectangle(58.0f, 16.0f, 20.0f, 68.0f, 4.0f);
    return p;
}

juce::Path stopIconPath()
{
    juce::Path p;
    p.addRoundedRectangle(20.0f, 20.0f, 60.0f, 60.0f, 8.0f);
    return p;
}

juce::Path loadIconPath()
{
    juce::Path p;
    p.startNewSubPath(15.0f, 28.0f);
    p.lineTo(40.0f, 28.0f);
    p.lineTo(48.0f, 38.0f);
    p.lineTo(85.0f, 38.0f);
    p.lineTo(85.0f, 78.0f);
    p.lineTo(15.0f, 78.0f);
    p.closeSubPath();
    return p;
}

// Petit chevron, pour le bouton "fichiers récents" à côté du bouton charger.
juce::Path chevronDownIconPath()
{
    juce::Path triangle;
    triangle.addTriangle(20.0f, 35.0f, 80.0f, 35.0f, 50.0f, 75.0f);
    return triangle;
}

// "Revenir au début" : une barre verticale suivie d'un triangle pointant vers
// elle (convention ⏮ standard des lecteurs).
juce::Path rewindIconPath()
{
    juce::Path p;
    p.addRoundedRectangle(15.0f, 18.0f, 10.0f, 64.0f, 2.0f);
    p.addTriangle(85.0f, 18.0f, 85.0f, 82.0f, 30.0f, 50.0f);
    return p;
}

// Le menu "fichiers récents" se dimensionne sur son entrée la plus large --
// un nom de fichier long rendait le menu disproportionné (retour
// utilisateur). Tronque au milieu plutôt qu'à la fin : le début (souvent le
// plus identifiant) et l'extension restent visibles.
juce::String truncateForMenu(const juce::String& text, int maxChars)
{
    if (text.length() <= maxChars)
        return text;

    const int keepEachSide = (maxChars - 1) / 2;
    return text.substring(0, keepEachSide) + juce::String::fromUTF8("\xE2\x80\xA6") // "…"
         + text.substring(text.length() - keepEachSide);
}
}

MUVideoPlayerProAudioProcessorEditor::MUVideoPlayerProAudioProcessorEditor(MUVideoPlayerProAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(playerPane);

    playerPane.addAndMakeVisible(iconRowBackground);

    positionLabel.setJustificationType(juce::Justification::centred);
    positionLabel.setFont(juce::Font(juce::FontOptions(20.0f).withStyle("Bold")));
    positionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    playerPane.addAndMakeVisible(positionLabel);

    volumeLabel.setText("Volume", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centredRight);
    volumeLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    volumeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    playerPane.addAndMakeVisible(volumeLabel);

    playerPane.addAndMakeVisible(videoComponent);
    videoComponent.setSource(&audioProcessor.getVideoFrameQueue(), &audioProcessor.getPositionCache(),
                              &audioProcessor.getVideoDecoder());
    // Reflète l'état déjà restauré côté processeur (setStateInformation a pu
    // s'exécuter avant l'ouverture de cette fenêtre, ex : rechargement de
    // projet) : sans ça, l'éditeur repartirait toujours sur les valeurs par
    // défaut du code ci-dessous au lieu du volume/mute/offset réellement actifs.
    videoComponent.setOffsetSeconds(audioProcessor.getOffsetSeconds());

    timelineViewport.setViewedComponent(&timelineComponent, false);
    timelineViewport.setScrollBarsShown(false, true);
    timelineViewport.setScrollBarThickness(6);
    playerPane.addAndMakeVisible(timelineViewport);
    timelineComponent.setSource(&audioProcessor.getVideoDecoder(), &audioProcessor.getPositionCache(),
                                 &audioProcessor.getHitPoints());
    // Ni la timeline ni la sidebar ne pilotent jamais le transport elles-
    // mêmes : un clic/glisser (timeline) ou "Add hit point"/clic sur un
    // timecode (sidebar) demande juste un Locate MMC, comme les autres
    // contrôles de ce plugin (playPauseButton, etc.) -- la vidéo suit ensuite
    // la position réelle de MuseScore une fois revenue via le ProcessContext,
    // via SeekPolicy.
    timelineComponent.onSeek = [this](double seconds) { audioProcessor.requestLocate(seconds); };
    // Les deux vues partagent le même HitPointList (audioProcessor.getHitPoints()) --
    // une modification faite dans l'une doit rafraîchir l'autre pour qu'elles
    // ne divergent pas visuellement.
    timelineComponent.onHitPointsChanged = [this] { refreshHitPointViews(); };

    // Callbacks branchés avant setSource(), même précaution que
    // videoSettingsPanel plus bas : setSource() déclenche un refresh()
    // immédiat qui pourrait un jour dépendre de l'un de ces callbacks.
    hitPointsSidebar.onSeek = [this](double seconds) { audioProcessor.requestLocate(seconds); };
    hitPointsSidebar.getCurrentPositionSeconds = [this] { return audioProcessor.getPositionCache().positionSeconds(); };
    hitPointsSidebar.onHitPointsChanged = [this] { refreshHitPointViews(); };
    hitPointsSidebar.setSource(&audioProcessor.getVideoDecoder(), &audioProcessor.getHitPoints());
    sidebarRoot.addAndMakeVisible(hitPointsSidebar);

    // Les callbacks doivent être branchés AVANT setSource() : celui-ci
    // déclenche un refresh() immédiat, qui lisait getOffsetSeconds() alors
    // encore nul si appelé après -- le champ Offset restait vide au premier
    // affichage (retour utilisateur).
    videoSettingsPanel.getOffsetSeconds = [this] { return audioProcessor.getOffsetSeconds(); };
    videoSettingsPanel.onOffsetChanged = [this](double seconds)
    {
        audioProcessor.setOffsetSeconds(seconds);
        videoComponent.setOffsetSeconds(seconds);
    };
    videoSettingsPanel.onClearVideo = [this]
    {
        audioProcessor.clearVideoFile();
        onVideoCleared();
    };
    videoSettingsPanel.setSource(&audioProcessor.getVideoDecoder());
    sidebarRoot.addAndMakeVisible(videoSettingsPanel);

    videoInformationPanel.getCurrentFile = [this] { return audioProcessor.getCurrentVideoFile(); };
    videoInformationPanel.setSource(&audioProcessor.getVideoDecoder());
    sidebarRoot.addAndMakeVisible(videoInformationPanel);

    hitPointsTabButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    hitPointsTabButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    hitPointsTabButton.onClick = [this] { selectSidebarTab(0); };
    sidebarRoot.addAndMakeVisible(hitPointsTabButton);

    settingsTabButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    settingsTabButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    settingsTabButton.onClick = [this] { selectSidebarTab(1); };
    sidebarRoot.addAndMakeVisible(settingsTabButton);

    informationTabButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    informationTabButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    informationTabButton.onClick = [this] { selectSidebarTab(2); };
    sidebarRoot.addAndMakeVisible(informationTabButton);

    sidebarRoot.addAndMakeVisible(tabUnderline);
    selectSidebarTab(0);

    addAndMakeVisible(sidebarRoot);
    addAndMakeVisible(sidebarResizer);

    sidebarToggleButton.setTooltip("Show/hide hit points");
    sidebarToggleButton.onClick = [this]
    {
        sidebarCollapsed = !sidebarCollapsed;
        sidebarRoot.setVisible(!sidebarCollapsed);
        sidebarResizer.setVisible(!sidebarCollapsed);
        sidebarToggleButton.setButtonText(juce::String::fromUTF8(sidebarCollapsed ? "\xC2\xAB" : "\xC2\xBB"));
        resized();
    };
    playerPane.addAndMakeVisible(sidebarToggleButton);

    // Item 0 = playerPane (large assez pour garder la barre d'outils
    // lisible : icônes de transport + zoom + volume tiennent ensemble à
    // partir de cette largeur). Item 1 = poignée de redimensionnement
    // (largeur fixe). Item 2 = sidebar (mêmes bornes que
    // hitPointsPanelMin/MaxWidth côté MuseScore, réduites pour ce plugin
    // plus compact).
    sidebarLayout.setItemLayout(0, 480, -1.0, -0.72);
    sidebarLayout.setItemLayout(1, 6, 6, 6);
    // Minimum relevé à 280 (depuis 180) : 3 onglets ("Hit points"/"Settings"/
    // "Information", 90px chacun) ne tiennent plus dans l'ancien minimum.
    sidebarLayout.setItemLayout(2, 280, 380, -0.28);

    loadButton.setImages(filledIcon(loadIconPath()).get());
    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose a video", juce::File(), "*.mp4;*.mov;*.m4v");

        const auto flags = juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
        {
            const juce::File file = chooser.getResult();

            if (file.existsAsFile() && audioProcessor.loadVideoFile(file))
                onVideoFileLoaded();
        });
    };

    recentFilesButton.setImages(filledIcon(chevronDownIconPath()).get());
    recentFilesButton.setTooltip("Recent files");
    recentFilesButton.onClick = [this] { showRecentFilesMenu(); };

    addHitPointButton.setTooltip("Add hit point at current position");
    addHitPointButton.setEnabled(false);
    addHitPointButton.onClick = [this]
    {
        const double seconds = audioProcessor.getPositionCache().positionSeconds();
        audioProcessor.getHitPoints().add(juce::roundToInt(seconds * 1000.0));
        refreshHitPointViews();
    };

    rewindButton.setImages(filledIcon(rewindIconPath()).get());
    rewindButton.setTooltip("Rewind to start");
    rewindButton.setEnabled(false);
    rewindButton.onClick = [this] { audioProcessor.requestLocate(0.0); };

    updatePlayPauseIcon(false);
    playPauseButton.setEnabled(false);
    playPauseButton.onClick = [this]
    {
        // Bascule selon l'état RÉEL de MuseScore, pas un drapeau local : si la
        // lecture a été pilotée depuis MuseScore lui-même entre-temps, ce
        // bouton doit quand même faire la bonne action.
        if (audioProcessor.getPositionCache().isPlaying())
            audioProcessor.requestPause();
        else
            audioProcessor.requestPlay();
    };

    stopButton.setImages(filledIcon(stopIconPath()).get());
    stopButton.setEnabled(false);
    stopButton.onClick = [this] { audioProcessor.requestStop(); };

    muted = audioProcessor.isMuted();
    updateMuteButtonState(muted);
    muteButton.setEnabled(false);
    muteButton.onClick = [this]
    {
        muted = !muted;
        audioProcessor.setMuted(muted);
        updateMuteButtonState(muted);
    };

    volumeSlider.setRange(0.0, 1.0);
    volumeSlider.setValue(audioProcessor.getVolume(), juce::dontSendNotification);
    volumeSlider.setDoubleClickReturnValue(true, 1.0);
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colours::grey);
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    volumeSlider.setColour(juce::Slider::backgroundColourId, juce::Colours::darkgrey);
    volumeSlider.setEnabled(false);
    volumeSlider.onValueChange = [this]
    {
        audioProcessor.setVolume((float) volumeSlider.getValue());
    };

    zoomOutButton.setTooltip("Zoom out");
    zoomOutButton.setEnabled(false);
    zoomOutButton.onClick = [this] { setTimelineZoom(timelineZoom - 0.25); };

    zoomField.setJustificationType(juce::Justification::centred);
    zoomField.setFont(juce::Font(juce::FontOptions(10.0f)));
    zoomField.setColour(juce::Label::textColourId, juce::Colours::white);
    zoomField.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    zoomField.setEditable(true, true, false);
    zoomField.setEnabled(false);
    zoomField.setText("100", juce::dontSendNotification);
    zoomField.onTextChange = [this]
    {
        const int percent = zoomField.getText().getIntValue();
        if (percent > 0)
            setTimelineZoom(percent / 100.0);
        else
            zoomField.setText(juce::String(juce::roundToInt(timelineZoom * 100.0)), juce::dontSendNotification);
    };
    playerPane.addAndMakeVisible(zoomField);

    zoomPresetsButton.setTooltip("Zoom presets");
    zoomPresetsButton.setEnabled(false);
    zoomPresetsButton.onClick = [this] { showZoomPresetsMenu(); };

    zoomInButton.setTooltip("Zoom in");
    zoomInButton.setEnabled(false);
    zoomInButton.onClick = [this] { setTimelineZoom(timelineZoom + 0.25); };

    // Même fond gris arrondi que les boutons texte ("#", "M"...) hérité du
    // LookAndFeel global -- sans override ici, contrairement à avant : les
    // icônes flottant sur fond transparent tranchaient avec les boutons texte
    // et paraissaient de tailles/styles disparates (retour utilisateur).
    for (auto* button : { &loadButton, &rewindButton, &playPauseButton, &stopButton })
        playerPane.addAndMakeVisible(button);

    // Exception délibérée : la flèche "fichiers récents" reste un simple
    // glyphe sans fond, satellite du bouton Charger plutôt qu'un bouton à
    // part entière.
    recentFilesButton.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    recentFilesButton.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::white.withAlpha(0.12f));
    playerPane.addAndMakeVisible(recentFilesButton);

    for (auto* button : { &addHitPointButton, &muteButton, &zoomOutButton, &zoomPresetsButton, &zoomInButton })
        playerPane.addAndMakeVisible(button);

    playerPane.addAndMakeVisible(volumeSlider);

    setResizable(true, true);
    // NOTE : le bouton plein écran/zoom natif de l'hôte reste grisé quel que
    // soit le plafond ici -- confirmé sans effet. L'éditeur d'un plugin VST3
    // hébergé n'a pas sa propre fenêtre : c'est l'hôte (MuseScore) qui
    // possède et contrôle entièrement son chrome (barre de titre, bouton
    // plein écran), hors de portée de ce plugin.
    setResizeLimits(800, 320, 1920, 1200);
    setSize(1020, 520);
    startTimerHz(30);
}

MUVideoPlayerProAudioProcessorEditor::~MUVideoPlayerProAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MUVideoPlayerProAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void MUVideoPlayerProAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    if (sidebarCollapsed)
    {
        playerPane.setBounds(area);
    }
    else
    {
        juce::Component* items[] = { &playerPane, &sidebarResizer, &sidebarRoot };
        sidebarLayout.layOutComponents(items, 3, area.getX(), area.getY(),
                                        area.getWidth(), area.getHeight(), false, true);
    }

    layoutPlayerPane();
    layoutSidebar();
}

void MUVideoPlayerProAudioProcessorEditor::layoutPlayerPane()
{
    auto area = playerPane.getLocalBounds().reduced(10);

    // Le bouton bascule sidebar vit dans playerPane (pas l'éditeur) : son
    // bord droit borde toujours soit la sidebar, soit le bord de la fenêtre,
    // jamais le contenu de la sidebar elle-même -- ce qui la chevauchait
    // (notamment "Add hit point") quand il était positionné au coin de
    // l'éditeur indépendamment de la largeur réelle de playerPane.
    auto topRow = area.removeFromTop(22);
    constexpr int toggleSize = 20;
    sidebarToggleButton.setBounds(topRow.removeFromRight(toggleSize));
    topRow.removeFromRight(4);
    positionLabel.setBounds(topRow);

    // Taille commune à tous les boutons de la barre d'outils (icônes ET
    // texte -- "#"/"M" compris) : une taille inconsistante entre eux était
    // explicitement pointée comme un défaut visuel.
    constexpr int iconButtonSize = 22;
    constexpr int iconGap = 4;

    // Timeline tout en bas, rangée d'icônes (barre d'outils) juste au-dessus
    // (entre la vidéo et la timeline), vidéo occupant le reste.
    constexpr int timelineHeight = 70; // bandeau marqueurs (20) + piste graduée (~48) + interstice
    auto timelineArea = area.removeFromBottom(timelineHeight);
    timelineViewport.setBounds(timelineArea);

    const int viewportWidth = juce::jmax(1, timelineViewport.getWidth());
    const int contentWidth = juce::jmax(viewportWidth, juce::roundToInt(viewportWidth * timelineZoom));
    timelineComponent.setSize(contentWidth, timelineArea.getHeight());

    area.removeFromBottom(6);

    auto iconRow = area.removeFromBottom(iconButtonSize);
    area.removeFromBottom(6);

    // Fond distinct du reste (pas le même noir que la vidéo), pour que cette
    // rangée se lise comme une vraie barre d'outils -- même largeur que la
    // timeline juste en dessous (pas d'expansion horizontale : un fond plus
    // large que la timeline créait une incohérence visuelle entre les deux).
    iconRowBackground.setBounds(iconRow.expanded(0, 3));

    videoComponent.setBounds(area.withTrimmedBottom(4));

    // --- Groupe droit : zoom timeline, volume, mute (M tout à droite) -------
    muteButton.setBounds(iconRow.removeFromRight(iconButtonSize));
    iconRow.removeFromRight(8);
    volumeSlider.setBounds(iconRow.removeFromRight(70));
    iconRow.removeFromRight(6);
    volumeLabel.setBounds(iconRow.removeFromRight(50));
    iconRow.removeFromRight(10);
    zoomInButton.setBounds(iconRow.removeFromRight(22));
    iconRow.removeFromRight(2);
    zoomPresetsButton.setBounds(iconRow.removeFromRight(18));
    iconRow.removeFromRight(2);
    zoomField.setBounds(iconRow.removeFromRight(34));
    iconRow.removeFromRight(2);
    zoomOutButton.setBounds(iconRow.removeFromRight(22));

    // --- Groupe gauche : charger / fichiers récents -------------------------
    loadButton.setBounds(iconRow.removeFromLeft(iconButtonSize));
    iconRow.removeFromLeft(iconGap);
    constexpr int recentFilesButtonWidth = 14;
    recentFilesButton.setBounds(iconRow.removeFromLeft(recentFilesButtonWidth));

    // --- Groupe centre : transport, centré dans l'espace restant ------------
    const int transportWidth = iconButtonSize * 4 + iconGap * 3;
    auto transportArea = iconRow.withSizeKeepingCentre(transportWidth, iconRow.getHeight());

    addHitPointButton.setBounds(transportArea.removeFromLeft(iconButtonSize));
    transportArea.removeFromLeft(iconGap);
    rewindButton.setBounds(transportArea.removeFromLeft(iconButtonSize));
    transportArea.removeFromLeft(iconGap);
    playPauseButton.setBounds(transportArea.removeFromLeft(iconButtonSize));
    transportArea.removeFromLeft(iconGap);
    stopButton.setBounds(transportArea.removeFromLeft(iconButtonSize));
}

void MUVideoPlayerProAudioProcessorEditor::layoutSidebar()
{
    auto area = sidebarRoot.getLocalBounds();
    auto tabRow = area.removeFromTop(28);

    constexpr int tabWidth = 90;
    hitPointsTabButton.setBounds(tabRow.removeFromLeft(tabWidth));
    settingsTabButton.setBounds(tabRow.removeFromLeft(tabWidth));
    informationTabButton.setBounds(tabRow.removeFromLeft(tabWidth));

    juce::TextButton* activeButton = &hitPointsTabButton;
    if (selectedSidebarTab == 1)
        activeButton = &settingsTabButton;
    else if (selectedSidebarTab == 2)
        activeButton = &informationTabButton;

    const auto activeBounds = activeButton->getBounds();
    tabUnderline.setBounds(activeBounds.getX(), activeBounds.getBottom() - 2, activeBounds.getWidth(), 2);

    hitPointsSidebar.setBounds(area);
    videoSettingsPanel.setBounds(area);
    videoInformationPanel.setBounds(area);
}

void MUVideoPlayerProAudioProcessorEditor::selectSidebarTab(int index)
{
    selectedSidebarTab = index;
    hitPointsSidebar.setVisible(index == 0);
    videoSettingsPanel.setVisible(index == 1);
    videoInformationPanel.setVisible(index == 2);

    auto setTabTextColour = [](juce::TextButton& button, bool active)
    {
        button.setColour(juce::TextButton::textColourOffId,
                          active ? juce::Colours::white : juce::Colours::white.withAlpha(0.5f));
    };

    setTabTextColour(hitPointsTabButton, index == 0);
    setTabTextColour(settingsTabButton, index == 1);
    setTabTextColour(informationTabButton, index == 2);

    layoutSidebar();
}

void MUVideoPlayerProAudioProcessorEditor::timerCallback()
{
    const auto& cache = audioProcessor.getPositionCache();
    positionLabel.setText(formatPosition(cache.positionSeconds()), juce::dontSendNotification);

    const bool isPlayingNow = cache.isPlaying();

    if (isPlayingNow != lastKnownIsPlaying)
        updatePlayPauseIcon(isPlayingNow);

    // Avance/seek de la vidéo sur la position courante : volontairement piloté
    // depuis ce Timer (thread message, fiable en toutes circonstances) plutôt
    // que depuis renderOpenGL() lui-même, car ce dernier peut être suspendu
    // par macOS (CVDisplayLink throttled) tant que la fenêtre de l'éditeur
    // n'est pas active (ex : l'utilisateur clique dans la partition
    // MuseScore) — voir le commentaire de classe de VideoGLComponent.
    videoComponent.pumpFromQueue();
    timelineComponent.updatePlayhead();
}

void MUVideoPlayerProAudioProcessorEditor::updatePlayPauseIcon(bool isPlaying)
{
    playPauseButton.setImages(filledIcon(isPlaying ? pauseIconPath() : playIconPath()).get());
    lastKnownIsPlaying = isPlaying;
}

void MUVideoPlayerProAudioProcessorEditor::updateMuteButtonState(bool isMuted)
{
    muteButton.setColour(juce::TextButton::buttonColourId,
                          isMuted ? juce::Colour(MUVideoPlayerProLookAndFeel::accentColourArgb)
                                  : juce::Colour(MUVideoPlayerProLookAndFeel::panelColourArgb));
}

void MUVideoPlayerProAudioProcessorEditor::refreshHitPointViews()
{
    hitPointsSidebar.refresh();
    timelineComponent.repaint();
}

void MUVideoPlayerProAudioProcessorEditor::onVideoFileLoaded()
{
    videoComponent.notifySourceReloaded();

    // Recale aussi MuseScore sur le début : évite tout écart entre la
    // position de la partition et le début de la vidéo tout juste chargée
    // (sinon le premier seek de rattrapage peut rester visible un instant
    // selon l'ampleur de l'écart).
    audioProcessor.requestLocate(0.0);

    // Le débit de la nouvelle vidéo peut différer de l'ancienne : reformate
    // les timecodes déjà affichés dans la sidebar en conséquence, et active
    // "Add hit point" (désactivé tant qu'aucune vidéo n'est chargée).
    refreshHitPointViews();
    videoSettingsPanel.refresh();
    videoInformationPanel.refresh();

    for (auto* control : { (juce::Component*) &addHitPointButton, (juce::Component*) &rewindButton,
                            (juce::Component*) &zoomOutButton, (juce::Component*) &zoomField,
                            (juce::Component*) &zoomPresetsButton, (juce::Component*) &zoomInButton,
                            (juce::Component*) &playPauseButton, (juce::Component*) &stopButton,
                            (juce::Component*) &muteButton, (juce::Component*) &volumeSlider })
        control->setEnabled(true);
}

void MUVideoPlayerProAudioProcessorEditor::onVideoCleared()
{
    // notifySourceReloaded() ne suffit pas ici (ça ne fait que traiter la
    // situation comme "un seek arrive", pas "il n'y a plus rien à montrer") :
    // sans clearDisplay(), la dernière frame uploadée dans la texture GL
    // restait affichée indéfiniment (retour utilisateur).
    videoComponent.clearDisplay();
    updatePlayPauseIcon(false);

    refreshHitPointViews();
    videoSettingsPanel.refresh();
    videoInformationPanel.refresh();
    timelineComponent.repaint();

    for (auto* control : { (juce::Component*) &addHitPointButton, (juce::Component*) &rewindButton,
                            (juce::Component*) &zoomOutButton, (juce::Component*) &zoomField,
                            (juce::Component*) &zoomPresetsButton, (juce::Component*) &zoomInButton,
                            (juce::Component*) &playPauseButton, (juce::Component*) &stopButton,
                            (juce::Component*) &muteButton, (juce::Component*) &volumeSlider })
        control->setEnabled(false);
}

void MUVideoPlayerProAudioProcessorEditor::showRecentFilesMenu()
{
    const auto& recent = audioProcessor.getRecentFiles().paths();

    juce::PopupMenu menu;

    if (recent.empty())
    {
        menu.addItem(1, "(empty)", false);
    }
    else
    {
        int itemId = 1;
        for (const auto& path : recent)
            menu.addItem(itemId++, truncateForMenu(juce::File(path).getFileName(), 40));

        menu.addSeparator();
        menu.addItem(1000, "Clear list");
    }

    // setLookAndFeel() direct (plutôt que Options::withParentComponent(),
    // essayé d'abord) : withParentComponent() imbrique la fenêtre du menu
    // dans celle de l'éditeur au lieu d'une vraie fenêtre native indépendante,
    // ce qui la faisait passer SOUS la surface OpenGL de VideoGLComponent
    // (retour utilisateur) -- son seul autre effet ici était de permettre au
    // menu de résoudre le bon LookAndFeel, que ceci fait tout aussi bien sans
    // ce défaut de z-order. withTargetComponent() ancre le menu sur le
    // bouton plutôt que sur la position du curseur, pour un ouverture plus
    // prévisible (et le bon facteur d'échelle en hi-DPI).
    menu.setLookAndFeel(&lookAndFeel);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(recentFilesButton), [this, recent](int result)
    {
        if (result == 0)
            return;

        if (result == 1000)
        {
            audioProcessor.getRecentFiles().clear();
            return;
        }

        const int index = result - 1;
        if (index < 0 || index >= (int) recent.size())
            return;

        const juce::File file(recent[(size_t) index]);
        if (file.existsAsFile() && audioProcessor.loadVideoFile(file))
            onVideoFileLoaded();
    });
}

void MUVideoPlayerProAudioProcessorEditor::showZoomPresetsMenu()
{
    juce::PopupMenu menu;
    for (int percent : { 100, 250, 500, 750, 1000 })
        menu.addItem(percent, juce::String(percent) + "%");

    menu.setLookAndFeel(&lookAndFeel);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(zoomPresetsButton), [this](int result)
    {
        if (result > 0)
            setTimelineZoom(result / 100.0);
    });
}

void MUVideoPlayerProAudioProcessorEditor::setTimelineZoom(double newZoom)
{
    timelineZoom = juce::jlimit(1.0, timelineZoomMax, newZoom);
    zoomField.setText(juce::String(juce::roundToInt(timelineZoom * 100.0)), juce::dontSendNotification);
    layoutPlayerPane();
}
