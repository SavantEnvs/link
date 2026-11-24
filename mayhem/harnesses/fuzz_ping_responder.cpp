// Fuzzes link::PingResponder, which answers _link_v1 ping datagrams on the
// measurement socket: header parsing, payload size limit, HostTime/PrevGHostTime
// TLV parsing and the pong it encodes back with the GhostXForm.
// Setup mirrors src/ableton/link/tst_PingResponder.cpp.
#include <ableton/discovery/test/Socket.hpp>
#include <ableton/link/GhostXForm.hpp>
#include <ableton/link/PingResponder.hpp>
#include <ableton/util/test/IoService.hpp>

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

  using Log = util::NullLog;
  Log log() const { return {}; }

  ableton::util::test::IoService mIo;
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  util::Injected<MockIoContext> io{MockIoContext{}};
  auto socket = io->openUnicastSocket<512>(discovery::makeAddress("127.0.0.1"));
  PingResponder<MockClock, MockIoContext> responder(
    socket,
    NodeId{{'s', 'e', 's', 's', 'i', 'o', 'n', '1'}},
    GhostXForm{1.0, std::chrono::microseconds{0}},
    MockClock{},
    util::injectVal(*io));
  const auto from = discovery::UdpEndpoint{discovery::makeAddress("127.0.0.1"), 8888};
  responder(from, data, data + size);
  return 0;
}
