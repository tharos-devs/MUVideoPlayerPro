#pragma once

#include <juce_core/juce_core.h>

// Point de repère nommé sur la timeline vidéo (port du modèle de données du
// panel vidéo MuseScore, PR musescore/MuseScore#34744 — jamais fusionnée en
// amont, cf. PROJECT_CONTEXT.md). id est l'identité stable du point (jamais
// réutilisé, jamais dérivé de sa position dans la liste : celle-ci est
// retriée chronologiquement à chaque modification) ; 0 signifie "pas encore
// attribué", HitPointList en attribue un réel à la création/au chargement.
struct HitPoint
{
    int id = 0;
    juce::String label;
    int timeMs = 0;
    uint32_t colour = 0x3B94E5;

    bool operator==(const HitPoint& other) const noexcept
    {
        return id == other.id && label == other.label && timeMs == other.timeMs && colour == other.colour;
    }

    bool operator!=(const HitPoint& other) const noexcept
    {
        return !(*this == other);
    }
};
