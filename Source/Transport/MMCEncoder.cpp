#include "MMCEncoder.h"

namespace
{
constexpr uint8_t kMmcSubId1 = 0x7F;
constexpr uint8_t kMmcSubId2 = 0x06;

enum class Command : uint8_t
{
    Stop = 0x01,
    Play = 0x02,
    Pause = 0x09,
    Locate = 0x44,
};

juce::MidiMessage makeSysEx(uint8_t deviceId, Command command, const std::vector<uint8_t>& data = {})
{
    std::vector<uint8_t> bytes;
    bytes.reserve(3 + data.size());
    bytes.push_back(kMmcSubId1);
    bytes.push_back(deviceId);
    bytes.push_back(kMmcSubId2);
    bytes.push_back(static_cast<uint8_t>(command));
    bytes.insert(bytes.end(), data.begin(), data.end());

    return juce::MidiMessage::createSysExMessage(bytes.data(), static_cast<int>(bytes.size()));
}
}

namespace MMCEncoder
{
juce::MidiMessage play(uint8_t deviceId)
{
    return makeSysEx(deviceId, Command::Play);
}

juce::MidiMessage pause(uint8_t deviceId)
{
    return makeSysEx(deviceId, Command::Pause);
}

juce::MidiMessage stop(uint8_t deviceId)
{
    return makeSysEx(deviceId, Command::Stop);
}

juce::MidiMessage locate(double positionSeconds, uint8_t deviceId)
{
    constexpr double fps = 25.0;
    constexpr uint8_t fpsCode = 1; // 0=24, 1=25, 2=29.97, 3=30

    positionSeconds = juce::jmax(0.0, positionSeconds);

    const int hours = static_cast<int>(positionSeconds / 3600.0) & 0x1F;
    double remaining = positionSeconds - static_cast<double>(hours) * 3600.0;

    const int minutes = static_cast<int>(remaining / 60.0);
    remaining -= static_cast<double>(minutes) * 60.0;

    const int seconds = static_cast<int>(remaining);
    remaining -= static_cast<double>(seconds);

    const double frameFloat = remaining * fps;
    const int frame = static_cast<int>(frameFloat);
    const int subframe = static_cast<int>((frameFloat - static_cast<double>(frame)) * 100.0);

    const uint8_t hrByte = static_cast<uint8_t>((fpsCode << 5) | (hours & 0x1F));

    const std::vector<uint8_t> data {
        0x06, // TARGET
        hrByte,
        static_cast<uint8_t>(minutes),
        static_cast<uint8_t>(seconds),
        static_cast<uint8_t>(frame),
        static_cast<uint8_t>(subframe),
    };

    return makeSysEx(deviceId, Command::Locate, data);
}
}
