#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Transport/MMCEncoder.h"
#include "Transport/SeekPolicy.h"

#include <cmath>
#include <vector>

MUVideoPlayerProAudioProcessor::MUVideoPlayerProAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

MUVideoPlayerProAudioProcessor::~MUVideoPlayerProAudioProcessor() = default;

void MUVideoPlayerProAudioProcessor::prepareToPlay(double, int)
{
}

void MUVideoPlayerProAudioProcessor::releaseResources()
{
}

bool MUVideoPlayerProAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void MUVideoPlayerProAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Lecture du ProcessContext hôte (position/état), sans I/O ni allocation.
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            const int64_t samples = position->getTimeInSamples().orFallback(0);
            const bool playing = position->getIsPlaying();
            positionCache.update(samples, getSampleRate(), playing);
        }
    }

    mixVideoAudio(buffer);

    // Émission MMC (sens plugin -> MuseScore), déclenchée depuis le thread
    // GUI via request*() et consommée ici. Dernier appel gagne.
    const PendingMmc command = pendingMmc.exchange(PendingMmc::None, std::memory_order_acq_rel);

    if (command != PendingMmc::None)
    {
        juce::MidiMessage sysEx;

        switch (command)
        {
        case PendingMmc::Play:
            sysEx = MMCEncoder::play();
            break;
        case PendingMmc::Pause:
            sysEx = MMCEncoder::pause();
            break;
        case PendingMmc::Stop:
            sysEx = MMCEncoder::stop();
            break;
        case PendingMmc::Locate:
            sysEx = MMCEncoder::locate(pendingLocateSeconds.load(std::memory_order_relaxed));
            break;
        case PendingMmc::None:
            break;
        }

        midiMessages.addEvent(sysEx, 0);
    }
}

void MUVideoPlayerProAudioProcessor::mixVideoAudio(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int outChannels = juce::jmin(buffer.getNumChannels(), AudioFrameQueue::numChannels);

    if (outChannels <= 0 || numSamples <= 0)
        return;

    // Un nouveau fichier vient d'être chargé : à traiter comme un seek pour
    // purger tout ce qui restait en attente de l'ancienne vidéo (même
    // mécanisme que VideoGLComponent::notifySourceReloaded côté rendu).
    if (audioReloadRequested.exchange(false, std::memory_order_acq_rel))
        audioCatchingUp = true;

    // Un seek vient d'être demandé (par la vidéo ou par le chargement d'un
    // fichier) — à vérifier même si le transport est à l'arrêt (donc AVANT le
    // retour anticipé ci-dessous) : sans ça, un seek demandé pendant l'arrêt
    // resterait invisible jusqu'à ce qu'un bloc de la nouvelle génération soit
    // tombé dessus par hasard, après avoir déjà laissé jouer du backlog de
    // l'ancienne génération une fois la lecture reprise.
    const uint32_t currentSeekRequestSequence = videoDecoder.getSeekRequestSequence();
    if (currentSeekRequestSequence != lastObservedSeekRequestSequence)
    {
        audioCatchingUp = true;
        lastObservedSeekRequestSequence = currentSeekRequestSequence;
    }

    const bool isPlayingNow = positionCache.isPlaying();
    const double sampleRate = getSampleRate();

    if (sampleRate <= 0.0)
        return;

    const double desiredSeconds = positionCache.positionSeconds();
    int written = 0;

    // Le drainage du backlog obsolète (ci-dessous) doit continuer à tourner
    // MÊME à l'arrêt — contrairement à VideoGLComponent::pumpFromQueue (qui,
    // lui, tourne toujours, quel que soit l'état de MuseScore), cette
    // fonction s'arrêtait auparavant net dès !isPlayingNow, ce qui laissait
    // l'éventuel backlog périmé stagner indéfiniment dans la file au lieu
    // d'être purgé. Conséquence vécue : si ce backlog remplit toute la
    // capacité de la file pendant que le transport est à l'arrêt, les blocs
    // de la génération SUIVANTE produits par le décodeur (poussés en mode non
    // bloquant côté audio, cf. VideoDecoder::decodeLoop) se faisaient rejeter
    // faute de place — plus rien à rattraper une fois la lecture reprise,
    // silence indéfini. Tant que !isPlayingNow, on continue donc à purger/
    // faire progresser le rattrapage, mais sans jamais copier quoi que ce
    // soit dans le buffer (silence, comme avant).
    while (isPlayingNow ? (written < numSamples) : true)
    {
        if (!hasPendingAudioChunk)
        {
            if (!audioFrameQueue.pop(pendingAudioChunk))
                break; // rien de disponible pour l'instant : le reste reste silencieux.

            pendingAudioChunkReadFrames = 0;
            hasPendingAudioChunk = true;
        }

        // Backlog obsolète : soit strictement plus ancien que ce qui est déjà
        // synchronisé, soit (rattrapage en cours, audioCatchingUp) encore de
        // l'ancienne génération avant que la nouvelle n'ait été acceptée —
        // cf. VideoGLComponent::pumpFromQueue pour le détail du même
        // raisonnement côté image.
        if (pendingAudioChunk.generation < audioSyncedGeneration
            || (audioCatchingUp && pendingAudioChunk.generation <= audioSyncedGeneration))
        {
            hasPendingAudioChunk = false;
            continue;
        }

        const double chunkStartSeconds = pendingAudioChunk.ptsSeconds
                                        + static_cast<double>(pendingAudioChunkReadFrames) / sampleRate;

        // Génération jamais vue jusqu'ici : peu importe qui a déclenché ce
        // seek (l'audio ne le fait plus jamais lui-même), on entre en mode
        // rattrapage tant qu'on n'est pas encore assez proche pour jouer.
        if (pendingAudioChunk.generation > audioSyncedGeneration)
            audioCatchingUp = true;

        // Ce bloc vient du keyframe visé par av_seek_frame, pas forcément
        // proche de la position désirée — on le consomme sans le jouer tant
        // qu'il reste trop loin (éviter un artefact audible en rejouant
        // chaque bloc intermédiaire jusqu'à la cible).
        if (audioCatchingUp && SeekPolicy::shouldSeek(desiredSeconds, chunkStartSeconds))
        {
            hasPendingAudioChunk = false;
            continue;
        }

        // Ce bloc est frais et assez proche : prêt à être joué, mais tant que
        // le transport est à l'arrêt on le laisse simplement en attente
        // (hasPendingAudioChunk reste vrai) sans le consommer, prêt pour la
        // reprise — cf. VideoGLComponent qui, lui aussi, ne fait qu'afficher/
        // attendre sans "consommer" au sens propre tant que rien ne le
        // demande.
        if (!isPlayingNow)
            break;

        audioCatchingUp = false;
        audioSyncedGeneration = pendingAudioChunk.generation;

        const int availableInChunk = pendingAudioChunk.numFrames - pendingAudioChunkReadFrames;
        const int framesToCopy = juce::jmin(availableInChunk, numSamples - written);
        const float gain = muted.load(std::memory_order_relaxed) ? 0.0f : volume.load(std::memory_order_relaxed);

        for (int ch = 0; ch < outChannels; ++ch)
        {
            float* dest = buffer.getWritePointer(ch, written);
            const float* src = pendingAudioChunk.interleaved.data()
                              + static_cast<size_t>(pendingAudioChunkReadFrames) * AudioFrameQueue::numChannels
                              + static_cast<size_t>(ch);

            for (int i = 0; i < framesToCopy; ++i)
                dest[i] += src[static_cast<size_t>(i) * AudioFrameQueue::numChannels] * gain;
        }

        pendingAudioChunkReadFrames += framesToCopy;
        written += framesToCopy;
        hasSyncedAudioOnce = true;
        lastAudioPtsSeconds = chunkStartSeconds + static_cast<double>(framesToCopy) / sampleRate;

        if (pendingAudioChunkReadFrames >= pendingAudioChunk.numFrames)
            hasPendingAudioChunk = false;
    }
}

juce::AudioProcessorEditor* MUVideoPlayerProAudioProcessor::createEditor()
{
    return new MUVideoPlayerProAudioProcessorEditor(*this);
}

bool MUVideoPlayerProAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String MUVideoPlayerProAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MUVideoPlayerProAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MUVideoPlayerProAudioProcessor::producesMidi() const
{
    return true;
}

double MUVideoPlayerProAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MUVideoPlayerProAudioProcessor::getNumPrograms()
{
    return 1;
}

int MUVideoPlayerProAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MUVideoPlayerProAudioProcessor::setCurrentProgram(int)
{
}

const juce::String MUVideoPlayerProAudioProcessor::getProgramName(int)
{
    return {};
}

void MUVideoPlayerProAudioProcessor::changeProgramName(int, const juce::String&)
{
}

namespace
{
// MuseScore persiste déjà nativement l'état des plugins VST3 insérés dans le
// .mscz (§5 du cahier des charges) : ce XML est ce que getState/setState VST3
// écrit/relit dans le projet hôte, pas un mécanisme maison à part.
constexpr int stateVersion = 1;
}

void MUVideoPlayerProAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement root("MUVIDEOPLAYERPRO_STATE");
    root.setAttribute("version", stateVersion);
    root.setAttribute("videoPath", currentVideoFile.getFullPathName());
    root.setAttribute("offsetMs", static_cast<int>(std::lround(offsetSeconds * 1000.0)));
    root.setAttribute("volume", static_cast<double>(getVolume()));
    root.setAttribute("muted", isMuted());

    auto* hitPointsXml = root.createNewChildElement("HITPOINTS");
    for (const auto& hitPoint : hitPoints.items())
    {
        auto* hitPointXml = hitPointsXml->createNewChildElement("HITPOINT");
        hitPointXml->setAttribute("id", hitPoint.id);
        hitPointXml->setAttribute("label", hitPoint.label);
        hitPointXml->setAttribute("timeMs", hitPoint.timeMs);
        hitPointXml->setAttribute("colour", static_cast<int>(hitPoint.colour));
    }

    copyXmlToBinary(root, destData);
}

void MUVideoPlayerProAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> root(getXmlFromBinary(data, sizeInBytes));

    if (root == nullptr || !root->hasTagName("MUVIDEOPLAYERPRO_STATE"))
        return;

    offsetSeconds = root->getDoubleAttribute("offsetMs", 0.0) / 1000.0;
    setVolume(static_cast<float>(root->getDoubleAttribute("volume", 1.0)));
    setMuted(root->getBoolAttribute("muted", false));

    std::vector<HitPoint> loadedHitPoints;
    if (auto* hitPointsXml = root->getChildByName("HITPOINTS"))
    {
        for (auto* hitPointXml : hitPointsXml->getChildWithTagNameIterator("HITPOINT"))
        {
            HitPoint hitPoint;
            hitPoint.id = hitPointXml->getIntAttribute("id");
            hitPoint.label = hitPointXml->getStringAttribute("label");
            hitPoint.timeMs = hitPointXml->getIntAttribute("timeMs");
            hitPoint.colour = static_cast<uint32_t>(hitPointXml->getIntAttribute("colour", 0x3B94E5));
            loadedHitPoints.push_back(hitPoint);
        }
    }
    hitPoints.restoreFrom(std::move(loadedHitPoints));

    const juce::String videoPath = root->getStringAttribute("videoPath");
    if (videoPath.isNotEmpty())
    {
        const juce::File file(videoPath);
        if (file.existsAsFile())
            loadVideoFile(file);
    }
}

void MUVideoPlayerProAudioProcessor::requestPlay()
{
    pendingMmc.store(PendingMmc::Play, std::memory_order_release);
}

void MUVideoPlayerProAudioProcessor::requestPause()
{
    pendingMmc.store(PendingMmc::Pause, std::memory_order_release);
}

void MUVideoPlayerProAudioProcessor::requestStop()
{
    pendingMmc.store(PendingMmc::Stop, std::memory_order_release);
}

void MUVideoPlayerProAudioProcessor::requestLocate(double positionSeconds)
{
    pendingLocateSeconds.store(positionSeconds, std::memory_order_relaxed);
    pendingMmc.store(PendingMmc::Locate, std::memory_order_release);
}

bool MUVideoPlayerProAudioProcessor::loadVideoFile(const juce::File& file)
{
    videoDecoder.stopDecoding();
    videoFrameQueue.reset();
    audioFrameQueue.reset();

    if (!videoDecoder.loadFile(file, getSampleRate()))
        return false;

    currentVideoFile = file;
    recentFiles.add(file.getFullPathName());

    // Consommé par mixVideoAudio() sur le thread audio, au prochain
    // processBlock() : purge le bloc audio éventuellement encore en attente
    // de l'ancienne vidéo (même mécanisme que VideoGLComponent::
    // notifySourceReloaded côté rendu).
    audioReloadRequested.store(true, std::memory_order_release);

    // Demande un seek vers la position courante de MuseScore avant même de
    // démarrer le décodage : comme le thread n'existe pas encore (startThread()
    // n'a pas été appelé), decodeLoop() consommera cette demande dès son tout
    // premier passage, avant de lire le moindre paquet depuis le début du
    // fichier. Sans ça, si le score est arrêté loin de zéro, l'écran
    // afficherait d'abord la frame 0 (et un rattrapage séquentiel visible)
    // avant qu'un seek correctif n'intervienne côté rendu (SeekPolicy).
    videoDecoder.requestSeek(positionCache.positionSeconds());

    videoDecoder.startDecoding();
    return true;
}

void MUVideoPlayerProAudioProcessor::clearVideoFile()
{
    videoDecoder.unloadFile();
    videoFrameQueue.reset();
    audioFrameQueue.reset();
    currentVideoFile = juce::File();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MUVideoPlayerProAudioProcessor();
}
