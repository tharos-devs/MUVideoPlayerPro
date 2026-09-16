#include "HitPointsSidebar.h"

#include "../Transport/VideoTimecode.h"

namespace
{
const juce::Colour backgroundColour { 0xFF1E1E1E };
const juce::Colour accentColour { 0xFF3B94E5 };
const juce::Colour headerColour { 0xFFB0B0B0 };
const juce::Colour deleteTextColour { 0xFFE07A73 };

// Label dont un simple clic (pas un double-clic, qui reste géré par
// juce::Label lui-même pour entrer en édition) déclenche un callback --
// utilisé pour le timecode d'une ligne (clic = seek, double-clic = édition).
class SeekableLabel : public juce::Label
{
public:
    std::function<void()> onSingleClick;

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::Label::mouseUp(event);
        if (event.mouseWasClicked() && !isBeingEdited() && onSingleClick)
            onSingleClick();
    }
};
}

class HitPointRowComponent : public juce::Component
{
public:
    HitPointRowComponent()
    {
        timecodeLabel.setJustificationType(juce::Justification::centredRight);
        timecodeLabel.setColour(juce::Label::textColourId, accentColour);
        timecodeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        timecodeLabel.setEditable(false, true, false);
        addAndMakeVisible(timecodeLabel);

        nameLabel.setJustificationType(juce::Justification::centredLeft);
        nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        nameLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        nameLabel.setEditable(false, true, false);
        addAndMakeVisible(nameLabel);

        // Fond/texte par défaut hérités du LookAndFeel global (dark theme) --
        // seule la teinte du texte diffère (rouge atténué : affordance
        // "suppression"), le reste reste cohérent avec les autres boutons.
        deleteButton.setColour(juce::TextButton::textColourOffId, deleteTextColour);
        deleteButton.setColour(juce::TextButton::textColourOnId, deleteTextColour);
        addAndMakeVisible(deleteButton);
    }

    void setHitPoint(int id, const juce::String& label, int timeMs, double frameRate)
    {
        hitPointId = id;
        timecodeLabel.setText(VideoTimecode::format(timeMs / 1000.0, frameRate), juce::dontSendNotification);
        nameLabel.setText(label, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        deleteButton.setBounds(area.removeFromRight(22));
        area.removeFromRight(4);
        timecodeLabel.setBounds(area.removeFromLeft(84));
        area.removeFromLeft(8);
        nameLabel.setBounds(area);
    }

    int hitPointId = 0;
    SeekableLabel timecodeLabel;
    juce::Label nameLabel;
    juce::TextButton deleteButton { juce::String::fromUTF8("\xC3\x97") }; // "×"
};

HitPointsSidebar::HitPointsSidebar()
{
    // Style par défaut du LookAndFeel global (dark theme) : pas de teinte
    // spécifique nécessaire ici, contrairement à deleteButton.
    addButton.onClick = [this]
    {
        if (hitPoints == nullptr || getCurrentPositionSeconds == nullptr)
            return;

        const double seconds = getCurrentPositionSeconds();
        hitPoints->add((int) std::lround(seconds * 1000.0));
        notifyHitPointsChanged();
        refresh();
    };
    addAndMakeVisible(addButton);

    rowsViewport.setViewedComponent(&rowsContainer, false);
    rowsViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(rowsViewport);
}

HitPointsSidebar::~HitPointsSidebar() = default;

void HitPointsSidebar::setSource(const VideoDecoder* decoder, HitPointList* newHitPoints)
{
    videoDecoder = decoder;
    hitPoints = newHitPoints;
    refresh();
}

double HitPointsSidebar::frameRate() const noexcept
{
    return videoDecoder != nullptr ? videoDecoder->getFrameRate() : 24.0;
}

void HitPointsSidebar::refresh()
{
    rebuildRows();
    layoutRows();
    addButton.setEnabled(hitPoints != nullptr && videoDecoder != nullptr && videoDecoder->getDurationSeconds() > 0.0);
    repaint();
}

void HitPointsSidebar::rebuildRows()
{
    rows.clear();

    if (hitPoints == nullptr)
        return;

    const double rate = frameRate();

    for (const auto& hitPoint : hitPoints->items())
    {
        auto* row = rows.add(std::make_unique<HitPointRowComponent>());
        row->setHitPoint(hitPoint.id, hitPoint.label, hitPoint.timeMs, rate);
        rowsContainer.addAndMakeVisible(row);

        const int id = hitPoint.id;

        row->timecodeLabel.onSingleClick = [this, id]
        {
            if (hitPoints == nullptr || onSeek == nullptr)
                return;

            for (const auto& current : hitPoints->items())
            {
                if (current.id == id)
                {
                    onSeek(current.timeMs / 1000.0);
                    return;
                }
            }
        };

        // Différé : ces callbacks sont invoqués depuis l'intérieur du
        // traitement interne du Label/TextButton de LA LIGNE elle-même
        // (mouseUp/perte de focus) -- reconstruire rows (et donc détruire
        // cette ligne) de façon synchrone ici la détruirait alors que JUCE a
        // encore du code à exécuter dessus plus haut dans la pile d'appel.
        row->timecodeLabel.onTextChange = [this, id, row]
        {
            const juce::String text = row->timecodeLabel.getText();
            juce::Component::SafePointer<HitPointsSidebar> safeThis(this);

            juce::MessageManager::callAsync([safeThis, id, text]
            {
                // safeThis se remet à null si la sidebar (donc l'éditeur) a
                // été détruite entre le clic et ce callback différé.
                if (safeThis == nullptr || safeThis->hitPoints == nullptr)
                    return;

                const int parsedMs = VideoTimecode::parseToMs(text, safeThis->frameRate());
                if (parsedMs >= 0)
                    safeThis->hitPoints->setTimeMs(id, parsedMs);

                safeThis->notifyHitPointsChanged();
                safeThis->refresh();
            });
        };

        row->nameLabel.onTextChange = [this, id, row]
        {
            const juce::String text = row->nameLabel.getText();
            juce::Component::SafePointer<HitPointsSidebar> safeThis(this);

            juce::MessageManager::callAsync([safeThis, id, text]
            {
                if (safeThis == nullptr || safeThis->hitPoints == nullptr)
                    return;

                safeThis->hitPoints->rename(id, text);
                safeThis->notifyHitPointsChanged();
                safeThis->refresh();
            });
        };

        row->deleteButton.onClick = [this, id]
        {
            juce::Component::SafePointer<HitPointsSidebar> safeThis(this);

            juce::MessageManager::callAsync([safeThis, id]
            {
                if (safeThis == nullptr || safeThis->hitPoints == nullptr)
                    return;

                safeThis->hitPoints->remove(id);
                safeThis->notifyHitPointsChanged();
                safeThis->refresh();
            });
        };
    }
}

void HitPointsSidebar::layoutRows()
{
    const int width = juce::jmax(0, rowsViewport.getWidth() - rowsViewport.getScrollBarThickness());
    int y = 0;

    for (auto* row : rows)
    {
        row->setBounds(0, y, width, rowHeight);
        y += rowHeight + rowSpacing;
    }

    rowsContainer.setSize(width, juce::jmax(0, y - rowSpacing));
}

void HitPointsSidebar::notifyHitPointsChanged()
{
    if (onHitPointsChanged)
        onHitPointsChanged();
}

void HitPointsSidebar::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    // Rien à annoncer par un en-tête de colonnes tant que la liste est vide.
    if (hitPoints == nullptr || hitPoints->items().empty())
        return;

    auto header = getLocalBounds().withTrimmedTop(32).withHeight(headerHeight).reduced(4, 0);

    g.setColour(headerColour);
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText("Timecode", header.removeFromLeft(84), juce::Justification::centredRight, false);
    header.removeFromLeft(8);
    g.drawText("Name", header, juce::Justification::centredLeft, false);
}

void HitPointsSidebar::resized()
{
    auto area = getLocalBounds();
    addButton.setBounds(area.removeFromTop(28).reduced(4, 2));
    area.removeFromTop(headerHeight);
    rowsViewport.setBounds(area.reduced(2, 0));
    layoutRows();
}
