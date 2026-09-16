#include "VideoInformationPanel.h"

#include <cmath>

namespace
{
const juce::Colour backgroundColour { 0xFF1E1E1E };
const juce::Colour labelColour { 0xFFB0B0B0 };
const juce::Colour valueColour { juce::Colours::white };
}

VideoInformationPanel::VideoInformationPanel()
    : rows { {
          { "File", &fileNameValue },
          { "Resolution", &resolutionValue },
          { "Duration", &durationValue },
          { "Frame rate", &frameRateValue },
          { "Video codec", &videoCodecValue },
          { "Audio codec", &audioCodecValue },
      } }
{
    for (auto& row : rows)
    {
        row.second->setJustificationType(juce::Justification::centredLeft);
        row.second->setColour(juce::Label::textColourId, valueColour);
        row.second->setFont(juce::Font(juce::FontOptions(12.0f)));
        row.second->setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(row.second);
    }
}

void VideoInformationPanel::setSource(const VideoDecoder* decoder)
{
    videoDecoder = decoder;
    refresh();
}

void VideoInformationPanel::refresh()
{
    const bool hasVideo = videoDecoder != nullptr && videoDecoder->getDurationSeconds() > 0.0;

    if (!hasVideo)
    {
        for (auto& row : rows)
            row.second->setText({}, juce::dontSendNotification);
        repaint();
        return;
    }

    const juce::File file = getCurrentFile != nullptr ? getCurrentFile() : juce::File();
    fileNameValue.setText(file.getFileName(), juce::dontSendNotification);
    fileNameValue.setTooltip(file.getFullPathName());

    resolutionValue.setText(juce::String(videoDecoder->getWidth()) + " x " + juce::String(videoDecoder->getHeight()),
                             juce::dontSendNotification);

    const int totalSeconds = (int) std::floor(videoDecoder->getDurationSeconds());
    durationValue.setText(juce::String::formatted("%02d:%02d:%02d",
                                                    totalSeconds / 3600, (totalSeconds / 60) % 60, totalSeconds % 60),
                           juce::dontSendNotification);

    frameRateValue.setText(juce::String(videoDecoder->getFrameRate(), 2) + " fps", juce::dontSendNotification);

    videoCodecValue.setText(videoDecoder->getVideoCodecName(), juce::dontSendNotification);
    audioCodecValue.setText(videoDecoder->getAudioCodecName().isNotEmpty() ? videoDecoder->getAudioCodecName()
                                                                            : "--",
                             juce::dontSendNotification);

    repaint();
}

void VideoInformationPanel::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    if (videoDecoder == nullptr || videoDecoder->getDurationSeconds() <= 0.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("No video loaded", getLocalBounds().reduced(8), juce::Justification::centred, true);
        return;
    }

    g.setColour(labelColour);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));

    int y = 8;
    for (const auto& row : rows)
    {
        g.drawText(row.first, 4, y, labelColumnWidth, rowHeight, juce::Justification::centredLeft, false);
        y += rowHeight;
    }
}

void VideoInformationPanel::resized()
{
    int y = 8;
    for (auto& row : rows)
    {
        row.second->setBounds(labelColumnWidth + 8, y, getWidth() - labelColumnWidth - 16, rowHeight);
        y += rowHeight;
    }
}
