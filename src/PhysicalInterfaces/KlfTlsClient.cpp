/* Copyright 2013-2019 Homegear GmbH
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "KlfTlsClient.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <memory>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Velux {

namespace {
//The KLF200 supports TLS 1.2 with DHE-RSA and AES-256-CBC only and prefers SHA256 as MAC. Current GnuTLS versions don't
//include DHE-RSA and CBC with SHA256 in NORMAL anymore.
constexpr const char *kTlsPriorities = "NORMAL:-VERS-TLS1.3:+DHE-RSA:+AES-256-CBC:+SHA256";

int64_t getTime() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

KlfTlsClient::KlfTlsClient(std::string host, uint16_t port) : _host(std::move(host)), _port(port) {
}

KlfTlsClient::~KlfTlsClient() {
  if (_session) gnutls_deinit(_session);
  if (_credentials) gnutls_certificate_free_credentials(_credentials);
  if (_fd != -1) ::close(_fd);
}

bool KlfTlsClient::waitFor(short events, int64_t timeout) {
  int64_t endTime = getTime() + timeout;
  while (!_shutdown) {
    int64_t remaining = endTime - getTime();
    if (remaining <= 0) return false;
    pollfd pollInfo{_fd, events, 0};
    //Wake up at least once per second to react to shutdown() during the connect phase.
    int result = ::poll(&pollInfo, 1, (int)std::min<int64_t>(remaining, 1000));
    if (result > 0) return true; //Ready or error. The following socket operation reports the error.
    if (result < 0 && errno != EINTR) throw KlfSocketException(std::string("Error polling socket: ") + strerror(errno));
  }
  throw KlfSocketClosedException("Socket was shut down.");
}

void KlfTlsClient::open(int32_t connectTimeout, int32_t handshakeTimeout) {
  if (_fd != -1) throw KlfSocketException("Socket was already opened.");

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *addressInfo = nullptr;
  int result = getaddrinfo(_host.c_str(), std::to_string(_port).c_str(), &hints, &addressInfo);
  if (result != 0) throw KlfSocketException("Could not resolve " + _host + ": " + gai_strerror(result));
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addressInfoGuard(addressInfo, &freeaddrinfo);

  _fd = ::socket(addressInfo->ai_family, addressInfo->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, addressInfo->ai_protocol);
  if (_fd == -1) throw KlfSocketException(std::string("Could not create socket: ") + strerror(errno));

  if (::connect(_fd, addressInfo->ai_addr, addressInfo->ai_addrlen) == -1) {
    if (errno != EINPROGRESS) throw KlfSocketException("Could not connect to " + _host + ": " + strerror(errno));
    if (!waitFor(POLLOUT, connectTimeout)) throw KlfSocketTimeoutException("Timeout connecting to " + _host + ".");
    int socketError = 0;
    socklen_t length = sizeof(socketError);
    if (getsockopt(_fd, SOL_SOCKET, SO_ERROR, &socketError, &length) == -1) socketError = errno;
    if (socketError != 0) throw KlfSocketException("Could not connect to " + _host + ": " + strerror(socketError));
  }

  result = gnutls_certificate_allocate_credentials(&_credentials);
  if (result != GNUTLS_E_SUCCESS) throw KlfSocketException(std::string("Could not allocate TLS credentials: ") + gnutls_strerror(result));
  result = gnutls_init(&_session, GNUTLS_CLIENT | GNUTLS_NONBLOCK);
  if (result != GNUTLS_E_SUCCESS) throw KlfSocketException(std::string("Could not initialize TLS session: ") + gnutls_strerror(result));
  result = gnutls_priority_set_direct(_session, kTlsPriorities, nullptr);
  if (result != GNUTLS_E_SUCCESS) throw KlfSocketException(std::string("Could not set TLS priorities: ") + gnutls_strerror(result));
  result = gnutls_credentials_set(_session, GNUTLS_CRD_CERTIFICATE, _credentials);
  if (result != GNUTLS_E_SUCCESS) throw KlfSocketException(std::string("Could not set TLS credentials: ") + gnutls_strerror(result));
  gnutls_transport_set_int(_session, _fd);
  //The certificate is intentionally not verified (see class description), so gnutls_session_set_verify_cert() isn't called.

  int64_t startTime = getTime();
  while ((result = gnutls_handshake(_session)) < 0) {
    if (gnutls_error_is_fatal(result)) throw KlfSocketException(std::string("TLS handshake failed: ") + gnutls_strerror(result));
    int64_t remaining = startTime + handshakeTimeout - getTime();
    if (remaining <= 0 || !waitFor(gnutls_record_get_direction(_session) == 1 ? POLLOUT : POLLIN, remaining)) {
      throw KlfSocketTimeoutException("TLS handshake timed out after " + std::to_string(getTime() - startTime) + " ms.");
    }
  }

  _connected = true;
}

void KlfTlsClient::shutdown() {
  _shutdown = true;
  _connected = false;
  //Wakes up a pending poll() in another thread. The descriptor is closed in the destructor, when nobody uses it anymore.
  if (_fd != -1) ::shutdown(_fd, SHUT_RDWR);
}

size_t KlfTlsClient::read(uint8_t *buffer, size_t bufferSize, int32_t timeout) {
  if (!_connected) throw KlfSocketClosedException("Not connected.");
  int64_t endTime = getTime() + timeout;
  while (true) {
    if (gnutls_record_check_pending(_session) == 0) {
      int64_t remaining = endTime - getTime();
      if (remaining <= 0 || !waitFor(POLLIN, remaining)) throw KlfSocketTimeoutException("Read timeout.");
    }
    ssize_t received = gnutls_record_recv(_session, buffer, bufferSize);
    if (received > 0) return (size_t)received;
    if (received == GNUTLS_E_AGAIN || received == GNUTLS_E_INTERRUPTED) continue;
    _connected = false;
    if (received == 0) throw KlfSocketClosedException("Connection closed by peer.");
    throw KlfSocketClosedException(std::string("Error reading from socket: ") + gnutls_strerror((int)received));
  }
}

void KlfTlsClient::send(const uint8_t *data, size_t size, int32_t timeout) {
  if (!_connected) throw KlfSocketClosedException("Not connected.");
  int64_t endTime = getTime() + timeout;
  size_t sent = 0;
  while (sent < size) {
    ssize_t result = gnutls_record_send(_session, data + sent, size - sent);
    if (result > 0) {
      sent += (size_t)result;
      continue;
    }
    if (result == GNUTLS_E_AGAIN || result == GNUTLS_E_INTERRUPTED) {
      //GnuTLS requires calling gnutls_record_send() again with the same data.
      int64_t remaining = endTime - getTime();
      if (remaining <= 0 || !waitFor(POLLOUT, remaining)) throw KlfSocketTimeoutException("Write timeout.");
      continue;
    }
    _connected = false;
    throw KlfSocketClosedException(std::string("Error writing to socket: ") + gnutls_strerror((int)result));
  }
}

}
