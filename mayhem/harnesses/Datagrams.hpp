// Splits one fuzz input into a sequence of UDP datagrams: each datagram is a
// big-endian u16 length followed by that many bytes; a short trailing length
// takes the rest of the input. Shared by the multi-datagram harnesses.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fuzz
{
inline std::vector<std::vector<uint8_t>> splitDatagrams(const uint8_t* data, size_t size)
{
  std::vector<std::vector<uint8_t>> out;
  size_t pos = 0;
  while (pos + 2 <= size && out.size() < 64)
  {
    size_t len = (size_t(data[pos]) << 8) | data[pos + 1];
    pos += 2;
    if (len > size - pos)
      len = size - pos;
    out.emplace_back(data + pos, data + pos + len);
    pos += len;
  }
  return out;
}
} // namespace fuzz
