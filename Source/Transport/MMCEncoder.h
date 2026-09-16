#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Construit les messages MIDI SysEx MMC (Play/Pause/Stop/Locate) attendus par
// le décodeur MuseScore (muse::midiremote::MMCDecoder,
// ~/MuseScore/muse/framework/midiremote/internal/mmcdecoder.cpp).
//
// Format : F0 7F <deviceId> 06 <command> [data...] F7
//   Stop=0x01, Play=0x02, Pause=0x09
//   Locate=0x44, data = [0x06 (TARGET), hr_byte, min, sec, frame, subframe]
//     hr_byte = (fpsCode << 5) | (hours & 0x1F), fpsCode 25fps = 1
//
// Vecteurs d'octets croisés avec
// ~/MuseScore/muse/framework/midiremote/tests/mmcdecoder_tests.cpp.
namespace MMCEncoder
{
constexpr uint8_t defaultDeviceId = 0x7F;

juce::MidiMessage play(uint8_t deviceId = defaultDeviceId);
juce::MidiMessage pause(uint8_t deviceId = defaultDeviceId);
juce::MidiMessage stop(uint8_t deviceId = defaultDeviceId);

// positionSeconds doit être >= 0. Encode en SMPTE 25fps.
juce::MidiMessage locate(double positionSeconds, uint8_t deviceId = defaultDeviceId);
}
