juce_add_console_app(MMCEncoderTests
    PRODUCT_NAME "MMCEncoderTests"
)

target_sources(MMCEncoderTests
    PRIVATE
        MMCEncoderTests.cpp
        ../Source/Transport/MMCEncoder.cpp
)

target_compile_definitions(MMCEncoderTests
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(MMCEncoderTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME MMCEncoderTests COMMAND MMCEncoderTests)

# --- HitPointListTests ---------------------------------------------------

juce_add_console_app(HitPointListTests
    PRODUCT_NAME "HitPointListTests"
)

target_sources(HitPointListTests
    PRIVATE
        HitPointListTests.cpp
)

target_compile_definitions(HitPointListTests
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(HitPointListTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME HitPointListTests COMMAND HitPointListTests)

# --- VideoTimecodeTests --------------------------------------------------

juce_add_console_app(VideoTimecodeTests
    PRODUCT_NAME "VideoTimecodeTests"
)

target_sources(VideoTimecodeTests
    PRIVATE
        VideoTimecodeTests.cpp
)

target_compile_definitions(VideoTimecodeTests
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(VideoTimecodeTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME VideoTimecodeTests COMMAND VideoTimecodeTests)

# --- RecentFilesStoreTests -------------------------------------------------

juce_add_console_app(RecentFilesStoreTests
    PRODUCT_NAME "RecentFilesStoreTests"
)

target_sources(RecentFilesStoreTests
    PRIVATE
        RecentFilesStoreTests.cpp
        ../Source/Transport/RecentFilesStore.cpp
)

target_compile_definitions(RecentFilesStoreTests
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(RecentFilesStoreTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME RecentFilesStoreTests COMMAND RecentFilesStoreTests)

# --- VideoDecoderTests -------------------------------------------------

find_program(FFMPEG_EXECUTABLE ffmpeg)

if(FFMPEG_EXECUTABLE)
    set(VIDEO_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set(VIDEO_FIXTURE_MP4 "${VIDEO_FIXTURE_DIR}/test_pattern.mp4")

    # Clip H.264 synthétique de 2s @ 25fps + piste audio sinusoïdale 440Hz
    # @ 22050Hz mono (volontairement différent du sample rate cible des tests,
    # pour vérifier que le resampling a bien lieu), généré une fois par le
    # build (pas commité : reproductible localement via ffmpeg, déjà dispo en
    # dev).
    add_custom_command(
        OUTPUT "${VIDEO_FIXTURE_MP4}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VIDEO_FIXTURE_DIR}"
        COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi -i "testsrc2=duration=2:size=320x240:rate=25"
                -f lavfi -i "sine=frequency=440:duration=2:sample_rate=22050"
                -c:v libx264 -pix_fmt yuv420p -c:a aac "${VIDEO_FIXTURE_MP4}"
        COMMENT "Generating VideoDecoderTests fixture (test_pattern.mp4)"
        VERBATIM
    )
    add_custom_target(VideoDecoderTestsFixture DEPENDS "${VIDEO_FIXTURE_MP4}")

    juce_add_console_app(VideoDecoderTests
        PRODUCT_NAME "VideoDecoderTests"
    )

    target_sources(VideoDecoderTests
        PRIVATE
            VideoDecoderTests.cpp
            ../Source/Video/VideoDecoder.cpp
    )

    target_compile_definitions(VideoDecoderTests
        PRIVATE
            JUCE_STANDALONE_APPLICATION=1
            TEST_FIXTURE_MP4_PATH="${VIDEO_FIXTURE_MP4}"
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )

    target_link_libraries(VideoDecoderTests
        PRIVATE
            juce::juce_core
            PkgConfig::FFMPEG
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_dependencies(VideoDecoderTests VideoDecoderTestsFixture)

    add_test(NAME VideoDecoderTests COMMAND VideoDecoderTests)
else()
    message(WARNING "ffmpeg executable not found: skipping VideoDecoderTests (fixture generation needs it)")
endif()
