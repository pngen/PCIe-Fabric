// SPDX-License-Identifier: Apache-2.0
// Winsock TCP transport used by coordinator and worker. Socket lifetime is
// managed, send/recv bounded, and no-data is distinguished from failure.
#include "pciefabric/tcp.hpp"

#ifdef _WIN32
#include <algorithm>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "pciefabric/protocol.hpp"

namespace pci {

namespace {
struct WinsockInit {
  WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
  ~WinsockInit() { WSACleanup(); }
};
WinsockInit g_init;
}  // namespace

TcpSocket::TcpSocket(TcpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = invalid(); }
TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
  if (this != &o) { close(); fd_ = o.fd_; o.fd_ = invalid(); }
  return *this;
}

Result<void> TcpSocket::connect(const std::string& host, std::uint16_t port) {
  struct addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  std::string portstr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portstr.c_str(), &hints, &res) != 0)
    return Result<void>::err(ErrorCode::NETWORK_ERROR, "getaddrinfo failed");
  std::uintptr_t s = invalid();
  for (auto* p = res; p; p = p->ai_next) {
    s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == invalid()) continue;
    if (::connect(static_cast<SOCKET>(s), p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) { u_long mode = 0; ::ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &mode); break; }
    ::closesocket(static_cast<SOCKET>(s)); s = invalid();
  }
  freeaddrinfo(res);
  if (s == invalid()) return Result<void>::err(ErrorCode::NETWORK_ERROR, "connect failed");
  fd_ = s;
  return Result<void>::success();
}

Result<void> TcpSocket::send_all(const std::uint8_t* data, std::size_t n) {
  if (!valid()) return Result<void>::err(ErrorCode::NETWORK_ERROR, "socket not open");
  std::size_t sent = 0;
  while (sent < n) {
    int r = ::send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(data + sent),
                   static_cast<int>(n - sent), 0);
    if (r == SOCKET_ERROR) return Result<void>::err(ErrorCode::NETWORK_ERROR, "send failed");
    if (r == 0) return Result<void>::err(ErrorCode::PEER_LOST, "peer closed on send");
    sent += static_cast<std::size_t>(r);
  }
  return Result<void>::success();
}

Result<std::size_t> TcpSocket::recv_some(std::uint8_t* buf, std::size_t n) {
  if (!valid()) return Result<std::size_t>::err(ErrorCode::NETWORK_ERROR, "socket not open");
  int r = ::recv(static_cast<SOCKET>(fd_), reinterpret_cast<char*>(buf), static_cast<int>(n), 0);
  if (r == SOCKET_ERROR) {
    int e = WSAGetLastError();
    if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK)
      return Result<std::size_t>::success(static_cast<std::size_t>(0));
    return Result<std::size_t>::err(ErrorCode::NETWORK_ERROR, "recv failed");
  }
  return Result<std::size_t>::success(static_cast<std::size_t>(r));
}

void TcpSocket::close() {
  if (valid()) { ::closesocket(static_cast<SOCKET>(fd_)); fd_ = invalid(); }
}

TcpListener::TcpListener(std::uint16_t port) : port_(port) {}
TcpListener::TcpListener(TcpListener&& o) noexcept : fd_(o.fd_), port_(o.port_) { o.fd_ = invalid(); }
TcpListener& TcpListener::operator=(TcpListener&& o) noexcept {
  if (this != &o) { close(); fd_ = o.fd_; port_ = o.port_; o.fd_ = invalid(); }
  return *this;
}
Result<void> TcpListener::bind() {
  std::uintptr_t s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s == invalid()) return Result<void>::err(ErrorCode::NETWORK_ERROR, "socket failed");
  BOOL opt = TRUE;
  ::setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (::bind(static_cast<SOCKET>(s), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    ::closesocket(static_cast<SOCKET>(s));
    return Result<void>::err(ErrorCode::NETWORK_ERROR, "bind failed");
  }
  if (::listen(static_cast<SOCKET>(s), SOMAXCONN) == SOCKET_ERROR) {
    ::closesocket(static_cast<SOCKET>(s));
    return Result<void>::err(ErrorCode::NETWORK_ERROR, "listen failed");
  }
  fd_ = s;
  return Result<void>::success();
}

Result<bool> TcpListener::accept(TcpSocket& out, bool non_blocking) {
  if (!valid()) return Result<bool>::err(ErrorCode::NETWORK_ERROR, "listener not open");
  if (non_blocking) {
    u_long mode = 1;
    ::ioctlsocket(static_cast<SOCKET>(fd_), FIONBIO, &mode);
  }
  SOCKET c = ::accept(static_cast<SOCKET>(fd_), nullptr, nullptr);
  if (c == INVALID_SOCKET) {
    int e = WSAGetLastError();
    if (non_blocking && (e == WSAEWOULDBLOCK)) return Result<bool>::success(false);
    return Result<bool>::err(ErrorCode::NETWORK_ERROR, "accept failed");
  }
  if (non_blocking) { u_long mode = 0; ::ioctlsocket(static_cast<SOCKET>(fd_), FIONBIO, &mode); }
  // The accepted connection socket is blocking: no-data/timeout must never be
  // misread as a permanent connection failure.
  { u_long mode = 0; ::ioctlsocket(c, FIONBIO, &mode); }
  out = TcpSocket(static_cast<std::uintptr_t>(c));
  return Result<bool>::success(true);
}
void TcpListener::close() {
  if (valid()) { ::closesocket(static_cast<SOCKET>(fd_)); fd_ = invalid(); }
}

Result<void> write_frame(TcpSocket& s, const wire::Frame& f) {
  auto bytes = wire::encode(f);
  return s.send_all(bytes.data(), bytes.size());
}

Result<wire::Frame> read_frame(TcpSocket& s) {
  std::vector<std::uint8_t> header(21);
  std::size_t got = 0;
  while (got < 21) {
    auto r = s.recv_some(header.data() + got, 21 - got);
    if (!r) return Result<wire::Frame>::err(r.code(), r.message());
    if (r.value() == 0) return Result<wire::Frame>::err(ErrorCode::PEER_LOST, "connection closed");
    got += r.value();
  }
  // parse payload length from header bytes
  std::uint64_t len = 0;
  for (int i = 0; i < 8; ++i) len |= static_cast<std::uint64_t>(header[9 + i]) << (i * 8);
  std::size_t plen = static_cast<std::size_t>(len);
  if (plen > wire::kMaxPayload) return Result<wire::Frame>::err(ErrorCode::PROTOCOL_OVERFLOW, "payload too large");
  std::vector<std::uint8_t> frame(21 + plen);
  std::copy(header.begin(), header.end(), frame.begin());
  got = 0;
  while (got < plen) {
    auto r = s.recv_some(frame.data() + 21 + got, plen - got);
    if (!r) return Result<wire::Frame>::err(r.code(), r.message());
    if (r.value() == 0) return Result<wire::Frame>::err(ErrorCode::PEER_LOST, "connection closed mid-frame");
    got += r.value();
  }
  return wire::decode(frame);
}

}  // namespace pci
#endif