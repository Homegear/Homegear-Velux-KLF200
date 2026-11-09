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

#include "Klf200.h"
#include "../Velux.h"
#include "../GD.h"

#include <algorithm>

namespace Velux {

namespace {
//The KLF200 closes sockets after 15 minutes without communication (API chapter 4.1). GW_GET_STATE_REQ is used as keep-alive.
constexpr int64_t kHeartbeatInterval = 30000;
//Time without any received frame after a keep-alive request after which the connection is considered dead.
constexpr int64_t kHeartbeatTimeout = 30000;
//Read and write timeout. The listen thread checks the keep-alive at least this often.
constexpr int32_t kSocketTimeout = 10000;
constexpr int32_t kConnectTimeout = 10000;
//The TLS handshake of the KLF200 takes more than 4 seconds even when it is idle (libc1-net aborts it after 5 seconds, see
//KlfTlsClient).
constexpr int32_t kHandshakeTimeout = 30000;
//The KLF200 only accepts two sockets at the same time and frees sockets of dead connections only after 15 minutes. So don't
//reconnect too aggressively.
constexpr int32_t kMinReconnectDelay = 15;
constexpr int32_t kMaxReconnectDelay = 120;
//ProtocolID (1) + Length (1) + Command (2) + Data (max. 250) + Checksum (1)
constexpr size_t kMaxFrameSize = 255;

std::string getErrorString(const PVeluxPacket &errorPacket) {
  auto payload = errorPacket->getPayload();
  if (payload.empty()) return "unknown";
  std::string description;
  switch (payload.at(0)) {
    case 0: description = "Not further defined error"; break;
    case 1: description = "Unknown command or command is not accepted at this state"; break;
    case 2: description = "Error on frame structure"; break;
    case 7: description = "Busy. Try again later"; break;
    case 8: description = "Bad system table index"; break;
    case 12: description = "Not authenticated"; break;
    default: description = "Unknown error";
  }
  return std::to_string(payload.at(0)) + " (" + description + ")";
}

//Returns the packet as hex string for logging. Passwords (GW_PASSWORD_ENTER_REQ, GW_PASSWORD_CHANGE_REQ) are not written to the log.
std::string getLogString(const std::vector<uint8_t> &slipPacket) {
  if (slipPacket.size() > 5 && slipPacket[3] == 0x30 && (slipPacket[4] == 0x00 || slipPacket[4] == 0x02)) {
    return BaseLib::HelperFunctions::getHexString(slipPacket.data(), 5) + "...(password hidden)";
  }
  return BaseLib::HelperFunctions::getHexString(slipPacket);
}
}

Klf200::Klf200(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(GD::bl, GD::family->getFamily(), settings) {
  _out.init(GD::bl);
  _out.setPrefix(GD::out.getPrefix() + "KLF200 \"" + settings->id + "\": ");

  signal(SIGPIPE, SIG_IGN);

  _stopped = true;

  if (!settings) {
    _out.printCritical("Critical: Error initializing. Settings pointer is empty.");
    return;
  }
  _hostname = settings->host;
  _port = BaseLib::Math::getNumber(settings->port);
  if (_port < 1 || _port > 65535) _port = 51200;
}

Klf200::~Klf200() {
  stopListening();
  _bl->threadManager.join(_initThread);
}

uint16_t Klf200::getMessageCounter() {
  return _messageCounter++;
}

void Klf200::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet) {
  try {
    PVeluxPacket veluxPacket(std::dynamic_pointer_cast<VeluxPacket>(packet));
    if (!veluxPacket) return;

    if (!_initComplete) {
      //Before the login is complete the KLF200 only answers with "not authenticated".
      _out.printError("Error: Not connected to KLF200. Could not send packet " + BaseLib::HelperFunctions::getHexString(veluxPacket->getBinary()));
      return;
    }

    auto response = getResponse(veluxPacket->getResponseCommand(), veluxPacket);
    if (!response) _out.printError("Error sending packet " + BaseLib::HelperFunctions::getHexString(veluxPacket->getBinary()));
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Klf200::startListening() {
  try {
    stopListening();

    if (_hostname.empty()) {
      _out.printError("Error: Configuration of KLF200 is incomplete (hostname is missing). Please correct it in \"veluxklf200.conf\".");
      return;
    }

    if (_settings->password.empty()) {
      _out.printError("Error: Configuration of KLF200 is incomplete (password is missing). Please correct it in \"veluxklf200.conf\".");
      return;
    }

    _stopCallbackThread = false;
    _reconnectDelay = 0;
    if (_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Klf200::listen, this);
    else _bl->threadManager.start(_listenThread, true, &Klf200::listen, this);
    IPhysicalInterface::startListening();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Klf200::stopListening() {
  try {
    _stopCallbackThread = true;
    _stopped = true;
    {
      auto socket = getSocket();
      if (socket) socket->shutdown();
    }
    _bl->threadManager.join(_listenThread);
    _bl->threadManager.join(_initThread);
    closeSocket();
    _initComplete = false;
    IPhysicalInterface::stopListening();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::shared_ptr<KlfTlsClient> Klf200::getSocket() {
  std::lock_guard<std::mutex> socketGuard(_socketMutex);
  return _tcpSocket;
}

void Klf200::closeSocket() {
  std::shared_ptr<KlfTlsClient> socket;
  {
    std::lock_guard<std::mutex> socketGuard(_socketMutex);
    socket.swap(_tcpSocket);
  }
  //The socket object is destroyed as soon as no other thread uses it anymore. Every connection attempt uses a new socket object.
  if (socket) socket->shutdown();
}

bool Klf200::connect() {
  auto socket = std::make_shared<KlfTlsClient>(_hostname, (uint16_t)_port);
  {
    //Published before connecting, so stopListening() can abort a connection attempt. Nothing else uses the socket before
    //_stopped is reset.
    std::lock_guard<std::mutex> socketGuard(_socketMutex);
    _tcpSocket = socket;
  }
  try {
    socket->open(kConnectTimeout, kHandshakeTimeout);
  }
  catch (const std::exception &ex) {
    //Make sure a partially established connection (e. g. failed TLS handshake) doesn't block a socket on the KLF200.
    closeSocket();
    throw;
  }
  return socket->connected();
}

void Klf200::init() {
  try {
    {
      std::lock_guard<std::mutex> requestsGuard(_responsesMutex);
      _responses.clear();
      _responseCollections.clear();
    }

    {
      std::vector<uint8_t> payload;
      payload.reserve(32);
      payload.insert(payload.end(), _settings->password.begin(), _settings->password.end());
      payload.resize(32, 0);
      auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_PASSWORD_ENTER_REQ, payload);

      auto responsePacket = getResponse(VeluxCommand::GW_PASSWORD_ENTER_CFM, veluxPacket);
      if (!responsePacket || responsePacket->getPayload().empty() || responsePacket->getPayload().at(0) != 0) {
        _out.printError("Error: Could not login into KLF200. Please check your password.");
        _stopped = true;
        return;
      }
    }

    {
      std::vector<uint8_t> payload;
      auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_VERSION_REQ, payload);
      auto responsePacket = getResponse(VeluxCommand::GW_GET_VERSION_CFM, veluxPacket);
      if (!responsePacket || responsePacket->getPayload().size() < 9) {
        _out.printError("Error: Could not get version information from KLF200.");
        _stopped = true;
        return;
      }

      payload = responsePacket->getPayload();
      std::string version = std::to_string(payload.at(0)) + '.' + std::to_string(payload.at(1)) + '.' + std::to_string(payload.at(2)) + '.' + std::to_string(payload.at(3)) + '.' + std::to_string(payload.at(4)) + '.' + std::to_string(payload.at(5));
      std::string hardwareVersion = std::to_string(payload.at(6));

      if (payload.at(7) != 14 || payload.at(8) != 3) {
        _out.printError("Error: Server is no KLF200.");
        _stopped = true;
        return;
      }

      _out.printInfo("Info: Successfully connected to KLF200. Software version: " + version + "; hardware version: " + hardwareVersion);
    }

    {
      std::vector<uint8_t> payload;
      auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_PROTOCOL_VERSION_REQ, payload);
      auto responsePacket = getResponse(VeluxCommand::GW_GET_PROTOCOL_VERSION_CFM, veluxPacket);
      if (!responsePacket || responsePacket->getPayload().size() < 4) {
        _out.printError("Error: Could not get protocol version from KLF200.");
        _stopped = true;
        return;
      }

      payload = responsePacket->getPayload();
      std::string protocolVersion = std::to_string((((uint16_t)payload.at(0)) << 8) | payload.at(1)) + '.' + std::to_string((((uint16_t)payload.at(2)) << 8) | payload.at(3));

      _out.printInfo("Info: Protocol version: " + protocolVersion);
    }

    {
      std::vector<uint8_t> payload;
      auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_HOUSE_STATUS_MONITOR_ENABLE_REQ, payload);
      auto responsePacket = getResponse(VeluxCommand::GW_HOUSE_STATUS_MONITOR_ENABLE_CFM, veluxPacket);
      if (!responsePacket) {
        _out.printError("Error: Could not enable house status monitor on KLF200.");
        _stopped = true;
        return;
      }
    }

    {
      std::vector<uint8_t> payload;
      payload.reserve(4);
      int64_t time = BaseLib::HelperFunctions::getTimeSeconds();
      payload.push_back((time >> 24) & 0xFF);
      payload.push_back((time >> 16) & 0xFF);
      payload.push_back((time >> 8) & 0xFF);
      payload.push_back(time & 0xFF);
      auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_SET_UTC_REQ, payload);
      auto responsePacket = getResponse(VeluxCommand::GW_SET_UTC_CFM, veluxPacket);
      if (!responsePacket) {
        _out.printError("Error: Could not set time on KLF200.");
        _stopped = true;
        return;
      }
    }

    {
      std::vector<uint8_t> payload;
      auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_STATE_REQ, payload);
      auto responsePacket = getResponse(VeluxCommand::GW_GET_STATE_CFM, veluxPacket);
      if (!responsePacket || responsePacket->getPayload().size() < 6) {
        _out.printError("Error: Could not get state of KLF200.");
        _stopped = true;
        return;
      }

      payload = responsePacket->getPayload();
      auto state = payload.at(0);
      if (state != 2) {
        _out.printWarning("Warning: KLF200 is not configured as a gateway or no nodes are paired to it (state: " + std::to_string(state) + ").");
      }
    }

    _reconnectDelay = kMinReconnectDelay;
    _initComplete = true;

    _out.printInfo("Info: Initialization complete.");
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Klf200::checkHeartbeat() {
  try {
    if (_stopped || !_initComplete) return;

    auto time = BaseLib::HelperFunctions::getTime();
    if (_lastPacketReceived < _lastHeartbeat) {
      //Nothing received since the last keep-alive request was sent.
      if (time - _lastHeartbeat > kHeartbeatTimeout) {
        _out.printError("Error: KLF200 did not respond to keep-alive request within " + std::to_string(kHeartbeatTimeout / 1000) + " seconds.");
        _stopped = true;
      }
      return;
    }

    if (time - _lastHeartbeat < kHeartbeatInterval) return;
    _lastHeartbeat = time;

    //Only send the request without waiting for the response. This is called from the listen thread, which needs to read the response.
    std::vector<uint8_t> payload;
    auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_STATE_REQ, payload);
    sendSlipPacket(slipEncode(veluxPacket->getBinary()));
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void Klf200::listen() {
  try {
    std::vector<uint8_t> buffer(1024);
    std::vector<uint8_t> buffer2;
    buffer2.reserve(1024);
    bool escape = false;
    while (!_stopCallbackThread) {
      try {
        auto socket = getSocket();
        if (_stopped || !socket || !socket->connected()) {
          int32_t reconnectDelay = _reconnectDelay;
          if (socket) _out.printWarning("Warning: Connection to device closed. Trying to reconnect in " + std::to_string(reconnectDelay) + " seconds...");
          //Pending requests return as soon as _stopped is set.
          _stopped = true;
          _initComplete = false;
          socket.reset();
          closeSocket();
          buffer2.clear();
          escape = false;

          for (int32_t i = 0; i < reconnectDelay && !_stopCallbackThread; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          }
          if (_stopCallbackThread) break;
          _reconnectDelay = std::min(std::max(reconnectDelay * 2, kMinReconnectDelay), kMaxReconnectDelay);

          //Make sure the initialization of the previous connection has finished before the new connection is used.
          _bl->threadManager.join(_initThread);

          try {
            if (connect()) {
              _out.printInfo("Info: Successfully connected.");
              _lastHeartbeat = BaseLib::HelperFunctions::getTime();
              _stopped = false;
              _bl->threadManager.start(_initThread, true, &Klf200::init, this);
            } else _out.printError("Error: Could not connect to KLF200.");
          }
          catch (const std::exception &ex) {
            _out.printError("Error: Could not connect to KLF200: " + std::string(ex.what()));
          }
          continue;
        }

        size_t bytesRead = 0;
        try {
          bytesRead = socket->read(buffer.data(), buffer.size(), kSocketTimeout);
        }
        catch (const KlfSocketTimeoutException &ex) {
          checkHeartbeat();
          continue;
        }
        if (bytesRead > buffer.size()) bytesRead = buffer.size();

        if (GD::bl->debugLevel >= 5) _out.printDebug("Debug: TCP packet received: " + BaseLib::HelperFunctions::getHexString(buffer.data(), bytesRead));

        for (size_t i = 0; i < bytesRead; i++) {
          uint8_t byte = buffer.at(i);
          if (byte == 0xC0) {
            escape = false;
            if (!buffer2.empty()) {
              processPacket(buffer2);
              buffer2.clear();
            }
            continue;
          } else if (escape) {
            escape = false;
            if (byte == 0xDC) byte = 0xC0;
            else if (byte == 0xDD) byte = 0xDB;
            //Other bytes are a protocol violation. As recommended by RFC 1055 they are kept as they are.
          } else if (byte == 0xDB) {
            escape = true;
            continue;
          }

          if (buffer2.size() >= kMaxFrameSize) {
            _out.printWarning("Warning: Received frame is too long. Discarding data: " + BaseLib::HelperFunctions::getHexString(buffer2));
            buffer2.clear();
          }
          buffer2.push_back(byte);
        }

        checkHeartbeat();
      }
      catch (const KlfSocketClosedException &ex) {
        _stopped = true;
        if (!_stopCallbackThread) _out.printWarning("Warning: Connection closed by KLF200: " + std::string(ex.what()));
      }
      catch (const KlfSocketException &ex) {
        _stopped = true;
        if (!_stopCallbackThread) _out.printError("Error reading from KLF200: " + std::string(ex.what()));
      }
      catch (const std::exception &ex) {
        _stopped = true;
        if (!_stopCallbackThread) _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

std::vector<uint8_t> Klf200::slipEncode(const std::vector<uint8_t> &data) {
  try {
    std::vector<uint8_t> result;
    result.reserve(data.size() * 120 / 100); //Assume a maximum of 20% size increase

    result.push_back(0xC0); //SLIP start
    for (auto byte: data) {
      if (byte == 0xC0) {
        //Escape start byte
        result.push_back(0xDB);
        result.push_back(0xDC);
      } else if (byte == 0xDB) {
        //Escape escape byte
        result.push_back(0xDB);
        result.push_back(0xDD);
      } else result.push_back(byte);
    }
    result.push_back(0xC0); //SLIP end
    return result;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::vector<uint8_t>();
}

bool Klf200::sendSlipPacket(const std::vector<uint8_t> &slipPacket) {
  auto socket = getSocket();
  if (!socket) return false;

  try {
    std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
    _out.printInfo("Info: Sending packet " + getLogString(slipPacket));
    socket->send(slipPacket.data(), slipPacket.size(), kSocketTimeout);
    _lastPacketSent = BaseLib::HelperFunctions::getTime();
    return true;
  }
  catch (const KlfSocketException &ex) {
    _out.printError("Error sending packet: " + std::string(ex.what()));
    //The connection is broken. listen() reconnects.
    _stopped = true;
  }
  return false;
}

void Klf200::processPacket(std::vector<uint8_t> &data) {
  try {
    auto veluxPacket = std::make_shared<VeluxPacket>(data);
    _lastPacketReceived = BaseLib::HelperFunctions::getTime();

    std::unique_lock<std::mutex> requestsGuard(_responsesMutex);
    auto responsesIterator = _responses.find(veluxPacket->getCommand());
    if (responsesIterator != _responses.end()) {
      auto request = responsesIterator->second;
      requestsGuard.unlock();
      {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->response = veluxPacket;
        request->mutexReady = true;
      }
      request->conditionVariable.notify_one();
      return;
    }

    auto responseCollectionsIterator = _responseCollections.find(veluxPacket->getCommand());
    if (responseCollectionsIterator != _responseCollections.end()) {
      responseCollectionsIterator->second.push_back(veluxPacket);
      return;
    }
    requestsGuard.unlock();

    if (veluxPacket->getCommand() == VeluxCommand::GW_ERROR_NTF) {
      _out.printWarning("Warning: KLF200 reported error " + getErrorString(veluxPacket) + ".");
      return;
    }
    if (veluxPacket->getCommand() == VeluxCommand::GW_GET_STATE_CFM) return; //Response to keep-alive request

    raisePacketReceived(veluxPacket);
  }
  catch (const InvalidVeluxPacketException &ex) {
    _out.printWarning("Warning: Invalid packet received (" + std::string(ex.what()) + "): " + BaseLib::HelperFunctions::getHexString(data));
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

bool Klf200::waitForResponse(const std::shared_ptr<Request> &request, int32_t waitForSeconds) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(waitForSeconds);
  std::unique_lock<std::mutex> lock(request->mutex);
  while (!request->mutexReady && !_stopped) {
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline) break;
    //Wake up at least once per second to react to _stopped.
    request->conditionVariable.wait_until(lock, std::min(deadline, now + std::chrono::seconds(1)));
  }
  return request->mutexReady;
}

void Klf200::removeResponseHandler(VeluxCommand command, const std::shared_ptr<Request> &request) {
  std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
  auto responsesIterator = _responses.find(command);
  if (responsesIterator != _responses.end() && responsesIterator->second == request) _responses.erase(responsesIterator);
}

PVeluxPacket Klf200::getResponse(VeluxCommand responseCommand, const PVeluxPacket &requestPacket, int32_t waitForSeconds) {
  try {
    if (_stopped) return PVeluxPacket();

    std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
    std::shared_ptr<Request> request = std::make_shared<Request>();
    {
      std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
      _responses[responseCommand] = request;
      //The KLF200 might answer with GW_ERROR_NTF instead of the confirmation (API chapter 4.6).
      _responses[VeluxCommand::GW_ERROR_NTF] = request;
    }

    auto slipPacket = slipEncode(requestPacket->getBinary());
    bool sent = sendSlipPacket(slipPacket);
    if (sent) waitForResponse(request, waitForSeconds);

    removeResponseHandler(responseCommand, request);
    removeResponseHandler(VeluxCommand::GW_ERROR_NTF, request);

    if (!sent) return PVeluxPacket();

    PVeluxPacket responsePacket;
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      responsePacket = request->response;
    }

    if (!responsePacket) {
      if (!_stopped) _out.printError("Error: No response received to packet: " + getLogString(slipPacket));
      return PVeluxPacket();
    }

    if (responsePacket->getCommand() == VeluxCommand::GW_ERROR_NTF) {
      _out.printError("Error: KLF200 answered packet " + getLogString(slipPacket) + " with error " + getErrorString(responsePacket) + ".");
      return PVeluxPacket();
    }

    return responsePacket;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return PVeluxPacket();
}

std::pair<PVeluxPacket, std::list<PVeluxPacket>> Klf200::getMultipleResponses(VeluxCommand responseCommand, VeluxCommand notificationCommand, VeluxCommand finishedCommand, const PVeluxPacket &requestPacket, int32_t waitForSeconds) {
  std::pair<PVeluxPacket, std::list<PVeluxPacket>> returnValue;
  try {
    if (_stopped) return returnValue;

    std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
    std::shared_ptr<Request> request = std::make_shared<Request>();
    std::shared_ptr<Request> finishedRequest = std::make_shared<Request>();
    {
      std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
      _responses[responseCommand] = request;
      _responses[VeluxCommand::GW_ERROR_NTF] = request;
      _responses[finishedCommand] = finishedRequest;
      _responseCollections[notificationCommand] = std::list<PVeluxPacket>();
    }

    auto slipPacket = slipEncode(requestPacket->getBinary());
    if (sendSlipPacket(slipPacket)) {
      waitForResponse(request, 15);

      PVeluxPacket responsePacket;
      {
        std::lock_guard<std::mutex> lock(request->mutex);
        responsePacket = request->response;
      }

      if (!responsePacket) {
        if (!_stopped) _out.printError("Error: No response received to packet: " + getLogString(slipPacket));
      } else if (responsePacket->getCommand() == VeluxCommand::GW_ERROR_NTF) {
        _out.printError("Error: KLF200 answered packet " + getLogString(slipPacket) + " with error " + getErrorString(responsePacket) + ".");
      } else {
        returnValue.first = responsePacket;
        if (!waitForResponse(finishedRequest, waitForSeconds)) {
          _out.printWarning("Warning: No \"finished\" response received to packet: " + getLogString(slipPacket));
        }
      }
    }

    removeResponseHandler(responseCommand, request);
    removeResponseHandler(VeluxCommand::GW_ERROR_NTF, request);
    removeResponseHandler(finishedCommand, finishedRequest);
    {
      std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
      if (returnValue.first) returnValue.second = std::move(_responseCollections[notificationCommand]);
      _responseCollections.erase(notificationCommand);
    }

    return returnValue;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::pair<PVeluxPacket, std::list<PVeluxPacket>>();
}

std::pair<PVeluxPacket, std::list<PVeluxPacket>> Klf200::getMultipleResponses(VeluxCommand responseCommand, VeluxCommand notificationCommand, int32_t remainingPacketsByte, const PVeluxPacket &requestPacket, int32_t waitForSeconds, int32_t totalObjectsByte) {
  std::pair<PVeluxPacket, std::list<PVeluxPacket>> returnValue;
  try {
    if (_stopped) return returnValue;

    std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
    std::shared_ptr<Request> request = std::make_shared<Request>();
    {
      std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
      _responses[responseCommand] = request;
      _responses[VeluxCommand::GW_ERROR_NTF] = request;
      _responseCollections[notificationCommand] = std::list<PVeluxPacket>();
    }

    auto slipPacket = slipEncode(requestPacket->getBinary());
    if (sendSlipPacket(slipPacket)) {
      waitForResponse(request, 15);

      PVeluxPacket responsePacket;
      {
        std::lock_guard<std::mutex> lock(request->mutex);
        responsePacket = request->response;
      }

      if (!responsePacket) {
        if (!_stopped) _out.printError("Error: No response received to packet: " + getLogString(slipPacket));
      } else if (responsePacket->getCommand() == VeluxCommand::GW_ERROR_NTF) {
        _out.printError("Error: KLF200 answered packet " + getLogString(slipPacket) + " with error " + getErrorString(responsePacket) + ".");
      } else {
        returnValue.first = responsePacket;

        //When the confirmation reports zero objects, the KLF200 (firmware 0.2.0.0.71) doesn't send any notification.
        auto responsePayload = responsePacket->getPayload();
        bool noObjects = totalObjectsByte >= 0 && totalObjectsByte < (signed)responsePayload.size() && responsePayload.at(totalObjectsByte) == 0;

        int32_t remainingPackets = noObjects ? 0 : -1;
        auto deadline = noObjects ? std::chrono::steady_clock::now() : std::chrono::steady_clock::now() + std::chrono::seconds(waitForSeconds);
        while (!_stopped && std::chrono::steady_clock::now() < deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
          std::list<PVeluxPacket> &collection = _responseCollections[notificationCommand];
          if (collection.empty()) continue;
          auto payload = collection.back()->getPayload();
          //A negative index counts from the end of the payload.
          int32_t index = remainingPacketsByte < 0 ? (int32_t)payload.size() + remainingPacketsByte : remainingPacketsByte;
          if (index >= 0 && index < (signed)payload.size()) {
            remainingPackets = payload.at(index);
            if (remainingPackets == 0) break;
          }
        }

        if (remainingPackets != 0) {
          _out.printWarning("Warning: Not all response packets have been received before timeout for request: " + getLogString(slipPacket));
        }
      }
    }

    removeResponseHandler(responseCommand, request);
    removeResponseHandler(VeluxCommand::GW_ERROR_NTF, request);
    {
      std::lock_guard<std::mutex> responsesGuard(_responsesMutex);
      if (returnValue.first) returnValue.second = std::move(_responseCollections[notificationCommand]);
      _responseCollections.erase(notificationCommand);
    }

    return returnValue;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::pair<PVeluxPacket, std::list<PVeluxPacket>>();
}

std::list<PVeluxPacket> Klf200::getNodeInfo() {
  try {
    std::vector<uint8_t> payload;
    auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_ALL_NODES_INFORMATION_REQ, payload);
    auto result = getMultipleResponses(VeluxCommand::GW_GET_ALL_NODES_INFORMATION_CFM, VeluxCommand::GW_GET_ALL_NODES_INFORMATION_NTF, VeluxCommand::GW_GET_ALL_NODES_INFORMATION_FINISHED_NTF, veluxPacket);
    if (!result.first || result.first->getPayload().size() < 2) {
      _out.printError("Error: Could not get nodes from KLF200.");
      return std::list<PVeluxPacket>();
    }

    payload = result.first->getPayload();
    auto state = payload.at(0);
    auto nodeCount = payload.at(1);
    if (state == 1) {
      _out.printInfo("Info: Node table is empty.");
    }

    if (result.second.size() != nodeCount) _out.printWarning("Warning: Expected to receive information for " + std::to_string(nodeCount) + " nodes, but only received information for " + std::to_string(result.second.size()) + " nodes.");

    return result.second;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::list<PVeluxPacket>();
}

std::list<PVeluxPacket> Klf200::getSceneInfo() {
  try {
    std::vector<uint8_t> payload;
    auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_SCENE_LIST_REQ, payload);
    auto result = getMultipleResponses(VeluxCommand::GW_GET_SCENE_LIST_CFM, VeluxCommand::GW_GET_SCENE_LIST_NTF, -1, veluxPacket, 15, 0);
    //GW_GET_SCENE_LIST_CFM only contains TotalNumberOfObjects (API chapter 11.7.2).
    if (!result.first || result.first->getPayload().empty()) {
      _out.printError("Error: Could not get scenes from KLF200.");
      return std::list<PVeluxPacket>();
    }

    return result.second;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return std::list<PVeluxPacket>();
}

}
