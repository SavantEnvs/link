// Writes the committed seed corpora under mayhem/<target>/testsuite/ using upstream's own
// message encoders. Not part of the build; regenerate with mayhem/seedgen/gen_seeds.sh.
#include "LinkAudioSource.hpp"

#include <ableton/discovery/v1/Messages.hpp>
#include <ableton/link/PayloadEntries.hpp>
#include <ableton/link/PeerState.hpp>
#include <ableton/link/v1/Messages.hpp>
#include <ableton/link_audio/ChannelAnnouncements.hpp>
#include <ableton/link_audio/ChannelRequests.hpp>
#include <ableton/link_audio/PeerAnnouncement.hpp>
#include <ableton/link_audio/v1/Messages.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace ableton;
using Bytes = std::vector<uint8_t>;
using Micros = std::chrono::microseconds;

static void write(const std::string& dir, const std::string& name, const Bytes& b)
{
  std::ofstream(dir + "/" + name, std::ios::binary)
    .write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

static Bytes frame(const std::vector<Bytes>& ds)
{
  Bytes out;
  for (const auto& d : ds)
  {
    out.push_back(uint8_t(d.size() >> 8));
    out.push_back(uint8_t(d.size() & 0xff));
    out.insert(out.end(), d.begin(), d.end());
  }
  return out;
}

template <typename F>
static Bytes enc(F f)
{
  std::array<uint8_t, 1200> buf{};
  auto end = f(buf.begin());
  return Bytes(buf.begin(), end);
}

static link_audio::AudioBuffer audioBuffer(uint8_t numChannels, uint16_t frames, uint32_t sr)
{
  link_audio::AudioBuffer audio;
  audio.channelId = link::NodeId{fuzz::kSourceId};
  audio.sessionId = link::NodeId{fuzz::kSessionId};
  audio.codec = link_audio::Codec::kPCM_i16;
  audio.sampleRate = sr;
  audio.numChannels = numChannels;
  audio.chunks = {link_audio::AudioBuffer::Chunk{7, frames, link::Beats{1.5}, link::Tempo{120.0}}};
  audio.numBytes = static_cast<uint16_t>(audio.numFrames() * numChannels * sizeof(int16_t));
  for (size_t i = 0; i < audio.numBytes; ++i)
    audio.bytes[i] = uint8_t(i * 37);
  return audio;
}

int main(int argc, char** argv)
{
  const std::string root = argc > 1 ? argv[1] : "mayhem";
  const auto peer = link::NodeId{fuzz::kPeerId};
  const auto session = link::NodeId{fuzz::kSessionId};
  const auto source = link::NodeId{fuzz::kSourceId};

  // --- link_audio_buffer: kAudioBuffer payloads (header stripped)
  {
    const auto d = root + "/link-audio-buffer/testsuite";
    int i = 0;
    for (auto [ch, fr, sr] : {std::tuple<uint8_t, uint16_t, uint32_t>{2, 4, 44100},
                              {1, 64, 48000},
                              {2, 128, 96000}})
    {
      auto msg = enc([&](auto out)
                     { return link_audio::v1::audioBufferMessage(peer, audioBuffer(ch, fr, sr), out); });
      const auto payload = link_audio::v1::parseMessageHeader(msg.begin(), msg.end()).second;
      write(d, "buffer" + std::to_string(i++), Bytes(payload, msg.end()));
    }
  }

  // --- link_audio_messenger: framed LinkAudio datagrams
  {
    const auto d = root + "/link-audio-messenger/testsuite";
    auto ann = link_audio::PeerAnnouncement{peer, session, {"RemotePeer"}, {}};
    ann.channels.channels.push_back({"Remote 1", link::NodeId{{'r', 'c', 'h', 'a', 'n', '0', '0', '1'}}});
    ann.channels.channels.push_back({"Remote 2", link::NodeId{{'r', 'c', 'h', 'a', 'n', '0', '0', '2'}}});
    const auto announce = enc([&](auto out) {
      return link_audio::v1::detail::encodeMessage(peer, 5, link_audio::v1::kPeerAnnouncement,
        toPayload(ann) + discovery::makePayload(link::HostTime{Micros{1234}}), out);
    });
    const auto byes = enc([&](auto out) {
      link_audio::ChannelByes b;
      b.byes.push_back({link::NodeId{{'r', 'c', 'h', 'a', 'n', '0', '0', '1'}}});
      return link_audio::v1::detail::encodeMessage(peer, 5, link_audio::v1::kChannelByes,
        discovery::makePayload(b), out);
    });
    const auto pong = enc([&](auto out) {
      return link_audio::v1::detail::encodeMessage(peer, 5, link_audio::v1::kPong,
        discovery::makePayload(link::HostTime{Micros{0}}), out);
    });
    const auto req = enc([&](auto out) {
      return link_audio::v1::detail::encodeMessage(peer, 5, link_audio::v1::kChannelRequest,
        toPayload(link_audio::ChannelRequest{peer, source}), out);
    });
    const auto stop = enc([&](auto out) {
      return link_audio::v1::detail::encodeMessage(peer, 5, link_audio::v1::kStopChannelRequest,
        toPayload(link_audio::ChannelStopRequest{peer, source}), out);
    });
    const auto audio = enc([&](auto out)
                           { return link_audio::v1::audioBufferMessage(peer, audioBuffer(2, 16, 44100), out); });
    write(d, "announce", frame({announce}));
    write(d, "announce_byes", frame({announce, byes}));
    write(d, "pong", frame({pong}));
    write(d, "requests", frame({req, stop}));
    write(d, "audio", frame({audio, audio}));
    write(d, "all", frame({announce, pong, req, audio, stop, byes}));
  }

  // --- discovery_messenger: framed _asdp_v1 datagrams carrying a link::PeerState
  {
    const auto d = root + "/discovery-messenger/testsuite";
    link::PeerState st{};
    st.nodeState.nodeId = peer;
    st.nodeState.sessionId = session;
    st.nodeState.timeline = link::Timeline{link::Tempo{133.0}, link::Beats{2.0}, Micros{5000}};
    st.nodeState.startStopState = link::StartStopState{true, link::Beats{1.0}, Micros{42}};
    st.measurementEndpoint = discovery::UdpEndpoint{discovery::makeAddress("10.0.0.2"), 20808};
    st.audioEndpoint = discovery::UdpEndpoint{discovery::makeAddress("10.0.0.2"), 20809};
    const auto alive = enc([&](auto out) { return discovery::v1::aliveMessage(peer, 5, toPayload(st), out); });
    const auto resp = enc([&](auto out) { return discovery::v1::responseMessage(peer, 5, toPayload(st), out); });
    const auto bye = enc([&](auto out) { return discovery::v1::byeByeMessage(peer, out); });
    st.measurementEndpoint = discovery::UdpEndpoint{discovery::makeAddress("fe80::1"), 20808};
    st.audioEndpoint = discovery::UdpEndpoint{discovery::makeAddress("fe80::1"), 20809};
    const auto alive6 = enc([&](auto out) { return discovery::v1::aliveMessage(peer, 5, toPayload(st), out); });
    write(d, "alive", frame({alive}));
    write(d, "alive6", frame({alive6}));
    write(d, "response_bye", frame({resp, bye}));
    write(d, "alive_response_bye", frame({alive, resp, alive6, bye}));
  }

  // --- ping_responder: one _link_v1 ping
  {
    const auto d = root + "/ping-responder/testsuite";
    write(d, "ping", enc([&](auto out)
                         { return link::v1::pingMessage(discovery::makePayload(link::HostTime{Micros{2}}), out); }));
    write(d, "ping_prev", enc([&](auto out) {
      return link::v1::pingMessage(
        discovery::makePayload(link::HostTime{Micros{2}}, link::PrevGHostTime{Micros{1}}), out);
    }));
  }

  // --- measurement: framed _link_v1 pongs
  {
    const auto d = root + "/measurement/testsuite";
    std::vector<Bytes> pongs;
    for (int i = 0; i < 40; ++i)
    {
      pongs.push_back(enc([&](auto out) {
        return link::v1::pongMessage(
          discovery::makePayload(link::SessionMembership{session},
                                 link::GHostTime{Micros{3 + i * 10}},
                                 link::HostTime{Micros{2 + i * 10}},
                                 link::PrevGHostTime{Micros{1 + i * 10}}),
          out);
      }));
    }
    write(d, "pong1", frame({pongs[0]}));
    write(d, "pongs", frame(pongs));
    write(d, "init_pong", frame({enc([&](auto out) {
      return link::v1::pongMessage(
        discovery::makePayload(link::SessionMembership{session}, link::GHostTime{Micros{0}},
                               link::HostTime{Micros{0}}),
        out);
    })}));
  }
  return 0;
}
