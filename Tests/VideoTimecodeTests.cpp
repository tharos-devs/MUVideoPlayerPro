#include <juce_core/juce_core.h>
#include "../Source/Transport/VideoTimecode.h"

class VideoTimecodeTests final : public juce::UnitTest
{
public:
    VideoTimecodeTests() : juce::UnitTest("VideoTimecode", "Transport") {}

    void runTest() override
    {
        beginTest("Zero and frame-boundary values at 25fps");
        {
            expectEquals(VideoTimecode::format(0.0, 25.0), juce::String("00:00:00:00"));
            expectEquals(VideoTimecode::format(0.04, 25.0), juce::String("00:00:00:01")); // 1 frame @ 25fps
            expectEquals(VideoTimecode::format(1.0, 25.0), juce::String("00:00:01:00"));
            expectEquals(VideoTimecode::format(61.0, 25.0), juce::String("00:01:01:00"));
            expectEquals(VideoTimecode::format(3661.0, 25.0), juce::String("01:01:01:00"));
        }

        beginTest("Frame rollover at 30fps");
        {
            expectEquals(VideoTimecode::format(29.0 / 30.0, 30.0), juce::String("00:00:00:29"));
            expectEquals(VideoTimecode::format(1.0, 30.0), juce::String("00:00:01:00"));
        }

        beginTest("Negative positions clamp to zero, never produce negative fields");
        {
            expectEquals(VideoTimecode::format(-5.0, 25.0), juce::String("00:00:00:00"));
        }

        beginTest("Out-of-range frame rate clamps to [1, 240] instead of dividing by zero");
        {
            // fps <= 0 clampé à 1 -- comportement identique à un fps de 1.
            expectEquals(VideoTimecode::format(1.0, 0.0), juce::String("00:00:01:00"));
            expectEquals(VideoTimecode::format(1.0, -10.0), juce::String("00:00:01:00"));
            // fps > 240 clampé à 240 -- toujours un timecode bien formé.
            expectEquals(VideoTimecode::format(1.0, 10000.0), VideoTimecode::format(1.0, 240.0));
        }

        beginTest("parseToMs round-trips with format() at frame boundaries");
        {
            expectEquals(VideoTimecode::parseToMs("00:00:00:00", 25.0), 0);
            expectEquals(VideoTimecode::parseToMs("00:00:01:00", 25.0), 1000);
            expectEquals(VideoTimecode::parseToMs("01:02:03:04", 25.0), (1 * 3600 + 2 * 60 + 3) * 1000 + (4 * 1000) / 25);
            expectEquals(VideoTimecode::parseToMs(VideoTimecode::format(12.34, 25.0), 25.0),
                         VideoTimecode::parseToMs(VideoTimecode::format(12.34, 25.0), 25.0));
        }

        beginTest("parseToMs rejects malformed or out-of-range timecodes");
        {
            expectEquals(VideoTimecode::parseToMs("not a timecode", 25.0), -1);
            expectEquals(VideoTimecode::parseToMs("00:00:00", 25.0), -1);      // pas assez de champs
            expectEquals(VideoTimecode::parseToMs("00:60:00:00", 25.0), -1);   // minutes > 59
            expectEquals(VideoTimecode::parseToMs("00:00:60:00", 25.0), -1);   // secondes > 59
            expectEquals(VideoTimecode::parseToMs("00:00:00:25", 25.0), -1);   // frame >= débit (25fps: 0..24)
            expectEquals(VideoTimecode::parseToMs("00:00:00:-1", 25.0), -1);   // négatif
            expectEquals(VideoTimecode::parseToMs("", 25.0), -1);
        }
    }
};

static VideoTimecodeTests videoTimecodeTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult(i);
        if (result->failures > 0)
            return 1;
    }

    return 0;
}
