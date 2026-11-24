// Fuzzes the send side of LinkAudio peer announcements: the local peer name and
// channel (sink) names, as set through the public API (BasicLinkAudio::setPeerName,
// LinkAudioSink / LinkAudioSink::setName), are serialized by link_audio::UdpMessenger
// into fixed-size v1::MessageBuffer datagrams and split across several messages when
// they do not fit (PeerAnnouncement / PeerInfo / ChannelAnnouncements encoding).
//
// Names are truncated to v1::kMaxNameSize exactly as the public API does before they
// reach the messenger (include/ableton/LinkAudio.ipp). Oracle: every announcement we
// send must decode back (PeerAnnouncement::fromPayload) to the same peer name and the
// same channels, in order — a mismatch aborts.
#include "LinkAudioSource.hpp"

#include <ableton/discovery/test/Interface.hpp>
#include <ableton/link/PayloadEntries.hpp>
#include <ableton/link_audio/ChannelAnnouncements.hpp>
#include <ableton/link_audio/PeerAnnouncement.hpp>
#include <ableton/link_audio/UdpMessenger.hpp>
#include <ableton/link_audio/v1/Messages.hpp>

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdio>
#include <cstdlib>

using namespace ableton;
using namespace ableton::link_audio;

namespace
{
using Context = ableton::test::serial_io::Context;
struct Handler;
struct Observer;
using FuzzMessenger = UdpMessenger<Handler&, discovery::test::Interface, Observer&, Context>;

struct Observer
{
  using GatewayObserverAnnouncement = PeerAnnouncement;
  using GatewayObserverNodeId = link::NodeId;
  template <typename A>
  friend void sawAnnouncement(Observer&, A)
  {
  }
  template <typename It>
  friend void channelsLeft(Observer&, It, It)
  {
  }
};

struct Handler
{
  void receiveChannelRequest(ChannelRequest, uint8_t) {}
  void receiveChannelRequest(ChannelStopRequest, uint8_t) {}
  template <typename It>
  void receiveAudioBuffer(It, It)
  {
  }
};

// What BasicLinkAudio::setPeerName / LinkAudioSink::setName do to a name.
std::string apiName(std::string name)
{
  if (name.size() > v1::kMaxNameSize)
    name.resize(v1::kMaxNameSize);
  return name;
}

PeerAnnouncement makeAnnouncement(FuzzedDataProvider& fdp)
{
  auto a = PeerAnnouncement{link::NodeId{fuzz::kLocalId},
                            link::NodeId{fuzz::kSessionId},
                            {apiName(fdp.ConsumeRandomLengthString(1024))},
                            {}};
  const auto n = fdp.ConsumeIntegralInRange<size_t>(0, 64);
  for (size_t i = 0; i < n && fdp.remaining_bytes() > 0; ++i)
  {
    link::NodeIdArray id{};
    for (auto& b : id)
      b = fdp.ConsumeIntegral<uint8_t>();
    a.channels.channels.push_back({apiName(fdp.ConsumeRandomLengthString(1024)), id});
  }
  return a;
}

void check(bool ok, const char* what)
{
  if (!ok)
  {
    std::fprintf(stderr, "announcement round-trip mismatch: %s\n", what);
    std::abort();
  }
}

void verify(discovery::test::Interface& iface, const PeerAnnouncement& expected)
{
  size_t channelIndex = 0;
  for (const auto& [message, to] : iface.sentMessages)
  {
    check(message.size() <= v1::kMaxMessageSize, "datagram larger than kMaxMessageSize");
    auto [header, payloadBegin] = v1::parseMessageHeader(message.begin(), message.end());
    if (header.messageType != v1::kPeerAnnouncement)
      continue;
    check(header.ident == expected.ident(), "ident");
    const auto got = PeerAnnouncement::fromPayload(header.ident, payloadBegin, message.end());
    check(got.sessionId == expected.sessionId, "session id");
    check(got.peerInfo.name == expected.peerInfo.name, "peer name");
    for (const auto& c : got.channels.channels)
    {
      check(channelIndex < expected.channels.channels.size(), "extra channel");
      check(c.id == expected.channels.channels[channelIndex].id, "channel id");
      check(c.name == expected.channels.channels[channelIndex].name, "channel name");
      ++channelIndex;
    }
  }
  check(channelIndex == expected.channels.channels.size(), "missing channels");
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  FuzzedDataProvider fdp(data, size);
  ableton::test::serial_io::Fixture fixture;
  Observer observer;
  Handler handler;
  auto pIface = std::make_shared<discovery::test::Interface>(
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.42"), 1234});
  const auto peerEndpoint =
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.234"), 1900};

  const auto first = makeAnnouncement(fdp);
  FuzzMessenger messenger(util::injectRef(handler),
                          pIface,
                          first,
                          util::injectVal(fixture.makeIoContext()),
                          5,
                          20,
                          util::injectRef(observer));
  messenger.sawLinkAudioEndpoint(link::NodeId{fuzz::kPeerId}, peerEndpoint);
  fixture.advanceTime(std::chrono::milliseconds(260));
  verify(*pIface, first);

  // Rename peer / channels, add or remove channels (setPeerName, sink setName, new sinks).
  const auto second = makeAnnouncement(fdp);
  pIface->sentMessages.clear();
  messenger.updateAnnouncement(second);
  fixture.advanceTime(std::chrono::milliseconds(260));
  verify(*pIface, second);
  return 0;
}
