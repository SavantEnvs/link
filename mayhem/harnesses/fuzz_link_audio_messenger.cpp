// Fuzzes the LinkAudio UDP datagram dispatcher (link_audio::UdpMessenger):
// header parsing, peer announcements (PeerInfo / ChannelAnnouncements), channel
// byes, channel (stop) requests, pings/pongs and audio buffers, exactly as a
// datagram arriving on the LinkAudio socket. Audio buffers are handed to a real
// MainProcessor with one registered Source, as LinkAudio's Controller does.
// Setup mirrors src/ableton/link_audio/tst_UdpMessenger.cpp.
#include "Datagrams.hpp"
#include "LinkAudioSource.hpp"

#include <ableton/discovery/test/Interface.hpp>
#include <ableton/link/PayloadEntries.hpp>
#include <ableton/link_audio/ChannelAnnouncements.hpp>
#include <ableton/link_audio/ChannelRequests.hpp>
#include <ableton/link_audio/PeerAnnouncement.hpp>
#include <ableton/link_audio/UdpMessenger.hpp>
#include <ableton/link_audio/v1/Messages.hpp>

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

  template <typename Announcement>
  friend void sawAnnouncement(Observer& o, Announcement a)
  {
    o.numChannels += a.announcement.channels.channels.size();
    for (const auto& c : a.announcement.channels.channels)
      o.nameBytes += c.name.size();
    o.nameBytes += a.announcement.peerInfo.name.size();
  }

  template <typename It>
  friend void channelsLeft(Observer& o, It begin, It end)
  {
    o.numByes += static_cast<size_t>(std::distance(begin, end));
  }

  size_t numChannels = 0;
  size_t nameBytes = 0;
  size_t numByes = 0;
};

struct Handler
{
  explicit Handler(fuzz::AudioSink& s)
    : sink(s)
  {
  }
  void receiveChannelRequest(ChannelRequest, uint8_t) { ++requests; }
  void receiveChannelRequest(ChannelStopRequest, uint8_t) { ++requests; }
  template <typename It>
  void receiveAudioBuffer(It begin, It end)
  {
    sink.processor.receiveAudioBuffer(begin, end);
  }
  fuzz::AudioSink& sink;
  size_t requests = 0;
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  const auto datagrams = fuzz::splitDatagrams(data, size);
  if (datagrams.empty())
    return -1;

  const auto peerEndpoint =
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.234"), 1900};
  const auto peerId = link::NodeId{fuzz::kPeerId};

  fuzz::AudioSink sink;
  Observer observer;
  Handler handler(sink);
  auto pIface = std::make_shared<discovery::test::Interface>(
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.42"), 1234});

  auto announcement = PeerAnnouncement{
    link::NodeId{fuzz::kLocalId}, link::NodeId{fuzz::kSessionId}, {"FuzzPeer"}, {}};
  announcement.channels.channels.push_back({"FuzzChannel", link::NodeId{fuzz::kSourceId}});

  FuzzMessenger messenger(util::injectRef(handler),
                      pIface,
                      announcement,
                      util::injectVal(sink.fixture.makeIoContext()),
                      5,
                      20,
                      util::injectRef(observer));
  // Announcements are only accepted from known receivers.
  messenger.sawLinkAudioEndpoint(peerId, peerEndpoint);

  for (const auto& d : datagrams)
  {
    pIface->incomingMessage(peerEndpoint, d.begin(), d.end());
    sink.fixture.flush();
  }
  sink.fixture.advanceTime(std::chrono::milliseconds(260));
  return 0;
}
