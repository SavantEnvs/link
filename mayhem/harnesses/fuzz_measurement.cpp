// Fuzzes link::Measurement, which consumes _link_v1 pong datagrams from a peer
// while measuring its clock offset: header parsing, SessionMembership /
// GHostTime / PrevGHostTime / HostTime TLV parsing, sample accumulation,
// follow-up pings, and on completion the offset samples (fed here to the same
// Median + GhostXForm math MeasurementService applies).
// Setup mirrors src/ableton/link/tst_Measurement.cpp.
#include "Datagrams.hpp"

#include <cmath>

#include <ableton/discovery/test/Socket.hpp>
#include <ableton/link/GhostXForm.hpp>
#include <ableton/link/Measurement.hpp>
#include <ableton/link/Median.hpp>
#include <ableton/link/SessionId.hpp>
#include <ableton/util/test/IoService.hpp>
#include <ableton/util/test/Timer.hpp>

using namespace ableton;
using namespace ableton::link;

namespace
{
struct MockClock
{
  std::chrono::microseconds micros() const { return std::chrono::microseconds{4}; }
};

struct MockIoContext
{
  template <std::size_t BufferSize>
  using Socket = discovery::test::Socket;

  template <std::size_t BufferSize>
  Socket<BufferSize> openUnicastSocket(const discovery::IpAddress&)
  {
    return Socket<BufferSize>(mIo);
  }

  using Timer = util::test::Timer;
  Timer makeTimer() { return {}; }

  using Log = util::NullLog;
  Log log() const { return {}; }

  template <typename Handler>
  void async(Handler handler)
  {
    handler();
  }

  ableton::util::test::IoService mIo;
};

const NodeId kSession{{'s', 'e', 's', 's', 'i', 'o', 'n', '1'}};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  const auto datagrams = fuzz::splitDatagrams(data, size);
  if (datagrams.empty())
    return -1;

  MockIoContext ioCtx;
  auto socket = ioCtx.openUnicastSocket<512>({});
  PeerState state{};
  state.nodeState.sessionId = kSession;
  state.measurementEndpoint =
    discovery::UdpEndpoint(discovery::makeAddress("127.0.0.1"), 9999);

  double result = 0;
  bool done = false;
  Measurement<MockClock, MockIoContext> measurement(
    state,
    [&](std::vector<double> samples)
    {
      done = true;
      if (!samples.empty())
      {
        result = median(samples.begin(), samples.end());
        const auto xform =
          GhostXForm{1, std::chrono::microseconds(llround(result))};
        result += static_cast<double>(xform.hostToGhost(std::chrono::microseconds{0}).count());
      }
    },
    {},
    MockClock{},
    util::Injected<MockIoContext>(ioCtx),
    socket);

  const auto from = discovery::UdpEndpoint(discovery::makeAddress("127.0.0.1"), 8888);
  for (const auto& d : datagrams)
  {
    if (done)
      break;
    measurement(from, d.data(), d.data() + d.size());
  }
  return 0;
}
