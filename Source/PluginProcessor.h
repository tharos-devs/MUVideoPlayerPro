#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Transport/HitPointList.h"
#include "Transport/PositionCache.h"
#include "Transport/RecentFilesStore.h"
#include "Video/VideoDecoder.h"
#include "Video/VideoFrameQueue.h"
#include "Video/AudioFrameQueue.h"

class MUVideoPlayerProAudioProcessor : public juce::AudioProcessor
{
public:
    MUVideoPlayerProAudioProcessor();
    ~MUVideoPlayerProAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Thread GUI uniquement : demande l'émission d'une commande MMC au
    // prochain processBlock(). Écrase toute demande en attente non encore
    // consommée (dernier appel gagne).
    void requestPlay();
    void requestPause();
    void requestStop();
    void requestLocate(double positionSeconds);

    // Thread GUI uniquement. Gain appliqué à l'audio de la vidéo (0..1) et
    // coupure totale ; lus depuis le thread audio dans mixVideoAudio().
    void setVolume(float newVolume) { volume.store(juce::jlimit(0.0f, 1.0f, newVolume), std::memory_order_relaxed); }
    void setMuted(bool shouldBeMuted) { muted.store(shouldBeMuted, std::memory_order_relaxed); }
    float getVolume() const { return volume.load(std::memory_order_relaxed); }
    bool isMuted() const { return muted.load(std::memory_order_relaxed); }

    // Thread GUI uniquement. Décalage signé vidéo/partition (§4 du cahier des
    // charges) : temps vidéo affiché = position MuseScore + offset. Poussé
    // vers VideoGLComponent::setOffsetSeconds() par l'éditeur (à sa création
    // et à chaque modification), puisque ce composant est recréé à chaque
    // ouverture/fermeture de la fenêtre du plugin alors que cette valeur doit
    // survivre à l'éditeur (persistée avec le reste de l'état, §5).
    double getOffsetSeconds() const { return offsetSeconds; }
    void setOffsetSeconds(double newOffsetSeconds) { offsetSeconds = newOffsetSeconds; }

    const PositionCache& getPositionCache() const { return positionCache; }

    // Thread GUI uniquement. Relance le décodeur sur le fichier donné.
    bool loadVideoFile(const juce::File& file);

    // Thread GUI uniquement. Onglet Settings, bouton "Clear video" : décharge
    // le fichier courant. Ne touche pas aux hit points/offset/volume -- ce ne
    // sont que des nombres, pas invalides sans vidéo, et rien ne demande de
    // les effacer aussi.
    void clearVideoFile();
    VideoFrameQueue& getVideoFrameQueue() { return videoFrameQueue; }
    VideoDecoder& getVideoDecoder() { return videoDecoder; }

    // Thread GUI uniquement. Chemin du fichier actuellement chargé (vide si
    // aucun), pour la persistance (§5) et le futur onglet "Information".
    const juce::File& getCurrentVideoFile() const { return currentVideoFile; }

    // Thread GUI uniquement. CRUD des hit points de la vidéo courante.
    HitPointList& getHitPoints() { return hitPoints; }
    const HitPointList& getHitPoints() const { return hitPoints; }

    // Thread GUI uniquement. Liste des derniers fichiers vidéo ouverts (§5
    // du cahier des charges : état local à la machine, indépendant du
    // projet -- pas persisté via getStateInformation, cf. RecentFilesStore.h).
    RecentFilesStore& getRecentFiles() { return recentFiles; }

private:
    enum class PendingMmc
    {
        None,
        Play,
        Pause,
        Stop,
        Locate,
    };

    // Thread audio uniquement (processBlock) : mixe l'audio déjà décodé/
    // resamplé dans le bus de sortie, en s'additionnant au passthrough hôte
    // déjà présent dans buffer. Silencieux tant que MuseScore ne joue pas
    // (comme la vidéo, figée à l'arrêt) ou tant que rien n'est encore
    // synchronisé après un (re)chargement/seek.
    void mixVideoAudio(juce::AudioBuffer<float>& buffer);

    PositionCache positionCache;

    // Sert de tampon d'avance de décodage, pas d'historique : le rendu
    // (VideoGLComponent) consomme au rythme de la position réelle de
    // MuseScore, donc la capacité n'a besoin d'absorber que les à-coups
    // transitoires entre décodage et affichage. Vidéo et audio partagent un
    // seul thread de décodage séquentiel ; la vidéo bloque volontairement
    // quand sa file est pleine (vraie régulation de rythme), donc une
    // capacité trop juste ici fait bloquer ce thread plus souvent que
    // nécessaire pendant une lecture normale — ce qui affame aussi la
    // production audio pendant ces pauses (constaté : l'audio, moins bien
    // approvisionné, finissait par accumuler un écart déclenchant son propre
    // seek correctif, qui perturbait la vidéo déjà bien synchronisée). Une
    // capacité plus généreuse (~2s de vidéo) laisse le décodage prendre de
    // l'avance sans bloquer aussi souvent.
    VideoFrameQueue videoFrameQueue { 60 };

    // Capacité en blocs (pas en échantillons) : chaque bloc correspond à un
    // paquet audio décodé (souvent ~1024 échantillons), comme VideoFrameQueue
    // pour la vidéo — cf. son commentaire pour le pourquoi d'une capacité
    // généreuse plutôt que juste "large".
    AudioFrameQueue audioFrameQueue { 256 };

    VideoDecoder videoDecoder { videoFrameQueue, audioFrameQueue };

    // Thread audio uniquement (processBlock) : état de synchronisation du
    // flux audio avec la position MuseScore.
    //
    // Contrairement aux versions précédentes, l'audio ne décide JAMAIS lui-
    // même de demander un seek : il suit passivement les générations produites
    // par le décodeur, quel que soit qui a déclenché le seek (VideoGLComponent
    // ou un rechargement de fichier). Avoir deux décideurs indépendants
    // (vidéo et audio) sur un même décodeur partagé s'est avéré activement
    // nuisible : dès qu'ils étaient légèrement en désaccord (dérive naturelle
    // inévitable entre deux threads consommant à des rythmes différents),
    // chacun réclamait son propre seek correctif, interrompant le rattrapage
    // de l'autre avant qu'il n'aboutisse — d'où les sursauts audibles/visibles
    // et, pire, des rattrapages vidéo jamais menés à terme. La vidéo reste
    // seule décisionnaire (elle pilote l'affichage, c'est le signal principal
    // pour l'utilisateur) ; l'audio se contente de suivre.
    AudioFrameQueue::Chunk pendingAudioChunk;
    bool hasPendingAudioChunk = false;
    int pendingAudioChunkReadFrames = 0;
    uint32_t audioSyncedGeneration = 0;

    // Vrai dès qu'une génération jamais vue apparaît (peu importe qui a
    // déclenché le seek correspondant) et jusqu'à ce qu'un bloc suffisamment
    // proche de la position désirée ait été réellement joué : sert à la fois
    // à purger l'ancien backlog et à ne pas jouer le rattrapage intermédiaire
    // (même principe que seekPending côté VideoGLComponent, mais purement
    // réactif ici, jamais à l'origine d'un requestSeek()).
    bool audioCatchingUp = false;

    // Dernière valeur observée de VideoDecoder::getSeekRequestSequence() :
    // permet de détecter qu'un seek vient d'être demandé (par la vidéo ou un
    // rechargement) et de lever audioCatchingUp immédiatement, plutôt que de
    // ne le découvrir qu'en tombant sur un bloc de la nouvelle génération —
    // trop tard si du backlog de l'ancienne génération le précède dans la
    // file (cas vécu : un seek demandé pendant que le transport est à
    // l'arrêt, donc invisible pour mixVideoAudio tant que !isPlaying()).
    uint32_t lastObservedSeekRequestSequence = 0;

    bool hasSyncedAudioOnce = false;
    double lastAudioPtsSeconds = 0.0;

    // Écrit depuis le thread GUI (loadVideoFile), consommé depuis le thread
    // audio (mixVideoAudio) : signale qu'un nouveau fichier vient d'être
    // chargé, à traiter comme un seek pour purger tout ce qui restait en
    // attente de l'ancienne vidéo (même mécanisme que VideoGLComponent::
    // notifySourceReloaded côté rendu).
    std::atomic<bool> audioReloadRequested { false };

    std::atomic<PendingMmc> pendingMmc { PendingMmc::None };
    std::atomic<double> pendingLocateSeconds { 0.0 };

    std::atomic<float> volume { 1.0f };
    std::atomic<bool> muted { false };

    // Non lus depuis processBlock() : GUI uniquement, comme le reste de
    // l'état édité par l'utilisateur (chemin vidéo, hit points).
    double offsetSeconds = 0.0;
    juce::File currentVideoFile;
    HitPointList hitPoints;
    RecentFilesStore recentFiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MUVideoPlayerProAudioProcessor)
};
