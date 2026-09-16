#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include <juce_core/juce_core.h>

// Formatage timecode SMPTE non-drop-frame HH:MM:SS:FF, port fidèle de
// mu::project::formatVideoTimecode (~/MuseScore, branche feature/video-sync-ui,
// src/project/internal/projectvideosettings.cpp) : une vidéo tournée à un
// débit fractionnaire (23.976, 29.97, 59.94...) reste étiquetée par rapport à
// son débit nominal arrondi (24/30/60) pour l'affichage HH:MM:SS:FF, mais
// c'est le débit réel (fractionnaire) qui détermine quelle frame physique est
// affichée à un instant donné — utiliser le débit arrondi pour les deux
// ferait dériver silencieusement le numéro de frame affiché du vrai au fil
// d'une vidéo longue. N'implémente pas le drop-frame (saut périodique de
// numéro de frame pour que l'horloge affichée corresponde au temps réel à
// 29.97/59.94fps) : sans lui, le timecode affiché est censé dériver du temps
// réel sur de longues vidéos à ces débits, comme tout autre affichage NDF.
namespace VideoTimecode
{
inline juce::String format(double videoPositionSeconds, double frameRate)
{
    const double clampedFrameRate = juce::jlimit(1.0, 240.0, frameRate);
    const int roundedFrameRate = juce::jmax(1, static_cast<int>(std::lround(clampedFrameRate)));
    const int64_t totalFrames = static_cast<int64_t>(
        std::floor(juce::jmax(0.0, videoPositionSeconds) * clampedFrameRate + 0.5));

    const int64_t frames = totalFrames % roundedFrameRate;
    const int64_t totalSeconds = totalFrames / roundedFrameRate;
    const int64_t seconds = totalSeconds % 60;
    const int64_t minutes = (totalSeconds / 60) % 60;
    const int64_t hours = totalSeconds / 3600;

    return juce::String::formatted("%02lld:%02lld:%02lld:%02lld",
                                    (long long) hours, (long long) minutes,
                                    (long long) seconds, (long long) frames);
}

// Parse l'inverse d'un timecode formaté par format() ci-dessus
// (HH:MM:SS:FF), pour la saisie manuelle dans la sidebar (étape 6c). Port
// fidèle de VideoPanelModel::parseTimecodeToMs (même branche MuseScore).
// Renvoie -1 si le texte n'est pas un timecode valide pour ce frameRate
// (mauvais format, minutes/secondes hors 0..59, frame >= débit arrondi...).
inline int parseToMs(const juce::String& timecode, double frameRate)
{
    const juce::StringArray parts = juce::StringArray::fromTokens(timecode.trim(), ":", "");
    if (parts.size() != 4)
        return -1;

    const juce::String hoursText = parts[0];
    const juce::String minutesText = parts[1];
    const juce::String secondsText = parts[2];
    const juce::String framesText = parts[3];

    if (!hoursText.containsOnly("0123456789") || hoursText.isEmpty())
        return -1;
    const int hours = hoursText.getIntValue();

    if (!minutesText.containsOnly("0123456789") || minutesText.isEmpty())
        return -1;
    const int minutes = minutesText.getIntValue();
    if (minutes > 59)
        return -1;

    if (!secondsText.containsOnly("0123456789") || secondsText.isEmpty())
        return -1;
    const int seconds = secondsText.getIntValue();
    if (seconds > 59)
        return -1;

    const double clampedFrameRate = juce::jlimit(1.0, 240.0, frameRate);
    const int roundedFrameRate = juce::jmax(1, static_cast<int>(std::lround(clampedFrameRate)));

    if (!framesText.containsOnly("0123456789") || framesText.isEmpty())
        return -1;
    const int frames = framesText.getIntValue();
    if (frames >= roundedFrameRate)
        return -1;

    const int64_t totalSeconds = static_cast<int64_t>(hours) * 3600 + minutes * 60 + seconds;
    const int64_t totalFrames = totalSeconds * roundedFrameRate + frames;
    const int64_t positionMs = static_cast<int64_t>(
        std::floor((static_cast<double>(totalFrames) * 1000.0 / roundedFrameRate) + 0.5));

    if (positionMs > std::numeric_limits<int>::max())
        return -1;

    return static_cast<int>(positionMs);
}
}
