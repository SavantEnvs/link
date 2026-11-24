// A MainProcessor with one registered Source (fixed channel id), driven by the
// deterministic serial_io fixture. The Source callback does what the public
// LinkAudioSource callback (include/ableton/LinkAudio.ipp) does with a received
// buffer: read every sample and map the buffer onto session beat time
// (numFrames / sampleRate, tempo / 60).
#pragma once

#include <ableton/link/NodeId.hpp>
#include <ableton/link_audio/AudioBuffer.hpp>
#include <ableton/link_audio/MainProcessor.hpp>
#include <ableton/link_audio/Source.hpp>
#include <ableton/platforms/stl/Random.hpp>
#include <ableton/test/serial_io/Fixture.hpp>

#include <functional>
#include <optional>

namespace fuzz
{
using ableton::link::NodeIdArray;
// Fixed ids; seeds carry them. kSourceId is the channel the local Source subscribes to.
constexpr NodeIdArray kLocalId{{'l', 'o', 'c', 'a', 'l', 'i', 'd', '0'}};
constexpr NodeIdArray kPeerId{{'p', 'e', 'e', 'r', 'i', 'd', '0', '1'}};
constexpr NodeIdArray kSessionId{{'s', 'e', 's', 's', 'i', 'o', 'n', '1'}};
constexpr NodeIdArray kSourceId{{'s', 'o', 'u', 'r', 'c', 'e', 'i', 'd'}};

struct GetSender
{
  using SendHandler = std::function<void(const uint8_t*, size_t)>;
  std::optional<SendHandler> forChannel(const ableton::link_audio::Id&)
  {
    return SendHandler{[](const uint8_t*, size_t) {}};
  }
};

struct GetNodeId
{
  const ableton::link::NodeId& operator()() const { return nodeId; }
  ableton::link::NodeId nodeId{kLocalId};
};

struct Noop
{
  void operator()() {}
};

struct AudioSink
{
  using Processor = ableton::link_audio::MainProcessor<Noop&,
                                                       GetSender,
                                                       GetNodeId,
                                                       ableton::platforms::stl::Random,
                                                       ableton::test::serial_io::Context>;
  AudioSink()
    : processor(ableton::util::injectVal(fixture.makeIoContext()),
                ableton::util::injectRef(noop))
  {
    using namespace ableton::link_audio;
    pSource = std::make_shared<Source>(
      ableton::link::NodeId{kSourceId},
      [this](BufferCallbackHandle<Buffer<int16_t>> h)
      {
        const auto& b = h.mBuffer;
        const uint64_t n = uint64_t(b.mNumFrames) * b.mNumChannels;
        uint64_t sum = 0;
        for (uint64_t i = 0; i < n; ++i)
          sum += static_cast<uint64_t>(h.mpSamples[i]);
        const auto durationSeconds =
          static_cast<double>(b.mNumFrames) / static_cast<double>(b.mSampleRate);
        const auto endBeats =
          b.mBeginBeats.floating() + durationSeconds * (b.mTempo.bpm() / 60.0);
        // Same conversion the API does when it hands beats back as micro-beats.
        const auto micro = ableton::link::Beats{endBeats}.microBeats();
        checksum += sum + static_cast<uint64_t>(micro) + b.mCount;
        ++numCallbacks;
      });
    processor.addSource(pSource,
                        ableton::util::injectVal(GetSender{}),
                        ableton::util::injectVal(GetNodeId{}));
    fixture.flush();
  }

  ableton::test::serial_io::Fixture fixture;
  Noop noop;
  Processor processor;
  std::shared_ptr<ableton::link_audio::Source> pSource;
  uint64_t checksum = 0;
  size_t numCallbacks = 0;
};
} // namespace fuzz
