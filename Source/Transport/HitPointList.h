#pragma once

#include <algorithm>
#include <vector>

#include <juce_core/juce_core.h>

#include "HitPoint.h"

// CRUD sur les hit points d'une vidéo chargée. Thread GUI uniquement (comme
// le chargement de fichier ou le réglage de l'offset), jamais touché depuis
// processBlock(). Toujours triée chronologiquement par timeMs après chaque
// modification ; les appelants doivent référencer un point par son id, pas
// par sa position dans la liste (cf. HitPoint.h).
class HitPointList
{
public:
    const std::vector<HitPoint>& items() const noexcept { return hitPoints; }

    // Étiquette par défaut ("#N") dérivée de l'id à venir, pas de la taille
    // de la liste : la taille est réutilisée après une suppression (ex :
    // supprimer "#2" parmi "#1"/"#2"/"#3" puis en ajouter un nouveau donnerait
    // à nouveau "#3", en doublon du "#3" déjà présent), alors que les id ne
    // sont jamais réutilisés. Renvoie l'id attribué au nouveau point.
    int add(int timeMs)
    {
        timeMs = std::max(0, timeMs);

        int nextId = 0;
        for (const auto& hitPoint : hitPoints)
            nextId = std::max(nextId, hitPoint.id);

        HitPoint hitPoint;
        hitPoint.id = nextId + 1;
        hitPoint.timeMs = timeMs;
        hitPoint.label = "#" + juce::String(hitPoint.id);
        hitPoints.push_back(hitPoint);

        sortByTime();
        return hitPoint.id;
    }

    void remove(int id)
    {
        hitPoints.erase(std::remove_if(hitPoints.begin(), hitPoints.end(),
                                        [id](const HitPoint& hitPoint) { return hitPoint.id == id; }),
                         hitPoints.end());
    }

    void rename(int id, const juce::String& label)
    {
        if (auto* hitPoint = find(id))
        {
            const juce::String trimmed = label.trim();
            hitPoint->label = trimmed.isEmpty() ? ("Hit " + juce::String(id)) : trimmed;
        }
    }

    void setTimeMs(int id, int timeMs)
    {
        if (auto* hitPoint = find(id))
        {
            hitPoint->timeMs = std::max(0, timeMs);
            sortByTime();
        }
    }

    void setColour(int id, uint32_t colour)
    {
        if (auto* hitPoint = find(id))
            hitPoint->colour = colour;
    }

    void clear() noexcept { hitPoints.clear(); }

    // Remplace le contenu par des points rechargés depuis la persistance :
    // retrie chronologiquement et attribue un id réel aux entrées
    // enregistrées avant l'existence du champ id (id == 0, cf. HitPoint.h).
    void restoreFrom(std::vector<HitPoint> loaded)
    {
        hitPoints = std::move(loaded);
        sortByTime();

        int nextId = 0;
        for (const auto& hitPoint : hitPoints)
            nextId = std::max(nextId, hitPoint.id);
        for (auto& hitPoint : hitPoints)
            if (hitPoint.id == 0)
                hitPoint.id = ++nextId;
    }

private:
    HitPoint* find(int id) noexcept
    {
        for (auto& hitPoint : hitPoints)
            if (hitPoint.id == id)
                return &hitPoint;
        return nullptr;
    }

    void sortByTime()
    {
        std::stable_sort(hitPoints.begin(), hitPoints.end(),
                          [](const HitPoint& a, const HitPoint& b) { return a.timeMs < b.timeMs; });
    }

    std::vector<HitPoint> hitPoints;
};
