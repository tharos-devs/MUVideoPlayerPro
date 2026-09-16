#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/Transport/MMCEncoder.h"

namespace
{
// Réimplémentation de MMCDecoder::locateToSeconds (MuseScore,
// ~/MuseScore/muse/framework/midiremote/internal/mmcdecoder.cpp), appliquée
// aux 6 octets de data produits par MMCEncoder::locate() (variante "sans
// format byte" : [TARGET, hr_byte, min, sec, frame, subframe]), pour
// vérifier le round-trip sans dépendre du code MuseScore lui-même.
double decodeLocateSeconds(const std::vector<uint8_t>& data)
{
    jassert(data.size() == 6);

    const uint8_t hrByte = data[1];
    const uint8_t minutes = data[2];
    const uint8_t seconds = data[3];
    const uint8_t frame = data[4];
    const uint8_t subframe = data[5];

    const uint8_t fpsCode = (hrByte >> 5) & 0x03;
    const uint8_t hours = hrByte & 0x1F;

    double fps = 25.0;
    switch (fpsCode)
    {
        case 0: fps = 24.0; break;
        case 1: fps = 25.0; break;
        case 2: fps = 29.97; break;
        case 3: fps = 30.0; break;
    }

    return hours * 3600.0 + minutes * 60.0 + seconds + (frame / fps) + (subframe / (fps * 100.0));
}

std::vector<uint8_t> rawBytes(const juce::MidiMessage& msg)
{
    return { msg.getRawData(), msg.getRawData() + msg.getRawDataSize() };
}
}

class MMCEncoderTests final : public juce::UnitTest
{
public:
    MMCEncoderTests() : juce::UnitTest("MMCEncoder", "Transport") {}

    void runTest() override
    {
        beginTest("Play/Pause/Stop match F0 7F <dev> 06 <cmd> F7");
        {
            expectEquals((int) rawBytes(MMCEncoder::play()).size(), 6);
            expect(rawBytes(MMCEncoder::play())
                       == std::vector<uint8_t> { 0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7 });
            expect(rawBytes(MMCEncoder::pause())
                       == std::vector<uint8_t> { 0xF0, 0x7F, 0x7F, 0x06, 0x09, 0xF7 });
            expect(rawBytes(MMCEncoder::stop())
                       == std::vector<uint8_t> { 0xF0, 0x7F, 0x7F, 0x06, 0x01, 0xF7 });
        }

        beginTest("Play with custom deviceId");
        {
            expect(rawBytes(MMCEncoder::play(0x01))
                       == std::vector<uint8_t> { 0xF0, 0x7F, 0x01, 0x06, 0x02, 0xF7 });
        }

        beginTest("Locate byte layout: F0 7F <dev> 06 44 06 <hr> <mn> <sc> <fr> <sf> F7");
        {
            const auto msg = MMCEncoder::locate(0.0);
            const auto bytes = rawBytes(msg);

            expectEquals((int) bytes.size(), 12);
            expectEquals((int) bytes[0], 0xF0);
            expectEquals((int) bytes[1], 0x7F);
            expectEquals((int) bytes[2], 0x7F);
            expectEquals((int) bytes[3], 0x06);
            expectEquals((int) bytes[4], 0x44);
            expectEquals((int) bytes[5], 0x06); // TARGET
            expectEquals((int) bytes[11], 0xF7);
        }

        beginTest("Locate round-trip (decoded with MuseScore's exact formula)");
        {
            for (const double seconds : { 0.0, 1.5, 15.04, 61.2, 3661.5, 5999.96 })
            {
                const auto msg = MMCEncoder::locate(seconds);
                const auto bytes = rawBytes(msg);

                // data = command byte (0x44) + les 6 octets de payload, jusqu'avant F7
                const std::vector<uint8_t> data(bytes.begin() + 4, bytes.end() - 1);
                expectEquals((int) data.size(), 7);
                expectEquals((int) data[0], 0x44); // command byte

                const std::vector<uint8_t> locateData(data.begin() + 1, data.end());
                const double decoded = decodeLocateSeconds(locateData);

                expectWithinAbsoluteError(decoded, seconds, 1.0 / 25.0 /* 1 frame @ 25fps */);
            }
        }
    }
};

static MMCEncoderTests mmcEncoderTests;

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
