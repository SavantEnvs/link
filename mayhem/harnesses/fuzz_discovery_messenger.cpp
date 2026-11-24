// Fuzzes Link's discovery protocol receive path (discovery::UdpMessenger with the
// real link::PeerState, as PeerGateway instantiates it): _asdp_v1 header parsing,
// alive/response/byebye dispatch, PeerState/NodeState TLV payload parsing
// (Timeline, SessionMembership, StartStopState, MeasurementEndpointV4/V6,
// AudioEndpointV4/V6) and the response we send back to an alive. The decoded peer
// state is then used the way link::Peers / SessionController use it.
// Setup mirrors src/ableton/discovery/tst_UdpMessenger.cpp.
#include "Datagrams.hpp"

#include <ableton/discovery/UdpMessenger.hpp>
#include <ableton/discovery/test/Interface.hpp>
#include <ableton/link/PeerState.hpp>
#include <ableton/test/serial_io/Fixture.hpp>

using namespace ableton;

namespace
{
struct Handler
{
  void operator()(discovery::PeerState<link::PeerState> s)
  {
    const auto& st = s.peerState;
    const auto& tl = st.nodeState.timeline;
    // Map a host-independent beat through the received timeline (as Link does when
    // a session's timeline is adopted).
    const auto beats = tl.toBeats(std::chrono::microseconds{1000000});
    const auto t = tl.fromBeats(link::Beats{1.0});
    // Unsigned (wrapping) accumulation: the harness's own bookkeeping must not trip UBSan.
    acc += static_cast<uint64_t>(beats.microBeats()) + static_cast<uint64_t>(t.count()) + s.ttl;
    acc += st.nodeState.startStopState.isPlaying ? 1u : 0u;
    acc += st.measurementEndpoint.port();
    if (st.audioEndpoint)
      acc += st.audioEndpoint->port();
    ++numStates;
  }
  void operator()(discovery::ByeBye<link::NodeId>) { ++numByes; }

  uint64_t acc = 0;
  size_t numStates = 0;
  size_t numByes = 0;
};

template <typename Messenger>
struct Receiver
{
  void listen(Messenger& m) { m.receive(std::ref(*this)); }
  template <typename T>
  void operator()(T t)
  {
    handler(std::move(t));
    listen(*pMessenger);
  }
  Handler handler;
  Messenger* pMessenger = nullptr;
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  const auto datagrams = fuzz::splitDatagrams(data, size);
  if (datagrams.empty())
    return -1;

  ableton::test::serial_io::Fixture io;
  auto iface = discovery::test::Interface(
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.42"), 1234});
  const auto peerEndpoint =
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.234"), 1900};

  link::PeerState self{};
  self.nodeState.nodeId = link::NodeId{{'l', 'o', 'c', 'a', 'l', 'i', 'd', '0'}};
  self.nodeState.sessionId = self.nodeState.nodeId;
  self.nodeState.timeline =
    link::Timeline{link::Tempo{120.0}, link::Beats{0.0}, std::chrono::microseconds{0}};
  self.measurementEndpoint =
    discovery::UdpEndpoint{discovery::makeAddress("123.123.234.42"), 2000};

  auto messenger = discovery::makeUdpMessenger(
    util::injectRef(iface), self, util::injectVal(io.makeIoContext()), 5, 20);
  using M = decltype(messenger);
  Receiver<M> receiver;
  receiver.pMessenger = &messenger;
  receiver.listen(messenger);

  for (const auto& d : datagrams)
  {
    iface.incomingMessage(peerEndpoint, d.begin(), d.end());
    io.flush();
  }
  return 0;
}
