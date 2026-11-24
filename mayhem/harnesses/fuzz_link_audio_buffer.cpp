// Fuzzes the payload of a LinkAudio kAudioBuffer datagram as delivered to
// MainProcessor::receiveAudioBuffer (the production ChannelsMessageHandler path):
// AudioBuffer::fromNetworkByteStream validation (chunks, tempo, codec, sample
// rate, channel count, byte count), then SourceProcessor PCM decoding, queueing
// and the Source callback for the subscribed channel. Setup mirrors
// src/ableton/link_audio/tst_MainProcessor.cpp.
//
// MainProcessor keeps a function-static AudioBuffer (upstream code); every field
// is overwritten by fromNetworkByteStream before use, or it throws.
#include "LinkAudioSource.hpp"

#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  fuzz::AudioSink sink;
  // UdpMessenger::receiveAudioBuffer (the only caller) catches std::runtime_error.
  try
  {
    sink.processor.receiveAudioBuffer(data, data + size);
  }
  catch (const std::runtime_error&)
  {
  }
  sink.fixture.flush();
  sink.fixture.advanceTime(std::chrono::milliseconds(20));
  return 0;
}
