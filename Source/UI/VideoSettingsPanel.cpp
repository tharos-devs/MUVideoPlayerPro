#include "VideoSettingsPanel.h"

namespace
{
const juce::Colour fieldBackgroundColour { 0xFF1E1E1E };
const juce::Colour fieldOutlineColour { 0xFF3B94E5 };

// Taille de police explicite plutôt que la taille par défaut de juce::Label
// (~15px) : dans une colonne aussi étroite que "ms" (22px), le texte ne
// tenait pas et JUCE l'affichait tronqué en "…" -- même famille de bug que
// les boutons de zoom (cf. MUVideoPlayerProLookAndFeel::getTextButtonFont()).
constexpr float fieldFontSize = 13.0f;

void styleEditableField(juce::Label& field)
{
    field.setJustificationType(juce::Justification::centred);
    field.setFont(juce::Font(juce::FontOptions(fieldFontSize)));
    field.setColour(juce::Label::backgroundColourId, fieldBackgroundColour);
    field.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    field.setColour(juce::Label::textColourId, juce::Colours::white);
    field.setColour(juce::Label::backgroundWhenEditingColourId, fieldBackgroundColour);
    field.setColour(juce::Label::outlineWhenEditingColourId, fieldOutlineColour);
    field.setColour(juce::Label::textWhenEditingColourId, juce::Colours::white);
    field.setEditable(true, true, false);
}

void styleCaption(juce::Label& caption, const juce::String& text)
{
    caption.setText(text, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centredLeft);
    caption.setFont(juce::Font(juce::FontOptions(fieldFontSize)));
    caption.setColour(juce::Label::textColourId, juce::Colours::white);
}
}

VideoSettingsPanel::VideoSettingsPanel()
{
    styleCaption(fpsCaption, "fps");
    addAndMakeVisible(fpsCaption);

    styleEditableField(fpsField);
    fpsField.onTextChange = [this]
    {
        const double parsed = fpsField.getText().getDoubleValue();
        if (videoDecoder != nullptr && parsed > 0.0)
            videoDecoder->setFrameRate(parsed);
        refresh();
    };
    addAndMakeVisible(fpsField);

    detectButton.onClick = [this]
    {
        if (videoDecoder != nullptr)
            videoDecoder->setFrameRate(videoDecoder->getDetectedFrameRate());
        refresh();
    };
    addAndMakeVisible(detectButton);

    styleCaption(offsetCaption, "Offset");
    addAndMakeVisible(offsetCaption);

    styleEditableField(offsetField);
    offsetField.onTextChange = [this] { applyOffsetMs(offsetField.getText().getIntValue()); };
    addAndMakeVisible(offsetField);

    styleCaption(msCaption, "ms");
    addAndMakeVisible(msCaption);

    offsetMinusButton.onClick = [this]
    {
        const int currentMs = getOffsetSeconds != nullptr ? juce::roundToInt(getOffsetSeconds() * 1000.0) : 0;
        applyOffsetMs(currentMs - 100);
    };
    addAndMakeVisible(offsetMinusButton);

    offsetPlusButton.onClick = [this]
    {
        const int currentMs = getOffsetSeconds != nullptr ? juce::roundToInt(getOffsetSeconds() * 1000.0) : 0;
        applyOffsetMs(currentMs + 100);
    };
    addAndMakeVisible(offsetPlusButton);

    clearVideoButton.onClick = [this]
    {
        if (onClearVideo)
            onClearVideo();
    };
    addAndMakeVisible(clearVideoButton);

    refresh();
}

void VideoSettingsPanel::setSource(VideoDecoder* decoder)
{
    videoDecoder = decoder;
    refresh();
}

void VideoSettingsPanel::applyOffsetMs(int newOffsetMs)
{
    if (onOffsetChanged)
        onOffsetChanged(newOffsetMs / 1000.0);

    refresh();
}

void VideoSettingsPanel::refresh()
{
    const bool hasVideo = videoDecoder != nullptr && videoDecoder->getDurationSeconds() > 0.0;

    for (auto* control : { (juce::Component*) &fpsField, (juce::Component*) &detectButton,
                            (juce::Component*) &offsetField, (juce::Component*) &offsetMinusButton,
                            (juce::Component*) &offsetPlusButton, (juce::Component*) &clearVideoButton })
        control->setEnabled(hasVideo);

    if (videoDecoder != nullptr)
        fpsField.setText(juce::String(videoDecoder->getFrameRate(), 3), juce::dontSendNotification);

    if (getOffsetSeconds != nullptr)
        offsetField.setText(juce::String(juce::roundToInt(getOffsetSeconds() * 1000.0)), juce::dontSendNotification);
}

void VideoSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(8);

    constexpr int rowHeight = 26;
    constexpr int rowGap = 10;
    constexpr int captionWidth = 50;
    constexpr int fieldWidth = 70;

    auto fpsRow = area.removeFromTop(rowHeight);
    fpsCaption.setBounds(fpsRow.removeFromLeft(captionWidth));
    fpsField.setBounds(fpsRow.removeFromLeft(fieldWidth));
    fpsRow.removeFromLeft(6);
    detectButton.setBounds(fpsRow);

    area.removeFromTop(rowGap);

    auto offsetRow = area.removeFromTop(rowHeight);
    offsetCaption.setBounds(offsetRow.removeFromLeft(captionWidth));
    offsetField.setBounds(offsetRow.removeFromLeft(fieldWidth));
    offsetRow.removeFromLeft(4);
    msCaption.setBounds(offsetRow.removeFromLeft(26));
    offsetRow.removeFromLeft(6);

    const int nudgeWidth = (offsetRow.getWidth() - 6) / 2;
    offsetMinusButton.setBounds(offsetRow.removeFromLeft(nudgeWidth));
    offsetRow.removeFromLeft(6);
    offsetPlusButton.setBounds(offsetRow);

    area.removeFromTop(rowGap);
    clearVideoButton.setBounds(area.removeFromTop(rowHeight));
}
