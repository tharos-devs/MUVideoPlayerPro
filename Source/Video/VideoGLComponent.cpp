#include "VideoGLComponent.h"
#include "VideoDecoder.h"
#include "../Transport/SeekPolicy.h"

namespace
{
const char* vertexShaderSource = R"(
    attribute vec2 position;
    attribute vec2 texCoordIn;
    varying vec2 texCoordOut;

    void main()
    {
        gl_Position = vec4(position, 0.0, 1.0);
        texCoordOut = texCoordIn;
    }
)";

const char* fragmentShaderSource = R"(
    varying vec2 texCoordOut;
    uniform sampler2D videoTexture;

    void main()
    {
        gl_FragColor = texture2D(videoTexture, texCoordOut);
    }
)";
}

VideoGLComponent::VideoGLComponent()
{
    glContext.setRenderer(this);
    glContext.setContinuousRepainting(true);
    glContext.attachTo(*this);
}

VideoGLComponent::~VideoGLComponent()
{
    glContext.detach();
}

void VideoGLComponent::setSource(VideoFrameQueue* queue, const PositionCache* newPositionCache, VideoDecoder* decoder)
{
    frameQueue.store(queue, std::memory_order_release);
    positionCache.store(newPositionCache, std::memory_order_release);
    videoDecoder.store(decoder, std::memory_order_release);
}

void VideoGLComponent::setOffsetSeconds(double newOffsetSeconds)
{
    offsetSeconds.store(newOffsetSeconds, std::memory_order_release);
}

void VideoGLComponent::notifySourceReloaded() noexcept
{
    reloadRequested.store(true, std::memory_order_release);
}

void VideoGLComponent::clearDisplay() noexcept
{
    const juce::ScopedLock lock(uploadLock);
    hasPendingUpload = false;
    clearDisplayRequested = true;
}

void VideoGLComponent::paint(juce::Graphics&)
{
    // Rendu entièrement géré par OpenGL (renderOpenGL()).
}

void VideoGLComponent::resized()
{
}

void VideoGLComponent::newOpenGLContextCreated()
{
    using namespace ::juce::gl;

    ensureShaderCreated();

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoGLComponent::openGLContextClosing()
{
    using namespace ::juce::gl;

    if (textureId != 0)
    {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }

    textureWidth = 0;
    textureHeight = 0;
    shader.reset();
}

void VideoGLComponent::ensureShaderCreated()
{
    auto newShader = std::make_unique<juce::OpenGLShaderProgram>(glContext);

    if (!newShader->addVertexShader(vertexShaderSource)
        || !newShader->addFragmentShader(fragmentShaderSource)
        || !newShader->link())
    {
        jassertfalse;
        return;
    }

    shader = std::move(newShader);
}

void VideoGLComponent::uploadFrame(const VideoFrameQueue::Frame& frame)
{
    using namespace ::juce::gl;

    glBindTexture(GL_TEXTURE_2D, textureId);

    if (frame.width != textureWidth || frame.height != textureHeight)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width, frame.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
        textureWidth = frame.width;
        textureHeight = frame.height;
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height,
                         GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoGLComponent::pumpFromQueue()
{
    VideoFrameQueue* queue = frameQueue.load(std::memory_order_acquire);
    const PositionCache* position = positionCache.load(std::memory_order_acquire);
    VideoDecoder* decoder = videoDecoder.load(std::memory_order_acquire);

    if (queue != nullptr && position != nullptr)
    {
        const double desiredSeconds = position->positionSeconds() + offsetSeconds.load(std::memory_order_relaxed);

        // Anti-rebond (cf. commentaire de stabilityDelayMs dans le .h) : un
        // saut discontinu (bien plus grand qu'un delta de lecture normale
        // entre deux appels) redémarre la fenêtre de stabilisation.
        const auto nowMs = juce::Time::getMillisecondCounter();

        if (!hasPreviousTickDesired
            || std::abs(desiredSeconds - previousTickDesiredSeconds) > SeekPolicy::thresholdSeconds)
        {
            desiredStableSinceMs = nowMs;
        }

        previousTickDesiredSeconds = desiredSeconds;
        hasPreviousTickDesired = true;

        const bool desiredIsStable = (nowMs - desiredStableSinceMs) >= (juce::uint32) stabilityDelayMs;

        // Un nouveau fichier a été chargé (VideoDecoder::loadFile a bump sa
        // génération) : traité comme un seek déjà demandé, pour réutiliser le
        // même mécanisme de purge agressive de tout ce qui restait en attente
        // de l'ancienne vidéo (voir la boucle plus bas).
        if (reloadRequested.exchange(false, std::memory_order_acq_rel))
        {
            seekPending = true;
            seekTargetSeconds = desiredSeconds;
            seekTargetSetMs = nowMs;
        }

        // La position désirée peut changer *pendant* qu'un seek est déjà en
        // vol (ex : chargement d'une vidéo qui déclenche à la fois un seek
        // immédiat vers l'ancienne position ET un MMC Locate(0) vers
        // MuseScore, dont l'effet n'arrive qu'un peu plus tard via
        // PositionCache — ou un second clic pendant que le premier rattrapage
        // tourne encore). Sans cette re-vérification, la boucle plus bas
        // resterait bloquée à jamais à comparer les frames à une cible
        // désormais obsolète (seekPending == true bloque le déclenchement
        // normal plus bas, qui ne s'exécute que si !seekPending).
        //
        // Le garde-fou "desiredStableSinceMs > seekTargetSetMs" est essentiel :
        // sans lui, une lecture normale qui reprend PENDANT qu'un rattrapage
        // post-clic est encore en cours fait dériver desiredSeconds en continu
        // au fil du temps qui passe — le seek en vol n'a alors jamais
        // l'occasion d'aboutir, redirigé sans cesse vers une cible mouvante
        // avant même d'avoir pu se rapprocher (constaté : seekPending ne se
        // lève plus jamais, affichage figé sur la dernière frame d'avant le
        // clic, décodeur enchaînant les seeks en boucle). Ce garde-fou exige
        // qu'un VRAI saut discontinu (confirmé stable, donc pas juste du bruit
        // transitoire) se soit produit APRÈS le début du seek en cours — une
        // dérive progressive sans jamais aucun saut ne satisfait jamais cette
        // condition, quel que soit l'écart accumulé au fil du temps.
        if (decoder != nullptr && seekPending && desiredIsStable
            && desiredStableSinceMs > seekTargetSetMs
            && SeekPolicy::shouldSeek(desiredSeconds, seekTargetSeconds))
        {
            decoder->requestSeek(desiredSeconds);
            seekTargetSeconds = desiredSeconds;
            seekTargetSetMs = nowMs;
        }

        bool advanced = false;
        VideoFrameQueue::Frame frameToShow;
        bool haveFrameToShow = false;

        // Écart important déjà détecté mais seek pas encore déclenché
        // (anti-rebond en cours, cf. stabilityDelayMs) : ne PAS avancer dans
        // la queue de l'ancienne génération pour l'instant. Sans ce garde-
        // fou, la boucle plus bas — qui ne se soucie que de pts <= desired —
        // se met à défiler très vite à travers tout ce qu'il reste de
        // l'ancienne génération en attente (son pts trivialement <= une
        // desiredSeconds qui vient de faire un grand bond), un bref effet
        // "avance rapide" visible juste avant que le vrai seek ne parte.
        //
        // Le "!desiredIsStable" est essentiel : pendant une lecture normale,
        // desiredSeconds avance en continu par petits pas, et l'affichage
        // peut prendre un peu de retard (> seuil) sans qu'il s'agisse d'un
        // saut à traiter par un seek — juste un rattrapage séquentiel normal,
        // que la boucle plus bas sait déjà faire. Sans ce garde-fou distinct
        // du saut réel (desiredIsStable reste vrai en continu pendant une
        // lecture fluide, seul un vrai saut le repasse à faux temporairement),
        // ce figeage se déclenchait aussi en pleine lecture dès que le retard
        // dépassait le seuil, empêchant tout rattrapage normal et forçant un
        // reseek à répétition — constaté : lecture saccadée après une reprise
        // de lecture.
        const bool awaitingSeekDecision = hasShownAnyFrame && !seekPending && !desiredIsStable
            && SeekPolicy::shouldSeek(desiredSeconds, lastShownPtsSeconds);

        // Avance dans la queue tant que la frame suivante n'est pas dans le
        // futur par rapport à la position désirée. On ne peut pas "remettre"
        // une frame déjà pop()-ée : celle qui dépasse desiredSeconds est
        // gardée de côté (pendingNextFrame) pour le prochain appel.
        while (!awaitingSeekDecision)
        {
            if (!hasPendingNextFrame)
            {
                if (!queue->pop(pendingNextFrame))
                    break;

                hasPendingNextFrame = true;
            }

            // Décodée avant un seek : à jeter, PTS obsolète. Deux cas : (1)
            // un seek déjà pris en compte (displayedGeneration a déjà avancé)
            // l'a dépassée ; (2) un seek vient d'être demandé (seekPending) et
            // la queue peut encore contenir tout un lot de frames de l'ancienne
            // génération. On les jette activement toutes plutôt que de garder
            // la première comme "pending" : sinon elle resterait bloquée pour
            // toujours (son PTS ne dépasse jamais desiredSeconds tel quel) et
            // empêcherait la queue de se vider pour laisser passer les
            // nouvelles frames post-seek. displayedGeneration n'avance que
            // lorsqu'on affiche réellement une frame (voir plus bas) : tant
            // qu'on n'a rien affiché depuis le seek, le seuil de purge reste
            // fixe sur l'ancienne génération.
            if (pendingNextFrame.generation < displayedGeneration
                || (seekPending && pendingNextFrame.generation <= displayedGeneration))
            {
                hasPendingNextFrame = false;
                continue;
            }

            // Ici, pendingNextFrame.generation > displayedGeneration :
            // première frame vue de la nouvelle génération. Son PTS vient du
            // keyframe visé par av_seek_frame (arrondi arrière), pas
            // forcément proche de la position désirée si l'intervalle entre
            // keyframes est grand.
            if (pendingNextFrame.ptsSeconds > desiredSeconds)
                break; // dans le futur : on la garde en attente.

            // Rattrapage post-seek en cours : cette frame est <= desired mais
            // encore loin — on la consomme sans l'afficher (une frame plus
            // proche arrive sous peu, le décodage continue en tâche de fond,
            // correctement cadencé par la queue) plutôt que de défiler
            // visiblement à travers chaque frame intermédiaire jusqu'à la
            // cible. IMPORTANT : seekPending reste vrai tant qu'on n'a pas
            // trouvé de frame suffisamment proche pour être réellement
            // affichée (voir plus bas) — pas dès qu'on voit la première frame
            // de la nouvelle génération, sinon le déclencheur de seek
            // redevient actif avant que le rattrapage n'ait eu la moindre
            // chance d'aboutir et redemande le même seek en boucle (constaté
            // par le passé : des centaines de seeks par seconde, jamais
            // aucune frame affichée). Sûr maintenant que le décodage est
            // correctement cadencé (backpressure vidéo bloquante + audio non
            // bloquant) et que plus rien n'interrompt un rattrapage en cours
            // avant qu'il puisse aboutir.
            if (seekPending && SeekPolicy::shouldSeek(desiredSeconds, pendingNextFrame.ptsSeconds))
            {
                hasPendingNextFrame = false;
                continue;
            }

            seekPending = false;
            frameToShow = pendingNextFrame;
            haveFrameToShow = true;
            hasPendingNextFrame = false;
            advanced = true;
        }

        if (advanced && haveFrameToShow)
        {
            {
                const juce::ScopedLock lock(uploadLock);
                pendingUploadFrame = frameToShow;
                hasPendingUpload = true;
            }

            if (frameToShow.generation != displayedGeneration)
                lastGenerationChangeMs = nowMs;

            lastShownPtsSeconds = frameToShow.ptsSeconds;
            hasShownAnyFrame = true;
            displayedGeneration = frameToShow.generation;
        }

        // SeekPolicy (§7) : ne seeker que si l'écart dépasse le seuil de
        // tolérance, jamais systématiquement — un Stop qui ramène la position
        // à 0 pendant que le décodeur est loin devant (ou un gros saut dans
        // MuseScore) en est le cas typique ; sinon on laisse le décodage
        // séquentiel suivre normalement. Le délai de grâce (catchUpGraceMs)
        // laisse le décodage séquentiel combler tout seul le petit écart
        // résiduel typique juste après un seek (keyframe légèrement avant la
        // cible) avant d'envisager un nouveau seek.
        if (decoder != nullptr && hasShownAnyFrame && !seekPending && desiredIsStable
            && (nowMs - lastGenerationChangeMs) >= (juce::uint32) catchUpGraceMs
            && SeekPolicy::shouldSeek(desiredSeconds, lastShownPtsSeconds))
        {
            decoder->requestSeek(desiredSeconds);
            seekTargetSeconds = desiredSeconds;
            seekTargetSetMs = nowMs;
            seekPending = true;
        }
    }
}

void VideoGLComponent::renderOpenGL()
{
    using namespace ::juce::gl;

    juce::OpenGLHelpers::clear(juce::Colours::black);

    if (shader == nullptr || textureId == 0)
        return;

    {
        const juce::ScopedLock lock(uploadLock);

        if (clearDisplayRequested)
        {
            // textureWidth/Height à 0 fait sortir prématurément ci-dessous,
            // sans dessiner le quad -- seul le clear noir du haut reste visible.
            textureWidth = 0;
            textureHeight = 0;
            hasPendingUpload = false;
            clearDisplayRequested = false;
        }
        else if (hasPendingUpload)
        {
            uploadFrame(pendingUploadFrame);
            hasPendingUpload = false;
        }
    }

    if (textureWidth == 0 || textureHeight == 0)
        return;

    shader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    shader->setUniform("videoTexture", (GLint) 0);

    // Quad centré en conservant le ratio largeur/hauteur de la vidéo (letter/
    // pillarboxing, les bandes restant à la couleur de clear — noir) plutôt
    // que d'étirer sur tout le composant. texcoord.y inversé (les frames
    // FFmpeg sont stockées ligne du haut en premier, OpenGL attend l'origine
    // en bas à gauche).
    float quadHalfWidth = 1.0f;
    float quadHalfHeight = 1.0f;

    const float componentAspect = (float) juce::jmax(1, getWidth()) / (float) juce::jmax(1, getHeight());
    const float videoAspect = (float) textureWidth / (float) juce::jmax(1, textureHeight);

    if (videoAspect > componentAspect)
        quadHalfHeight = componentAspect / videoAspect;
    else
        quadHalfWidth = videoAspect / componentAspect;

    const GLfloat vertices[] = {
        // x,              y,               u,    v
        -quadHalfWidth, -quadHalfHeight,  0.0f, 1.0f,
         quadHalfWidth, -quadHalfHeight,  1.0f, 1.0f,
        -quadHalfWidth,  quadHalfHeight,  0.0f, 0.0f,
         quadHalfWidth,  quadHalfHeight,  1.0f, 0.0f,
    };

    const juce::OpenGLShaderProgram::Attribute positionAttr(*shader, "position");
    glVertexAttribPointer((GLuint) positionAttr.attributeID, 2, GL_FLOAT, GL_FALSE,
                           4 * sizeof(GLfloat), vertices);
    glEnableVertexAttribArray((GLuint) positionAttr.attributeID);

    const juce::OpenGLShaderProgram::Attribute texCoordAttr(*shader, "texCoordIn");
    glVertexAttribPointer((GLuint) texCoordAttr.attributeID, 2, GL_FLOAT, GL_FALSE,
                           4 * sizeof(GLfloat), vertices + 2);
    glEnableVertexAttribArray((GLuint) texCoordAttr.attributeID);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
}
