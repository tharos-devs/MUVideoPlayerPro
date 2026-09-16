#include "VideoDecoder.h"

VideoDecoder::VideoDecoder(VideoFrameQueue& outputQueueIn, AudioFrameQueue& audioOutputQueueIn)
    : juce::Thread("VideoDecoder"), outputQueue(outputQueueIn), audioOutputQueue(audioOutputQueueIn)
{
}

VideoDecoder::~VideoDecoder()
{
    stopDecoding();
    closeInput();
}

bool VideoDecoder::loadFile(const juce::File& file, double targetSampleRateIn)
{
    closeInput();

    targetSampleRate = targetSampleRateIn > 0.0 ? targetSampleRateIn : 44100.0;

    // Le thread de décodage est garanti à l'arrêt à ce stade (loadFile()
    // n'est appelé qu'après stopDecoding() par PluginProcessor::loadVideoFile) :
    // une éventuelle demande de seek du fichier précédent, jamais consommée,
    // ne doit pas s'appliquer au nouveau fichier.
    seekRequested.store(false, std::memory_order_relaxed);

    // Nouvelle génération à chaque fichier chargé, comme pour un seek : permet
    // à VideoGLComponent de repérer et jeter tout ce qui restait en attente
    // (frame parquée, backlog de queue) de l'ancienne vidéo plutôt que de
    // rester bloqué dessus indéfiniment (même mécanisme que le seek §7).
    ++currentGeneration;

    const std::string path = file.getFullPathName().toStdString();

    if (avformat_open_input(&formatContext, path.c_str(), nullptr, nullptr) != 0)
        return false;

    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        closeInput();
        return false;
    }

    const AVCodec* codec = nullptr;
    videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);

    if (videoStreamIndex < 0 || codec == nullptr)
    {
        closeInput();
        return false;
    }

    AVStream* stream = formatContext->streams[videoStreamIndex];
    streamTimeBase = stream->time_base;

    codecContext = avcodec_alloc_context3(codec);
    if (codecContext == nullptr || avcodec_parameters_to_context(codecContext, stream->codecpar) < 0)
    {
        closeInput();
        return false;
    }

    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        closeInput();
        return false;
    }

    width = codecContext->width;
    height = codecContext->height;

    if (formatContext->duration > 0)
        durationSeconds = static_cast<double>(formatContext->duration) / AV_TIME_BASE;
    else if (stream->duration > 0)
        durationSeconds = static_cast<double>(stream->duration) * av_q2d(streamTimeBase);

    // avg_frame_rate est généralement le bon débit nominal pour l'affichage
    // (ex : 23.976 arrondi à 24 pour un timecode non-drop-frame) ; r_frame_rate
    // (le "plus petit dénominateur commun" du flux) sert de repli si le
    // conteneur ne renseigne pas avg_frame_rate (num/den à 0).
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0)
        frameRate = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0)
        frameRate = av_q2d(stream->r_frame_rate);

    // Conservée à part : setFrameRate() (onglet Settings) peut diverger de
    // cette valeur, et "Detect" doit pouvoir y revenir sans rouvrir le fichier.
    autoDetectedFrameRate = frameRate;

    videoCodecName = juce::String(avcodec_get_name(codecContext->codec_id)).toUpperCase();

    swsContext = sws_getContext(width, height, codecContext->pix_fmt,
                                 width, height, AV_PIX_FMT_RGBA,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (swsContext == nullptr)
    {
        closeInput();
        return false;
    }

    decodedFrame = av_frame_alloc();
    rgbaFrame = av_frame_alloc();
    packet = av_packet_alloc();

    if (decodedFrame == nullptr || rgbaFrame == nullptr || packet == nullptr)
    {
        closeInput();
        return false;
    }

    rgbaFrame->format = AV_PIX_FMT_RGBA;
    rgbaFrame->width = width;
    rgbaFrame->height = height;

    // align=1 : pas de padding de ligne, rgbaFrame->linesize[0] == width*4
    // exactement, ce qui permet de pousser data[0] directement dans la
    // queue sans repasser par un buffer intermédiaire compacté.
    if (av_frame_get_buffer(rgbaFrame, 1) < 0)
    {
        closeInput();
        return false;
    }

    // Piste audio : absente ou en erreur d'ouverture n'est pas fatal pour le
    // fichier (audioStreamIndex reste -1, mixage silencieux) — seule la
    // vidéo est requise.
    const AVCodec* audioCodec = nullptr;
    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);

    if (audioStreamIndex >= 0 && audioCodec != nullptr)
    {
        AVStream* audioStream = formatContext->streams[audioStreamIndex];
        audioStreamTimeBase = audioStream->time_base;

        audioCodecContext = avcodec_alloc_context3(audioCodec);

        const bool audioCodecReady = audioCodecContext != nullptr
            && avcodec_parameters_to_context(audioCodecContext, audioStream->codecpar) >= 0
            && avcodec_open2(audioCodecContext, audioCodec, nullptr) >= 0;

        if (audioCodecReady)
        {
            AVChannelLayout outLayout;
            av_channel_layout_default(&outLayout, AudioFrameQueue::numChannels);

            SwrContext* newSwr = nullptr;
            const int swrSetupResult = swr_alloc_set_opts2(&newSwr,
                &outLayout, AV_SAMPLE_FMT_FLT, static_cast<int>(targetSampleRate),
                &audioCodecContext->ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate,
                0, nullptr);

            if (swrSetupResult == 0 && newSwr != nullptr && swr_init(newSwr) >= 0)
            {
                swrContext = newSwr;
                decodedAudioFrame = av_frame_alloc();

                if (decodedAudioFrame == nullptr)
                {
                    swr_free(&swrContext);
                    avcodec_free_context(&audioCodecContext);
                    audioStreamIndex = -1;
                }
            }
            else
            {
                if (newSwr != nullptr)
                    swr_free(&newSwr);

                avcodec_free_context(&audioCodecContext);
                audioStreamIndex = -1;
            }

            av_channel_layout_uninit(&outLayout);
        }
        else
        {
            if (audioCodecContext != nullptr)
                avcodec_free_context(&audioCodecContext);

            audioStreamIndex = -1;
        }
    }

    // audioStreamIndex n'est définitivement >= 0 qu'une fois toute la
    // chaîne (codec + resampler) prête -- cf. les remises à -1 ci-dessus en
    // cas d'échec partiel, que ce nom ne doit pas survivre.
    if (audioStreamIndex >= 0 && audioCodec != nullptr)
        audioCodecName = juce::String(avcodec_get_name(audioCodec->id)).toUpperCase();

    return true;
}

void VideoDecoder::startDecoding()
{
    startThread();
}

void VideoDecoder::stopDecoding()
{
    stopThread(2000);
}

void VideoDecoder::unloadFile()
{
    stopDecoding();
    closeInput();
}

void VideoDecoder::requestSeek(double seconds) noexcept
{
    pendingSeekSeconds.store(seconds, std::memory_order_relaxed);
    seekRequested.store(true, std::memory_order_release);
    seekRequestSequence.fetch_add(1, std::memory_order_acq_rel);
}

void VideoDecoder::performSeek(double targetSeconds)
{
    if (formatContext == nullptr || codecContext == nullptr || videoStreamIndex < 0)
        return;

    const double upperBound = durationSeconds > 0.0 ? durationSeconds : targetSeconds;
    const double clamped = juce::jlimit(0.0, upperBound, targetSeconds);
    const int64_t targetTimestamp = static_cast<int64_t>(clamped / av_q2d(streamTimeBase));

    av_seek_frame(formatContext, videoStreamIndex, targetTimestamp, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecContext);

    if (audioCodecContext != nullptr)
        avcodec_flush_buffers(audioCodecContext);

    // avcodec_flush_buffers() ne remet à zéro que l'état du décodeur : le
    // filtre de resampling de swrContext garde son propre état interne
    // (ligne à retard, cf. swr_get_delay()) entre deux appels à swr_convert().
    // Sans le réinitialiser aussi, les tout premiers échantillons resamplés
    // après un seek peuvent encore contenir des restes du flux d'AVANT le
    // seek, mélangés aux nouveaux — l'artefact audible constaté juste après
    // un seek, avec un chunkStartSeconds qui ne correspond alors plus
    // vraiment au contenu réellement produit. swr_close()+swr_init() sur le
    // même contexte réutilise sa configuration mais vide cet état interne.
    if (swrContext != nullptr)
    {
        swr_close(swrContext);
        swr_init(swrContext);
    }

    ++currentGeneration;
}

void VideoDecoder::run()
{
    // decodeLoop() rend la main dès qu'un seek est demandé (traité comme un
    // "stop" pour son shouldStop() interne) ou dès l'EOF naturelle du
    // fichier. Dans les deux cas, le thread reste vivant : on attend soit un
    // seek (rejoué en relançant decodeLoop(), qui l'applique en tout début),
    // soit l'arrêt effectif du thread. Sans ça, un fichier déjà lu jusqu'au
    // bout ne pourrait plus jamais reprendre (le thread serait terminé).
    while (!threadShouldExit())
    {
        decodeLoop([this] { return threadShouldExit() || seekRequested.load(std::memory_order_acquire); });

        while (!threadShouldExit() && !seekRequested.load(std::memory_order_acquire))
            juce::Thread::sleep(5);
    }
}

void VideoDecoder::decodeLoop(const std::function<bool()>& shouldStop)
{
    if (formatContext == nullptr || codecContext == nullptr)
        return;

    if (seekRequested.exchange(false, std::memory_order_acq_rel))
        performSeek(pendingSeekSeconds.load(std::memory_order_relaxed));

    while (!shouldStop())
    {
        const int readResult = av_read_frame(formatContext, packet);

        if (readResult < 0)
            break; // EOF ou erreur : fin de la boucle de décodage.

        if (packet->stream_index == videoStreamIndex)
        {
            if (avcodec_send_packet(codecContext, packet) == 0)
            {
                while (!shouldStop())
                {
                    const int receiveResult = avcodec_receive_frame(codecContext, decodedFrame);

                    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                        break;

                    if (receiveResult < 0)
                        break;

                    sws_scale(swsContext, decodedFrame->data, decodedFrame->linesize, 0, height,
                              rgbaFrame->data, rgbaFrame->linesize);

                    const double ptsSeconds = decodedFrame->pts != AV_NOPTS_VALUE
                                                   ? static_cast<double>(decodedFrame->pts) * av_q2d(streamTimeBase)
                                                   : 0.0;

                    jassert(rgbaFrame->linesize[0] == width * 4);

                    // Attend qu'une place se libère dans la queue plutôt que
                    // de continuer à décoder le reste du fichier sans frein :
                    // c'est le vrai mécanisme de régulation entre décodage et
                    // consommation (pumpFromQueue ne consomme qu'une frame
                    // au-delà de la position désirée, donc la queue reste
                    // sciemment pleine tant que le transport est à l'arrêt —
                    // c'est l'état attendu, pas une erreur à contourner).
                    // shouldStop() reste vérifié à chaque itération : un
                    // nouveau seek interrompt toujours l'attente immédiatement
                    // (le vrai bug qui bloquait tout indéfiniment était
                    // ailleurs — seekPending relâché trop tôt côté
                    // VideoGLComponent — voir son commentaire).
                    while (!outputQueue.push(ptsSeconds, width, height, rgbaFrame->data[0], currentGeneration))
                    {
                        if (shouldStop())
                            break;

                        juce::Thread::sleep(1);
                    }

                    av_frame_unref(decodedFrame);
                }
            }
        }
        else if (packet->stream_index == audioStreamIndex && audioCodecContext != nullptr)
        {
            if (avcodec_send_packet(audioCodecContext, packet) == 0)
            {
                while (!shouldStop())
                {
                    const int receiveResult = avcodec_receive_frame(audioCodecContext, decodedAudioFrame);

                    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                        break;

                    if (receiveResult < 0)
                        break;

                    const double ptsSeconds = decodedAudioFrame->pts != AV_NOPTS_VALUE
                                                   ? static_cast<double>(decodedAudioFrame->pts) * av_q2d(audioStreamTimeBase)
                                                   : 0.0;

                    // Marge de sécurité (+1) : swr_convert peut renvoyer un
                    // échantillon de plus que l'estimation à cause des
                    // arrondis de resampling.
                    const int maxOutFrames = 1 + static_cast<int>(av_rescale_rnd(
                        swr_get_delay(swrContext, decodedAudioFrame->sample_rate) + decodedAudioFrame->nb_samples,
                        static_cast<int64_t>(targetSampleRate), decodedAudioFrame->sample_rate, AV_ROUND_UP));

                    if (maxOutFrames > 0)
                    {
                        const size_t neededSamples = static_cast<size_t>(maxOutFrames) * static_cast<size_t>(AudioFrameQueue::numChannels);

                        if (resampleScratch.size() < neededSamples)
                            resampleScratch.resize(neededSamples);

                        uint8_t* outPtrs[1] = { reinterpret_cast<uint8_t*>(resampleScratch.data()) };

                        const int convertedFrames = swr_convert(swrContext, outPtrs, maxOutFrames,
                                                                  const_cast<const uint8_t**>(decodedAudioFrame->data),
                                                                  decodedAudioFrame->nb_samples);

                        // N'attend PAS ici, contrairement à la file vidéo :
                        // mixVideoAudio() ne consomme la file audio QUE
                        // pendant que le transport joue (silence à l'arrêt,
                        // volontairement — cf. son commentaire), donc cette
                        // file peut rester pleine indéfiniment tant que
                        // MuseScore reste à l'arrêt. Si on bloquait ici comme
                        // pour la vidéo, ce même thread de décodage (boucle de
                        // lecture séquentielle partagée) resterait bloqué sur
                        // l'audio pour toujours et ne traiterait plus jamais
                        // aucun paquet vidéo, même avec de la place libre dans
                        // sa propre file — constaté : plus aucune image ne se
                        // met à jour après le premier seek, indéfiniment. La
                        // vidéo (bloquante ci-dessus) reste le seul régulateur
                        // de rythme du thread de décodage ; l'audio est donc
                        // sacrifiable ici : un bloc audio perdu pendant l'arrêt
                        // n'a aucune conséquence, rien ne le jouait de toute
                        // façon.
                        if (convertedFrames > 0)
                            audioOutputQueue.push(ptsSeconds, convertedFrames, resampleScratch.data(), currentGeneration);
                    }

                    av_frame_unref(decodedAudioFrame);
                }
            }
        }

        av_packet_unref(packet);
    }
}

void VideoDecoder::closeInput()
{
    if (packet != nullptr)
        av_packet_free(&packet);

    if (rgbaFrame != nullptr)
        av_frame_free(&rgbaFrame);

    if (decodedFrame != nullptr)
        av_frame_free(&decodedFrame);

    if (swsContext != nullptr)
    {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

    if (codecContext != nullptr)
        avcodec_free_context(&codecContext);

    if (decodedAudioFrame != nullptr)
        av_frame_free(&decodedAudioFrame);

    if (swrContext != nullptr)
        swr_free(&swrContext);

    if (audioCodecContext != nullptr)
        avcodec_free_context(&audioCodecContext);

    if (formatContext != nullptr)
        avformat_close_input(&formatContext);

    videoStreamIndex = -1;
    width = 0;
    height = 0;
    durationSeconds = 0.0;
    frameRate = 24.0;
    autoDetectedFrameRate = 24.0;
    videoCodecName.clear();
    audioCodecName.clear();
    audioStreamIndex = -1;
}
