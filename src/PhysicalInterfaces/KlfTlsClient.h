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

#ifndef KLFTLSCLIENT_H
#define KLFTLSCLIENT_H

#include <cstdint>
#include <atomic>
#include <stdexcept>
#include <string>

#include <gnutls/gnutls.h>

namespace Velux
{

class KlfSocketException : public std::runtime_error
{
public:
    explicit KlfSocketException(const std::string& message) : std::runtime_error(message) {}
};

class KlfSocketTimeoutException : public KlfSocketException
{
public:
    explicit KlfSocketTimeoutException(const std::string& message) : KlfSocketException(message) {}
};

class KlfSocketClosedException : public KlfSocketException
{
public:
    explicit KlfSocketClosedException(const std::string& message) : KlfSocketException(message) {}
};

/**
 * Minimal TLS client for the KLF200 based on GnuTLS. One object is used for exactly one connection.
 *
 * libc1-net can't be used for the KLF200: It aborts the TLS handshake after 5 seconds (the KLF200 needs more than
 * 4 seconds when idle) and it rejects the self-signed certificate of the KLF200, which expired on 2026-07-12, even
 * when certificate verification is disabled. As in the original implementation the certificate is not verified.
 *
 * read() must only be called from one thread and send() must only be called from one thread at a time. Reading and
 * sending at the same time from two threads is supported by GnuTLS. shutdown() can be called from any thread.
 */
class KlfTlsClient
{
public:
    KlfTlsClient(std::string host, uint16_t port);
    ~KlfTlsClient();
    KlfTlsClient(const KlfTlsClient&) = delete;
    KlfTlsClient& operator=(const KlfTlsClient&) = delete;

    /**
     * Connects and performs the TLS handshake. Throws KlfSocketException on errors.
     */
    void open(int32_t connectTimeout, int32_t handshakeTimeout);
    bool connected() { return _connected; }

    /**
     * Closes the connection. Pending read() and send() calls return with KlfSocketClosedException.
     */
    void shutdown();

    /**
     * Returns the number of bytes read (always > 0). Throws KlfSocketTimeoutException when no data was received within
     * the timeout and KlfSocketClosedException when the connection is closed.
     */
    size_t read(uint8_t* buffer, size_t bufferSize, int32_t timeout);
    void send(const uint8_t* data, size_t size, int32_t timeout);
private:
    std::string _host;
    uint16_t _port = 0;
    int _fd = -1;
    gnutls_session_t _session = nullptr;
    gnutls_certificate_credentials_t _credentials = nullptr;
    std::atomic_bool _connected{false};
    std::atomic_bool _shutdown{false};

    /**
     * Waits until the socket is ready for the given poll events. Returns false on timeout.
     */
    bool waitFor(short events, int64_t timeout);
};

}
#endif
