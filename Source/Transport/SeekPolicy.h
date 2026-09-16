#pragma once

#include <cmath>

// Seuil de tolérance avant de déclencher un seek dans le flux vidéo (§7 du
// cahier des charges) : en dessous, on laisse le décodage séquentiel
// rattraper son retard/avance normalement. Un seek systématique au (re)démarrage
// de la lecture provoquerait un flush du pipeline codec à chaque play/pause.
namespace SeekPolicy
{
constexpr double thresholdSeconds = 0.2;

inline bool shouldSeek(double desiredSeconds, double referenceSeconds) noexcept
{
    return std::abs(desiredSeconds - referenceSeconds) > thresholdSeconds;
}
}
