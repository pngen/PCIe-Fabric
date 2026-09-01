#pragma once
// RAII Winsock TCP transport. Socket lifetime is managed; send/recv are
// bounded; no-data is explicitly distinguished from a permanent failure.
#include <cstdint>
#include <string>
#include <vector>

#include "pciefabric/result.hpp"

#ifndef _WIN32
// Satisfy the header on non-Windows builds with a disabled stub.
namespace pci {
class TcpSocket {
public:
  TcpSocket() = default;
  explicit TcpSocket(int /*fd*/) {}
  TcpSocket(TcpSocket&& o) noexcept { (void)o; }
  TcpSocket& operator=(TcpSocket&& o) noexcept { (void)o; return *this; }
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  [[nodiscard]] bool valid() const { return false; }
  Result<void> connect(const std::string&, std::uint16_t) { return Result<void>::err(ErrorCode::NOT_IMPLEMENTED, "tcp unsupported"); }
  Result<void> send_all(const std::uint8_t*, std::size_t) { return Result<void>::err(ErrorCode::NOT_IMPLEMENTED, "tcp unsupported"); }
  Result<std::size_t> recv_some(std::uint8_t*, std::size_t) { return Result<std::size_t>::err(ErrorCode::NOT_IMPLEMENTED, "tcp unsupported"); }
  void close() {}
};
class TcpListener {
public:
  explicit TcpListener(std::uint16_t) {}
  Result<void> bind() { return Result<void>::err(ErrorCode::NOT_IMPLEMENTED, "tcp unsupported"); }
  Result<std::size_t> accept(TcpSocket*, bool) { return Result<std::size_t>::err(ErrorCode::NOT_IMPLEMENTED, "tcp unsupported"); }
  void close() {}
};
}
#else
namespace pci {

namespace wire { struct Frame; }

class TcpSocket {
public:
  TcpSocket() = default;
  explicit TcpSocket(std::uintptr_t fd) : fd_(fd) {}
  ~TcpSocket() { close(); }
  TcpSocket(TcpSocket&& o) noexcept;
  TcpSocket& operator=(TcpSocket&& o) noexcept;
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  [[nodiscard]] bool valid() const noexcept { return fd_ != invalid(); }
  Result<void> connect(const std::string& host, std::uint16_t port);
  // Send all bytes; returns bytes sent (== n).
  Result<void> send_all(const std::uint8_t* data, std::size_t n);
  // Read into buffer; returns bytes read. 0 means orderly EOF.
  Result<std::size_t> recv_some(std::uint8_t* buf, std::size_t n);
  void close();

private:
  using raw_t = std::uintptr_t;
  static raw_t invalid() noexcept { return static_cast<raw_t>(-1); }
  raw_t fd_{invalid()};
};

// A blocking TCP listener. accept() returns the number of connections accepted
// into the out-slot; when non_blocking is true it returns 0 immediately if no
// connection is pending.
class TcpListener {
public:
  explicit TcpListener(std::uint16_t port);
  TcpListener(TcpListener&& o) noexcept;
  TcpListener& operator=(TcpListener&& o) noexcept;
  ~TcpListener() { close(); }
  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;
  Result<void> bind();
  Result<bool> accept(TcpSocket& out, bool non_blocking);
  void close();
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ != invalid(); }

private:
  using raw_t = std::uintptr_t;
  static raw_t invalid() noexcept { return static_cast<raw_t>(-1); }
  raw_t fd_{invalid()};
  std::uint16_t port_{};
};

// Frame-level I/O on a socket (read/write exactly one wire frame).
Result<void> write_frame(TcpSocket& s, const wire::Frame& f);
Result<wire::Frame> read_frame(TcpSocket& s);

}  // namespace pci
#endif