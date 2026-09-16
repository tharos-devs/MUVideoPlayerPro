#pragma once

#include <vector>

#include <juce_core/juce_core.h>

// Liste des derniers fichiers vidéo ouverts, la plus récente d'abord, max 5
// entrées, dédupliquée (rouvrir un fichier déjà présent le remonte en tête
// plutôt que de le dupliquer) -- port du modèle recent_files.json de
// MUTranscriber (~/Documents/MuseScore4/Plugins/MUTranscriber/recent_files.json),
// cf. §5 du cahier des charges ("état indépendant de la partition (liste de
// fichiers récents...) : fichier local"). Contrairement au chemin vidéo
// courant/l'offset/le volume/les hit points (Phase 6a, persistés avec le
// projet MuseScore via getStateInformation), cette liste est locale à la
// machine, partagée entre tous les projets/instances du plugin.
//
// Thread GUI uniquement.
class RecentFilesStore
{
public:
    RecentFilesStore();

    // Emplacement du fichier JSON injectable -- utilisé par les tests pour
    // ne jamais lire/écrire le vrai fichier de l'utilisateur (~/Library/
    // Application Support/MUVideoPlayerPro/recent_files.json).
    explicit RecentFilesStore(juce::File customStorageFile);

    const std::vector<juce::String>& paths() const noexcept { return recentPaths; }

    void add(const juce::String& path);
    void clear();

private:
    static juce::File defaultStorageFile();
    void load();
    void save() const;

    const juce::File storageFile;
    std::vector<juce::String> recentPaths;

    static constexpr int maxEntries = 5;
};
