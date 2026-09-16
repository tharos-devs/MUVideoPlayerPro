#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>
#include <functional>

#include "VideoFrameQueue.h"
#include "AudioFrameQueue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// Décode un fichier vidéo H.264 (+ piste audio, si présente) dans un thread
// dédié, et pousse les frames vidéo (converties en RGBA) dans un
// VideoFrameQueue et les échantillons audio (resamplés au sample rate hôte,
// stéréo entrelacé) dans un AudioFrameQueue. Aucune I/O ni décodage ne doit
// jamais se produire depuis processBlock() : ce thread est le seul
// consommateur de FFmpeg (§6 du cahier des charges).
class VideoDecoder : private juce::Thread
{
public:
    VideoDecoder(VideoFrameQueue& outputQueue, AudioFrameQueue& audioOutputQueue);
    ~VideoDecoder() override;

    // Thread GUI uniquement. Ouvre le fichier, sélectionne les meilleurs flux
    // vidéo et audio, prépare les codecs et les conversions RGBA/resampling.
    // targetSampleRate : sample rate hôte, utilisé pour resampler l'audio
    // (absence de piste audio dans le fichier : pas une erreur, mixage
    // simplement silencieux pour ce fichier). Ne décode rien encore.
    bool loadFile(const juce::File& file, double targetSampleRate);

    void startDecoding();
    void stopDecoding();

    // Thread GUI uniquement. Arrête le décodage et libère le fichier chargé --
    // remet le décodeur dans l'état "aucune vidéo", comme avant tout appel à
    // loadFile() (onglet Settings, bouton "Clear video").
    void unloadFile();

    // Thread GUI/rendu OpenGL. Demande un seek à la position donnée, appliqué
    // par le thread de décodage au prochain passage dans decodeLoop() (§7 :
    // seek conditionnel — c'est à l'appelant de décider, via SeekPolicy,
    // qu'un seek est justifié plutôt que de laisser le décodage séquentiel
    // rattraper son retard). Dernier appel gagne si plusieurs s'accumulent
    // avant d'être consommés.
    void requestSeek(double seconds) noexcept;

    // Thread audio (processBlock). Incrémenté à chaque requestSeek(), quel
    // qu'en soit l'origine (VideoGLComponent ou rechargement de fichier) :
    // permet à mixVideoAudio() (qui ne décide plus jamais lui-même de seeker,
    // cf. son commentaire de classe dans PluginProcessor.h) de savoir qu'un
    // seek vient d'être demandé et de se mettre en mode rattrapage
    // immédiatement, plutôt que de ne le découvrir qu'en tombant sur un bloc
    // de la nouvelle génération — trop tard si des blocs de l'ancienne
    // génération l'ont précédé dans la file (constaté : un seek demandé
    // pendant que le transport est à l'arrêt, donc invisible pour
    // mixVideoAudio tant que la lecture n'a pas repris, laissait jouer ce
    // backlog obsolète avant de rattraper la bonne génération).
    uint32_t getSeekRequestSequence() const noexcept { return seekRequestSequence.load(std::memory_order_acquire); }

    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }
    double getDurationSeconds() const noexcept { return durationSeconds; }

    // Débit nominal (frames/s), pour l'affichage du timecode SMPTE non-drop-
    // frame de la timeline (§4 du cahier des charges). Valeur par défaut
    // (24) tant qu'aucune vidéo n'est chargée, cohérente avec celle utilisée
    // par le modèle MuseScore porté ici (voir HitPointList.h). Éditable par
    // l'utilisateur (onglet Settings) : peut diverger de getDetectedFrameRate()
    // une fois modifié manuellement, comme VideoPanelModel::frameRate côté
    // MuseScore (une propriété indépendante, pas un simple miroir du fichier).
    double getFrameRate() const noexcept { return frameRate; }
    void setFrameRate(double newFrameRate) noexcept { frameRate = juce::jlimit(1.0, 240.0, newFrameRate); }

    // Valeur détectée automatiquement au chargement (avg_frame_rate/
    // r_frame_rate FFmpeg), conservée séparément de getFrameRate() pour que
    // le bouton "Detect" de l'onglet Settings puisse revenir dessus après
    // une correction manuelle, sans avoir à rouvrir le fichier.
    double getDetectedFrameRate() const noexcept { return autoDetectedFrameRate; }

    // Pour l'onglet "Information" (étape 6d) : nom lisible du codec vidéo/
    // audio (ex : "H.264", "AAC"), chaîne vide si aucune vidéo chargée ou pas
    // de piste audio.
    juce::String getVideoCodecName() const { return videoCodecName; }
    juce::String getAudioCodecName() const { return audioCodecName; }

    // Exposé pour les tests : décode de manière synchrone (bloquant) jusqu'à
    // la fin du fichier ou jusqu'à ce que shouldStop() renvoie true.
    void decodeLoop(const std::function<bool()>& shouldStop);

private:
    void run() override;
    void closeInput();
    void performSeek(double targetSeconds);

    VideoFrameQueue& outputQueue;
    AudioFrameQueue& audioOutputQueue;

    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwsContext* swsContext = nullptr;
    AVFrame* decodedFrame = nullptr;
    AVFrame* rgbaFrame = nullptr;
    AVPacket* packet = nullptr;

    int videoStreamIndex = -1;
    int width = 0;
    int height = 0;
    double durationSeconds = 0.0;
    double frameRate = 24.0;
    double autoDetectedFrameRate = 24.0;
    juce::String videoCodecName;
    juce::String audioCodecName;
    AVRational streamTimeBase { 1, 1 };

    // Piste audio : absente d'un fichier (audioStreamIndex == -1) n'est pas
    // une erreur, juste un fichier dont le mixage restera silencieux.
    int audioStreamIndex = -1;
    AVCodecContext* audioCodecContext = nullptr;
    SwrContext* swrContext = nullptr;
    AVFrame* decodedAudioFrame = nullptr;
    AVRational audioStreamTimeBase { 1, 1 };
    double targetSampleRate = 44100.0;

    // Tampon de sortie du resampling (swr_convert), réutilisé d'un appel à
    // l'autre pour éviter une allocation par bloc décodé.
    std::vector<float> resampleScratch;

    // Écrits depuis le thread GUI/rendu (requestSeek), consommés depuis le
    // thread de décodage (au tout début de decodeLoop()).
    std::atomic<bool> seekRequested { false };
    std::atomic<double> pendingSeekSeconds { 0.0 };

    // Écrit dans requestSeek() (thread GUI/rendu), lu depuis le thread audio
    // — cf. getSeekRequestSequence().
    std::atomic<uint32_t> seekRequestSequence { 0 };

    // Lu/écrit uniquement depuis le thread de décodage : incrémenté à chaque
    // seek effectué, propagé aux frames poussées (VideoFrameQueue::Frame::
    // generation) pour que le consommateur ignore les frames pré-seek.
    uint32_t currentGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoDecoder)
};
