#include "TimelineComponent.h"

namespace
{
// Couleurs fixes (pas de thème clair/sombre à gérer ici, cf. VideoGLComponent) :
// mêmes teintes que le panel vidéo MuseScore porté (VideoPanel.qml), qui les
// fige aussi en dur pour la même raison (bandeau/piste toujours sombres,
// quel que soit le thème hôte). L'accent reprend HitPoint::colour par défaut
// (0x3B94E5) plutôt qu'une valeur arbitraire différente.
const juce::Colour rulerColour { 0xFF2A2A2A };
const juce::Colour trackColour { 0xFF181818 };
const juce::Colour accentColour { 0xFF3B94E5 };
const juce::Colour tickColour { 0xFFF0F0F0 };
// Ambre plutôt que blanc : le blanc se confondait avec le texte des
// étiquettes de temps/marqueurs et paraissait trop épais/imposant pour une
// simple tête de lecture (retour utilisateur).
const juce::Colour playheadColour { 0xFFE8B84B };

juce::String secondsToLabel(double seconds)
{
    const int wholeSeconds = (int) std::floor(seconds);
    if (wholeSeconds < 60)
        return juce::String(wholeSeconds) + "s";

    const int minutes = wholeSeconds / 60;
    const int remaining = wholeSeconds % 60;
    return juce::String(minutes) + ":" + (remaining < 10 ? "0" : "") + juce::String(remaining);
}

// Hauteur/largeur des graduations : mêmes seuils que
// VideoPanel.qml::timelineTick{Height,Width}() (multiples de la seconde
// nominale : 60/30/15/10/5/1s).
int tickHeight(int frameIndex, int oneSecond)
{
    if (frameIndex % (oneSecond * 60) == 0) return 14;
    if (frameIndex % (oneSecond * 30) == 0) return 13;
    if (frameIndex % (oneSecond * 15) == 0) return 12;
    if (frameIndex % (oneSecond * 10) == 0) return 10;
    if (frameIndex % (oneSecond * 5) == 0) return 8;
    if (frameIndex % oneSecond == 0) return 6;
    return 2;
}

float tickWidth(int frameIndex, int oneSecond)
{
    if (frameIndex % (oneSecond * 30) == 0) return 2.0f;
    if (frameIndex % oneSecond == 0) return 1.5f;
    return 1.0f;
}
}

TimelineComponent::TimelineComponent()
{
    setInterceptsMouseClicks(true, true);
}

TimelineComponent::~TimelineComponent() = default;

void TimelineComponent::setSource(const VideoDecoder* decoder, const PositionCache* newPositionCache,
                                   HitPointList* newHitPoints)
{
    videoDecoder = decoder;
    positionCache = newPositionCache;
    hitPoints = newHitPoints;
    repaint();
}

void TimelineComponent::updatePlayhead()
{
    if (!scrubbing && positionCache != nullptr)
        displayedPositionSeconds = positionCache->positionSeconds();

    repaint();
}

double TimelineComponent::durationSeconds() const noexcept
{
    return videoDecoder != nullptr ? videoDecoder->getDurationSeconds() : 0.0;
}

juce::Rectangle<int> TimelineComponent::rulerBounds() const
{
    return getLocalBounds().withHeight(rulerHeight);
}

juce::Rectangle<int> TimelineComponent::trackBounds() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight + rulerTrackGap);
}

double TimelineComponent::xToSeconds(float x, const juce::Rectangle<int>& bounds) const noexcept
{
    const double duration = durationSeconds();
    if (duration <= 0.0 || bounds.getWidth() <= 0)
        return 0.0;

    const float clampedX = juce::jlimit(0.0f, (float) bounds.getWidth(), x - (float) bounds.getX());
    return juce::jlimit(0.0, duration, (double) (clampedX / (float) bounds.getWidth()) * duration);
}

float TimelineComponent::secondsToX(double seconds, const juce::Rectangle<int>& bounds) const noexcept
{
    const double duration = durationSeconds();
    if (duration <= 0.0 || bounds.getWidth() <= 0)
        return (float) bounds.getX();

    return (float) bounds.getX() + (float) ((seconds / duration) * bounds.getWidth());
}

std::vector<TimelineComponent::MarkerLayout> TimelineComponent::layoutMarkers(const juce::Rectangle<int>& bounds) const
{
    std::vector<MarkerLayout> markers;
    if (hitPoints == nullptr || durationSeconds() <= 0.0)
        return markers;

    juce::Font font(juce::FontOptions(8.0f).withStyle("Bold"));

    for (const auto& hitPoint : hitPoints->items())
    {
        const double displaySeconds = (hitPoint.id == draggingHitPointId)
                                     ? draggingHitPointSeconds
                                     : hitPoint.timeMs / 1000.0;

        constexpr float lineWidth = 2.0f;
        const float rawX = secondsToX(displaySeconds, bounds);
        const float lineX = juce::jlimit((float) bounds.getX(), (float) bounds.getRight() - lineWidth, rawX);

        const float labelWidth = juce::jmax(20.0f, juce::GlyphArrangement::getStringWidth(font, hitPoint.label) + 8.0f);
        const float labelX = juce::jlimit((float) bounds.getX(), (float) bounds.getRight() - labelWidth, lineX);
        const float labelY = (float) bounds.getY() + ((float) bounds.getHeight() - 16.0f) * 0.5f;

        MarkerLayout layout;
        layout.id = hitPoint.id;
        layout.lineX = lineX;
        layout.labelBounds = { labelX, labelY, labelWidth, 16.0f };
        layout.label = hitPoint.label;
        markers.push_back(layout);
    }

    return markers;
}

int TimelineComponent::markerAt(juce::Point<float> position) const
{
    for (const auto& marker : layoutMarkers(rulerBounds()))
    {
        const auto hitArea = marker.labelBounds.getUnion({ marker.lineX, marker.labelBounds.getY(), 2.0f, marker.labelBounds.getHeight() })
                                  .expanded(4.0f);
        if (hitArea.contains(position))
            return marker.id;
    }

    return -1;
}

void TimelineComponent::paint(juce::Graphics& g)
{
    const auto ruler = rulerBounds();
    const auto track = trackBounds();

    g.setColour(rulerColour);
    g.fillRect(ruler);

    g.setColour(trackColour);
    g.fillRect(track);

    const double duration = durationSeconds();
    if (duration <= 0.0)
        return;

    // Piste : graduations + étiquettes de temps.
    const double frameRate = videoDecoder != nullptr ? videoDecoder->getFrameRate() : 24.0;
    const int oneSecond = juce::jmax(1, (int) std::lround(frameRate));
    const int frameCount = (int) std::floor(duration * frameRate) + 1;
    const int stride = juce::jmax(1, frameCount / juce::jmax(1, track.getWidth()));

    for (int frame = 0; frame < frameCount; frame += stride)
    {
        const float x = (float) track.getX() + ((float) frame / (float) juce::jmax(1, frameCount - 1)) * (float) track.getWidth();
        const float height = (float) tickHeight(frame, oneSecond);
        const float width = tickWidth(frame, oneSecond);
        const float alpha = (frame % oneSecond == 0) ? 0.62f : 0.24f;

        g.setColour(tickColour.withAlpha(alpha));
        g.fillRect(juce::jlimit((float) track.getX(), (float) track.getRight() - width, x - width * 0.5f),
                   (float) track.getY() + 4.0f, width, height);
    }

    juce::Font labelFont(juce::FontOptions(10.0f));
    g.setFont(labelFont);

    const int durationSecondsWhole = (int) std::floor(duration);
    const int lastGridSecond = durationSecondsWhole - (durationSecondsWhole % 5);
    std::vector<int> gridSeconds;
    for (int s = 0; s <= lastGridSecond; s += 5)
        gridSeconds.push_back(s);
    if (durationSecondsWhole != lastGridSecond)
        gridSeconds.push_back(durationSecondsWhole);

    float lastLabelRight = -1.0e6f;
    constexpr float minLabelGap = 6.0f;
    // Juste sous les graduations les plus hautes (tickTop 4 + hauteur max 14),
    // pas collé au bas de la piste : à cet endroit, un zoom horizontal
    // (timelineViewport) peut afficher une barre de défilement qui recouvre
    // le bas de la piste et rendait ce texte invisible.
    const float labelY = (float) track.getY() + 20.0f;

    for (int second : gridSeconds)
    {
        const juce::String label = secondsToLabel((double) second);
        const float labelWidth = juce::GlyphArrangement::getStringWidth(labelFont, label);
        const float labelX = (float) track.getX() + ((float) second / (float) juce::jmax(0.001, duration)) * (float) track.getWidth();

        const bool atLeftEdge = labelX < (float) track.getX() + 16.0f;
        const bool atRightEdge = labelX > (float) track.getRight() - 16.0f;
        const float leftEdge = atLeftEdge ? labelX : (atRightEdge ? labelX - labelWidth : labelX - labelWidth * 0.5f);
        const float rightEdge = leftEdge + labelWidth;

        if (leftEdge < lastLabelRight + minLabelGap)
            continue;

        g.setColour(tickColour.withAlpha(0.74f));
        g.drawText(label, (int) leftEdge, (int) labelY, (int) labelWidth + 2, 12,
                   juce::Justification::centredLeft, false);
        lastLabelRight = rightEdge;
    }

    // Repères verticaux fins sous chaque hit point (référence uniquement --
    // l'interaction se fait dans le bandeau ci-dessus).
    if (hitPoints != nullptr)
    {
        g.setColour(accentColour.withAlpha(0.8f));
        for (const auto& hitPoint : hitPoints->items())
        {
            const double displaySeconds = (hitPoint.id == draggingHitPointId)
                                         ? draggingHitPointSeconds
                                         : hitPoint.timeMs / 1000.0;
            const float x = secondsToX(displaySeconds, track);
            g.fillRect(juce::jlimit((float) track.getX(), (float) track.getRight() - 1.0f, x - 0.5f),
                       (float) track.getY() + 2.0f, 1.0f, (float) track.getHeight() - 2.0f);
        }
    }

    // Tête de lecture.
    g.setColour(playheadColour);
    const float playheadX = secondsToX(displayedPositionSeconds, track);
    g.fillRect(juce::jlimit((float) track.getX(), (float) track.getRight() - 1.0f, playheadX - 0.5f),
               (float) track.getY(), 1.0f, (float) track.getHeight());

    // Bandeau : marqueurs de hit points.
    for (const auto& marker : layoutMarkers(ruler))
    {
        if (marker.id == renamingHitPointId)
            continue; // le TextEditor overlay couvre déjà cette étiquette.

        g.setColour(accentColour);
        g.fillRect(marker.lineX, (float) ruler.getY(), 2.0f, (float) ruler.getHeight());

        const bool dragging = marker.id == draggingHitPointId;
        g.setColour(accentColour);
        g.fillRect(marker.labelBounds);
        if (dragging)
        {
            g.setColour(juce::Colours::white);
            g.drawRect(marker.labelBounds, 2.0f);
        }

        g.setColour(juce::Colours::white);
        juce::Font markerFont(juce::FontOptions(8.0f).withStyle("Bold"));
        g.setFont(markerFont);
        g.drawText(marker.label, marker.labelBounds.toNearestInt(), juce::Justification::centred, false);
    }
}

void TimelineComponent::resized()
{
    if (renameEditor != nullptr && renamingHitPointId != -1)
    {
        for (const auto& marker : layoutMarkers(rulerBounds()))
        {
            if (marker.id == renamingHitPointId)
            {
                renameEditor->setBounds(marker.labelBounds.toNearestInt());
                break;
            }
        }
    }
}

void TimelineComponent::mouseDown(const juce::MouseEvent& event)
{
    if (renameEditor != nullptr)
        endRenameEditor(true);

    if (hitPoints == nullptr || durationSeconds() <= 0.0)
        return;

    const auto position = event.position;

    if (rulerBounds().toFloat().contains(position))
    {
        const int id = markerAt(position);

        if (id != -1 && event.mods.isPopupMenu())
        {
            showContextMenu(id);
            return;
        }

        if (id != -1)
        {
            // Ne démarre pas encore le glissement ici (comme VideoPanel.qml) :
            // un simple clic sans mouvement ne doit pas re-snapper le marqueur
            // sur sa propre position. mouseDrag() s'en charge au premier
            // mouvement réel, en promouvant pressedHitPointId en
            // draggingHitPointId.
            pressedHitPointId = id;
        }
        else if (!event.mods.isPopupMenu())
        {
            // Un clic dans le bandeau hors de tout marqueur déplace aussi la
            // tête de lecture, comme un clic dans la piste juste en dessous --
            // l'utilisateur n'a pas à viser précisément la piste graduée pour
            // naviguer.
            seekTo(xToSeconds(position.x, rulerBounds()));
        }

        return;
    }

    if (trackBounds().toFloat().contains(position) && !event.mods.isPopupMenu())
    {
        scrubbing = true;
        lastScrubSeekMs = juce::Time::currentTimeMillis();
        seekTo(xToSeconds(position.x, trackBounds()));
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto position = event.position;

    if (draggingHitPointId == -1 && pressedHitPointId != -1)
        draggingHitPointId = pressedHitPointId; // premier mouvement réel : promotion en glissement.

    if (draggingHitPointId != -1)
    {
        draggingHitPointSeconds = xToSeconds(position.x, rulerBounds());
        repaint();
        return;
    }

    if (scrubbing)
    {
        const auto now = juce::Time::currentTimeMillis();
        if (now - lastScrubSeekMs < scrubSeekIntervalMs)
            return;

        lastScrubSeekMs = now;
        seekTo(xToSeconds(position.x, trackBounds()));
    }
}

void TimelineComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    pressedHitPointId = -1;

    if (draggingHitPointId != -1 && hitPoints != nullptr)
    {
        const int id = draggingHitPointId;
        const double seconds = draggingHitPointSeconds;
        draggingHitPointId = -1;

        // Seek avant de persister la nouvelle position : même ordre que
        // VideoPanel.qml (voir son commentaire sur onReleased), gardé par
        // cohérence même si le risque de double-seek qui motivait cet ordre
        // là-bas (resynchro réactive sur settingsChanged) n'a pas
        // d'équivalent dans cette architecture.
        seekTo(seconds);
        hitPoints->setTimeMs(id, (int) std::lround(seconds * 1000.0));
        notifyHitPointsChanged();
        repaint();
        return;
    }

    if (scrubbing)
    {
        scrubbing = false;
        seekTo(xToSeconds(event.position.x, trackBounds()));
    }
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (hitPoints == nullptr || durationSeconds() <= 0.0)
        return;

    const auto position = event.position;
    if (!rulerBounds().toFloat().contains(position))
        return;

    const int id = markerAt(position);
    if (id != -1)
    {
        beginRenameEditor(id);
        return;
    }

    hitPoints->add((int) std::lround(xToSeconds(position.x, rulerBounds()) * 1000.0));
    notifyHitPointsChanged();
    repaint();
}

void TimelineComponent::beginRenameEditor(int hitPointId)
{
    if (hitPoints == nullptr)
        return;

    juce::Rectangle<int> labelBounds;
    juce::String currentLabel;
    bool found = false;

    for (const auto& marker : layoutMarkers(rulerBounds()))
    {
        if (marker.id == hitPointId)
        {
            labelBounds = marker.labelBounds.toNearestInt();
            currentLabel = marker.label;
            found = true;
            break;
        }
    }

    if (!found)
        return;

    renamingHitPointId = hitPointId;
    renameEditor = std::make_unique<juce::TextEditor>();
    renameEditor->setFont(juce::FontOptions(11.0f));
    renameEditor->setJustification(juce::Justification::centred);
    renameEditor->setSelectAllWhenFocused(true);
    renameEditor->setText(currentLabel, juce::dontSendNotification);
    renameEditor->onReturnKey = [this] { endRenameEditor(true); };
    renameEditor->onEscapeKey = [this] { endRenameEditor(false); };
    renameEditor->onFocusLost = [this] { endRenameEditor(true); };

    addAndMakeVisible(*renameEditor);
    renameEditor->setBounds(labelBounds);
    renameEditor->grabKeyboardFocus();
    repaint();
}

void TimelineComponent::endRenameEditor(bool commit)
{
    if (renameEditor == nullptr)
        return;

    const int id = renamingHitPointId;
    const juce::String newLabel = renameEditor->getText();

    removeChildComponent(renameEditor.get());
    renameEditor.reset();
    renamingHitPointId = -1;

    if (commit && hitPoints != nullptr && id != -1)
    {
        hitPoints->rename(id, newLabel);
        notifyHitPointsChanged();
    }

    repaint();
}

void TimelineComponent::showContextMenu(int hitPointId)
{
    if (hitPoints == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Rename");
    menu.addItem(2, "Delete");

    // setLookAndFeel(&getLookAndFeel()) plutôt que Options::withParentComponent() :
    // ce composant est déjà correctement rattaché à la hiérarchie de
    // l'éditeur, donc getLookAndFeel() (qui remonte les parentComponent)
    // résout déjà le bon thème -- withParentComponent() imbriquait en plus la
    // fenêtre du menu dans celle de l'éditeur au lieu d'une vraie fenêtre
    // native, ce qui la faisait passer sous la surface OpenGL de
    // VideoGLComponent (retour utilisateur).
    menu.setLookAndFeel(&getLookAndFeel());
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, hitPointId](int result)
    {
        if (result == 1)
        {
            beginRenameEditor(hitPointId);
        }
        else if (result == 2 && hitPoints != nullptr)
        {
            hitPoints->remove(hitPointId);
            notifyHitPointsChanged();
            repaint();
        }
    });
}

void TimelineComponent::seekTo(double seconds)
{
    displayedPositionSeconds = juce::jlimit(0.0, juce::jmax(0.0, durationSeconds()), seconds);
    repaint();

    if (onSeek)
        onSeek(displayedPositionSeconds);
}

void TimelineComponent::notifyHitPointsChanged()
{
    if (onHitPointsChanged)
        onHitPointsChanged();
}
