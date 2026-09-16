#pragma once

#include <juce_opengl/juce_opengl.h>
#include <atomic>
#include <memory>

#include "VideoFrameQueue.h"
#include "../Transport/PositionCache.h"

class VideoDecoder;

// Affiche, via un quad texturé OpenGL, la frame vidéo dont le PTS correspond
// à la position de lecture courante (PositionCache + offset).
//
// Sur macOS, le rendu OpenGL réel (renderOpenGL(), et donc l'upload GL de la
// texture) peut être suspendu par l'OS quand la fenêtre de l'éditeur est
// masquée par une autre (ex : l'utilisateur clique dans la partition
// MuseScore) — CVDisplayLink est throttled pour les fenêtres occluses, et
// rien côté JUCE ne permet de le forcer de façon fiable (vérifié : ni
// setContinuousRepainting ni triggerRepaint() n'y changent quoi que ce soit
// une fois la fenêtre masquée). La décision "quelle frame afficher /
// faut-il seeker" (pumpFromQueue()) est donc volontairement découplée du
// rendu GL lui-même et pilotée depuis un juce::Timer côté éditeur (thread
// message, fiable en toutes circonstances) : elle avance dans la queue déjà
// décodée et, quand l'écart avec la position désirée dépasse
// SeekPolicy::thresholdSeconds, demande un seek au VideoDecoder plutôt que
// d'attendre que le décodage séquentiel rattrape son retard. La frame ainsi
// choisie est déposée dans un tampon protégé par verrou ; renderOpenGL() se
// contente de l'uploader vers la texture GL dès qu'un rendu réel a
// effectivement lieu (immédiat si la fenêtre est visible, différé jusqu'au
// retour du focus sinon).
class VideoGLComponent : public juce::Component,
                          private juce::OpenGLRenderer
{
public:
    VideoGLComponent();
    ~VideoGLComponent() override;

    // Thread GUI uniquement. La queue/le cache/le décodeur doivent rester
    // valides tant qu'ils sont attachés (passer nullptr pour détacher avant
    // destruction).
    void setSource(VideoFrameQueue* queue, const PositionCache* positionCache, VideoDecoder* decoder);

    // Thread GUI uniquement. À appeler juste après qu'un nouveau fichier a été
    // chargé (VideoDecoder::loadFile bump alors sa génération) : force
    // pumpFromQueue() à jeter tout ce qui restait en attente de l'ancienne
    // vidéo (frame parquée y compris) au lieu de rester bloqué dessus tant
    // qu'une frame de la nouvelle vidéo n'est pas arrivée.
    void notifySourceReloaded() noexcept;

    // Thread GUI uniquement. À appeler quand plus aucune vidéo n'est chargée
    // (ex : "Clear video") : sans ceci, renderOpenGL() continue d'afficher la
    // dernière frame uploadée dans la texture GL indéfiniment -- rien ne lui
    // dit jamais "il n'y a plus rien à montrer". Le prochain rendu GL revient
    // au fond noir uni (déjà le clear color de renderOpenGL()).
    void clearDisplay() noexcept;

    // Thread GUI uniquement. À appeler périodiquement (ex : depuis le
    // juce::Timer déjà utilisé pour le label de position, ~30Hz) : avance
    // dans la queue déjà décodée jusqu'à la position désirée courante et
    // déclenche un seek si l'écart dépasse le seuil de tolérance (§7). C'est
    // ici, pas dans renderOpenGL(), que vit la logique de SeekPolicy — voir
    // le commentaire de classe pour le pourquoi.
    void pumpFromQueue();

    // Thread GUI uniquement. Offset utilisateur, signé (§4 du cahier des
    // charges) : temps vidéo affiché = position MuseScore + offset.
    void setOffsetSeconds(double offsetSeconds);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void ensureShaderCreated();
    void uploadFrame(const VideoFrameQueue::Frame& frame);

    juce::OpenGLContext glContext;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;

    // Texture GL gérée à la main (pas juce::OpenGLTexture::loadARGB) : le
    // layout mémoire de juce::PixelARGB dépend de la plateforme, alors que
    // nos frames FFmpeg sont en RGBA8888 fixe (AV_PIX_FMT_RGBA) — plus
    // simple et sans ambiguïté d'uploader nous-mêmes via glTexImage2D.
    unsigned int textureId = 0;

    // Écrits depuis le thread GUI (setSource/setOffsetSeconds), lus depuis
    // pumpFromQueue() (thread GUI aussi, mais via le Timer plutôt que
    // directement) : atomiques par prudence, sans coût réel ici.
    std::atomic<VideoFrameQueue*> frameQueue { nullptr };
    std::atomic<const PositionCache*> positionCache { nullptr };
    std::atomic<VideoDecoder*> videoDecoder { nullptr };
    std::atomic<double> offsetSeconds { 0.0 };
    std::atomic<bool> reloadRequested { false };

    int textureWidth = 0;
    int textureHeight = 0;

    // Poignée entre pumpFromQueue() (thread GUI/Timer, producteur) et
    // renderOpenGL() (thread de rendu GL, consommateur) : la frame choisie
    // par pumpFromQueue() attend ici qu'un rendu GL ait effectivement lieu
    // pour être uploadée. Verrou léger (CriticalSection), pas le thread
    // audio temps réel — un mutex est approprié ici.
    juce::CriticalSection uploadLock;
    VideoFrameQueue::Frame pendingUploadFrame;
    bool hasPendingUpload = false;
    bool clearDisplayRequested = false;

    // Lus/écrits uniquement depuis pumpFromQueue() : frame en attente dont le
    // PTS est dans le futur par rapport à la position courante (on ne peut
    // pas la "remettre" dans la queue une fois pop()).
    VideoFrameQueue::Frame pendingNextFrame;
    bool hasPendingNextFrame = false;

    // Lus/écrits uniquement depuis pumpFromQueue() : suivi de SeekPolicy
    // (génération de la dernière frame choisie, dernier PTS choisi, et si un
    // seek a déjà été demandé pour éviter de le redemander en boucle tant que
    // le décodeur ne l'a pas pris en compte).
    uint32_t displayedGeneration = 0;
    double lastShownPtsSeconds = 0.0;
    bool hasShownAnyFrame = false;
    bool seekPending = false;

    // Position réellement demandée au décodeur par le dernier requestSeek()
    // émis (valable tant que seekPending est vrai) : permet de détecter que
    // la position désirée a bougé depuis, et de corriger le tir plutôt que
    // de rester bloqué à comparer les frames à une cible périmée.
    double seekTargetSeconds = 0.0;

    // Horodatage du dernier requestSeek() émis. Sert à distinguer, pendant un
    // rattrapage en cours, un VRAI nouveau saut discontinu (ex : un second
    // clic pendant que le premier rattrapage tourne encore) d'une simple
    // dérive progressive de desiredSeconds due à la lecture normale qui a
    // repris entre-temps : seul le premier cas doit rediriger le seek en vol
    // (cf. le garde-fou dans pumpFromQueue() qui compare ceci à
    // desiredStableSinceMs).
    juce::int64 seekTargetSetMs = 0;

    // Anti-rebond avant de déclencher un seek (§7 : ne jamais seeker sur du
    // bruit). Un saut discontinu de desiredSeconds (ex : l'utilisateur clique
    // ailleurs dans la partition — MuseScore peut aussi générer plusieurs
    // sauts transitoires très rapprochés pour un seul clic, ex : prévisualisa-
    // tion de note) redémarre la fenêtre de stabilisation ; le seek n'est
    // effectivement demandé qu'une fois la position désirée immobile depuis
    // stabilityDelayMs. Ceci ne touche en rien la précision de la synchro
    // finale (toujours calculée depuis la position réelle, jamais approximée)
    // — seulement le délai avant d'y réagir, pour éviter qu'une rafale de
    // sauts n'annule chaque tentative de seek avant qu'elle ait pu aboutir.
    static constexpr int stabilityDelayMs = 150;
    double previousTickDesiredSeconds = 0.0;
    bool hasPreviousTickDesired = false;
    juce::int64 desiredStableSinceMs = 0;

    // Délai de grâce après un changement de génération (nouveau seek pris en
    // compte, première frame affichée) avant de considérer qu'un nouveau seek
    // est nécessaire — même s'il reste un écart au-delà du seuil. La première
    // frame après un seek vient du keyframe visé par av_seek_frame, donc
    // encore un peu avant la cible : le décodage séquentiel comble cet écart
    // tout seul en quelques frames (aucun seek nécessaire), mais ça prend un
    // instant. Sans ce délai, le déclencheur normal repart aussitôt (le tout
    // prochain pumpFromQueue(), avant que le décodage n'ait eu la moindre
    // chance de continuer) et redemande sans cesse le même seek vers le même
    // keyframe — constaté : bloqué indéfiniment à quelques centaines de ms de
    // la cible, jamais de progression. Comme stabilityDelayMs, ceci ne retarde
    // que la décision de reseeker, jamais la précision de la cible finale.
    static constexpr int catchUpGraceMs = 300;
    juce::int64 lastGenerationChangeMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VideoGLComponent)
};
