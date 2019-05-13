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

namespace Velux
{

Klf200::Klf200(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(GD::bl, GD::family->getFamily(), settings)
{
    _out.init(GD::bl);
    _out.setPrefix(GD::out.getPrefix() + "KLF200 \"" + settings->id + "\": ");

    signal(SIGPIPE, SIG_IGN);

    _stopped = true;

    if(!settings)
    {
        _out.printCritical("Critical: Error initializing. Settings pointer is empty.");
        return;
    }
    _hostname = settings->host;
    _port = BaseLib::Math::getNumber(settings->port);
    if(_port < 1 || _port > 65535) _port = 51200;
}

Klf200::~Klf200()
{
    stopListening();
    _bl->threadManager.join(_initThread);
    _bl->threadManager.join(_heartbeatThread);
}

uint16_t Klf200::getMessageCounter()
{
    return _messageCounter++;
}

void Klf200::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
    try
    {
        PVeluxPacket veluxPacket(std::dynamic_pointer_cast<VeluxPacket>(packet));
        if(!veluxPacket) return;

        auto response = getResponse(veluxPacket->getResponseCommand(), veluxPacket);
        if(!response) _out.printError("Error sending packet " + BaseLib::HelperFunctions::getHexString(veluxPacket->getBinary()));

        _lastPacketSent = BaseLib::HelperFunctions::getTime();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Klf200::startListening()
{
    try
    {
        stopListening();

        if(_hostname.empty())
        {
            _out.printError("Error: Configuration of KLF200 is incomplete (hostname is missing). Please correct it in \"veluxklf200.conf\".");
            return;
        }

        if(_settings->password.empty())
        {
            _out.printError("Error: Configuration of KLF200 is incomplete (password is missing). Please correct it in \"veluxklf200.conf\".");
            return;
        }

        _tcpSocket = std::make_shared<BaseLib::TcpSocket>(_bl, _hostname, std::to_string(_port), true, std::string(), false);
        _tcpSocket->setConnectionRetries(1);
        _tcpSocket->setReadTimeout(1000000);
        _tcpSocket->setWriteTimeout(1000000);
        _stopCallbackThread = false;
        if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Klf200::listen, this);
        else _bl->threadManager.start(_listenThread, true, &Klf200::listen, this);
        IPhysicalInterface::startListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Klf200::stopListening()
{
    try
    {
        _stopCallbackThread = true;
        if(_tcpSocket) _tcpSocket->close();
        _bl->threadManager.join(_listenThread);
        _stopped = true;
        IPhysicalInterface::stopListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Klf200::init()
{
    try
    {
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
            if(!responsePacket || responsePacket->getPayload().at(0) == 1)
            {
                _out.printError("Error: Could not login into KLF200. Please check your password.");
                _stopped = true;
                return;
            }
        }

        {
            std::vector<uint8_t> payload;
            auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_VERSION_REQ, payload);
            auto responsePacket = getResponse(VeluxCommand::GW_GET_VERSION_CFM, veluxPacket);
            if(!responsePacket || responsePacket->getPayload().size() < 9)
            {
                _out.printError("Error: Could not get version information from KLF200.");
                _stopped = true;
                return;
            }

            payload = responsePacket->getPayload();
            std::string version = std::to_string(payload.at(0)) + '.' + std::to_string(payload.at(1)) + '.' + std::to_string(payload.at(2)) + '.' + std::to_string(payload.at(3)) + '.' + std::to_string(payload.at(4)) + '.' + std::to_string(payload.at(5));
            std::string hardwareVersion = std::to_string(payload.at(6));

            if(payload.at(7) != 14 || payload.at(8) != 3)
            {
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
            if(!responsePacket || responsePacket->getPayload().size() < 4)
            {
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
            if(!responsePacket)
            {
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
            if(!responsePacket)
            {
                _out.printError("Error: Could not set time on KLF200.");
                _stopped = true;
                return;
            }
        }

        {
            std::vector<uint8_t> payload;
            auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_STATE_REQ, payload);
            auto responsePacket = getResponse(VeluxCommand::GW_GET_STATE_CFM, veluxPacket);
            if(!responsePacket || responsePacket->getPayload().size() < 6)
            {
                _out.printError("Error: Could get state of KLF200.");
                _stopped = true;
                return;
            }

            payload = responsePacket->getPayload();
            auto state = payload.at(0);
            if(state != 2)
            {
                _out.printWarning("Warning: KLF200 is not configured as a gateway or no nodes are paired to it (state: " + std::to_string(state) + ").");
            }
        }

        _lastHeartBeat = BaseLib::HelperFunctions::getTime();

        _out.printInfo("Info: Initialization complete.");
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Klf200::heartbeat()
{
    try
    {
        std::vector<uint8_t> payload;
        auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_STATE_REQ, payload);
        auto responsePacket = getResponse(VeluxCommand::GW_GET_STATE_CFM, veluxPacket, 60);
        if(!responsePacket)
        {
            _out.printError("Error: Could get state of KLF200.");
            _stopped = true;
            return;
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Klf200::listen()
{
    try
    {
        try
        {
            _tcpSocket->open();
            if(_tcpSocket->connected())
            {
                _out.printInfo("Info: Successfully connected.");
                _stopped = false;
                _bl->threadManager.start(_initThread, true, &Klf200::init, this);
            }
            _lastHeartBeat = BaseLib::HelperFunctions::getTime();
        }
        catch(const std::exception& ex)
        {
            _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }

        std::vector<uint8_t> buffer(1024);
        std::vector<uint8_t> buffer2;
        buffer2.reserve(1024);
        bool escape = false;
        while(!_stopCallbackThread)
        {
            try
            {
                if(_stopped || !_tcpSocket->connected())
                {
                    if(_stopCallbackThread) return;
                    if(_stopped) _out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
                    _tcpSocket->close();
                    for(int32_t i = 0; i < 15; i++)
                    {
                        if(_stopCallbackThread) continue;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    }
                    _tcpSocket->open();
                    if(_tcpSocket->connected())
                    {
                        _out.printInfo("Info: Successfully connected.");
                        _stopped = false;
                        _bl->threadManager.start(_initThread, true, &Klf200::init, this);
                    }
                    _lastHeartBeat = BaseLib::HelperFunctions::getTime();
                    continue;
                }

                int32_t bytesRead = 0;
                try
                {
                    bytesRead = _tcpSocket->proofread((char*)buffer.data(), buffer.size());
                }
                catch(BaseLib::SocketTimeOutException& ex)
                {
                    if(_stopCallbackThread) continue;
                    if(BaseLib::HelperFunctions::getTime() - _lastHeartBeat > 15000)
                    {
                        _lastHeartBeat = BaseLib::HelperFunctions::getTime();
                        _bl->threadManager.start(_heartbeatThread, false, &Klf200::heartbeat, this);
                    }
                    continue;
                }
                if(bytesRead <= 0) continue;
                if(bytesRead > 1024) bytesRead = 1024;

                if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: TCP packet received: " + BaseLib::HelperFunctions::getHexString(buffer.data(), bytesRead));

                for(int32_t i = 0; i < bytesRead; i++)
                {
                    if(buffer.at(i) == 0xC0)
                    {
                        escape = false;
                        if(buffer2.empty()) continue;
                        else
                        {
                            processPacket(buffer2);
                            buffer2.clear();
                            continue;
                        }
                    }
                    else if(buffer.at(i) == 0xDB)
                    {
                        escape = true;
                    }
                    else if(escape)
                    {
                        if(buffer.at(i) == 0xDC) buffer2.push_back(0xC0);
                        else if(buffer.at(i) == 0xDD) buffer2.push_back(0xDB);
                    }
                    else buffer2.push_back(buffer.at(i));
                }
            }
            catch(const std::exception& ex)
            {
                _stopped = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::vector<uint8_t> Klf200::slipEncode(const std::vector<uint8_t>& data)
{
    try
    {
        std::vector<uint8_t> result;
        result.reserve(data.size() * 120 / 100); //Assume a maximum of 20% size increase

        result.push_back(0xC0); //SLIP start
        for(auto byte : data)
        {
            if(byte == 0xC0)
            {
                //Escape start byte
                result.push_back(0xDB);
                result.push_back(0xDC);
            }
            else if(byte == 0xDB)
            {
                //Escape escape byte
                result.push_back(0xDB);
                result.push_back(0xDD);
            }
            else result.push_back(byte);
        }
        result.push_back(0xC0); //SLIP end
        return result;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::vector<uint8_t>();
}

void Klf200::processPacket(std::vector<uint8_t>& data)
{
    try
    {
        auto veluxPacket = std::make_shared<VeluxPacket>(data);

        std::unique_lock<std::mutex> requestsGuard(_responsesMutex);
        auto responsesIterator = _responses.find(veluxPacket->getCommand());
        auto responseCollectionsIterator = _responseCollections.find(veluxPacket->getCommand());
        if(responsesIterator != _responses.end())
        {
            auto request = responsesIterator->second;
            requestsGuard.unlock();
            request->response = veluxPacket;
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->mutexReady = true;
            }
            request->conditionVariable.notify_one();
            return;
        }
        else if(responseCollectionsIterator != _responseCollections.end())
        {
            responseCollectionsIterator->second.push_back(veluxPacket);
            requestsGuard.unlock();
            return;
        }
        else requestsGuard.unlock();

        raisePacketReceived(veluxPacket);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

PVeluxPacket Klf200::getResponse(VeluxCommand responseCommand, const PVeluxPacket& requestPacket, int32_t waitForSeconds)
{
    try
    {
        if(_stopped) return PVeluxPacket();

        std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
        std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
        std::shared_ptr<Request> request = std::make_shared<Request>();
        std::unique_lock<std::mutex> requestsGuard(_responsesMutex);
        _responses[responseCommand] = request;
        requestsGuard.unlock();
        std::unique_lock<std::mutex> lock(request->mutex);

        auto requestBinary = requestPacket->getBinary();
        auto slipPacket = slipEncode(requestBinary);

        try
        {
            GD::out.printInfo("Info: Sending packet " + BaseLib::HelperFunctions::getHexString(slipPacket));
            _tcpSocket->proofwrite((char*)slipPacket.data(), slipPacket.size());
        }
        catch(const BaseLib::SocketOperationException& ex)
        {
            _out.printError("Error sending packet: " + std::string(ex.what()));
            return PVeluxPacket();
        }

        int32_t i = 0;
        while(!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(1000), [&]
        {
            i++;
            return request->mutexReady || _stopped || i == waitForSeconds;
        }));

        if(i == waitForSeconds || !request->response)
        {
            _out.printError("Error: No response received to packet: " + BaseLib::HelperFunctions::getHexString(slipPacket));
            return PVeluxPacket();
        }

        auto responsePacket = request->response;

        requestsGuard.lock();
        _responses.erase(responseCommand);
        requestsGuard.unlock();

        return responsePacket;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return PVeluxPacket();
}

std::pair<PVeluxPacket, std::list<PVeluxPacket>> Klf200::getMultipleResponses(VeluxCommand responseCommand, VeluxCommand notificationCommand, VeluxCommand finishedCommand, const PVeluxPacket& requestPacket, int32_t waitForSeconds)
{
    try
    {
        std::pair<PVeluxPacket, std::list<PVeluxPacket>> returnValue;
        if(_stopped) return returnValue;

        std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
        std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
        std::shared_ptr<Request> request = std::make_shared<Request>();
        std::shared_ptr<Request> finishedRequest = std::make_shared<Request>();
        std::unique_lock<std::mutex> responsesGuard(_responsesMutex);
        _responses[responseCommand] = request;
        _responses[finishedCommand] = finishedRequest;
        _responseCollections[notificationCommand] = std::list<PVeluxPacket>();
        responsesGuard.unlock();
        std::unique_lock<std::mutex> lock(request->mutex);

        auto requestBinary = requestPacket->getBinary();
        auto slipPacket = slipEncode(requestBinary);

        {
            try
            {
                GD::out.printInfo("Info: Sending packet " + BaseLib::HelperFunctions::getHexString(slipPacket));
                _tcpSocket->proofwrite((char*) slipPacket.data(), slipPacket.size());
            }
            catch(const BaseLib::SocketOperationException& ex)
            {
                _out.printError("Error sending packet: " + std::string(ex.what()));
                return returnValue;
            }

            int32_t i = 0;
            while(!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(1000), [&]
            {
                i++;
                return request->mutexReady || _stopped || i == 15;
            }));

            if(i == 15 || !request->response)
            {
                _out.printError("Error: No response received to packet: " + BaseLib::HelperFunctions::getHexString(slipPacket));
                return returnValue;
            }

            returnValue.first = request->response;

            responsesGuard.lock();
            _responses.erase(responseCommand);
            responsesGuard.unlock();
        }

        {
            int32_t i = 0;
            while(!finishedRequest->conditionVariable.wait_for(lock, std::chrono::milliseconds(1000), [&]
            {
                i++;
                return finishedRequest->mutexReady || _stopped || i == waitForSeconds;
            }));

            if(i == waitForSeconds || !finishedRequest->response)
            {
                _out.printWarning("Warning: No \"finished\" response received to packet: " + BaseLib::HelperFunctions::getHexString(slipPacket));
            }

            responsesGuard.lock();
            returnValue.second = _responseCollections[notificationCommand];
            _responses.erase(finishedCommand);
            _responseCollections.erase(notificationCommand);
            responsesGuard.unlock();
        }

        return returnValue;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::pair<PVeluxPacket, std::list<PVeluxPacket>>();
}

std::pair<PVeluxPacket, std::list<PVeluxPacket>> Klf200::getMultipleResponses(VeluxCommand responseCommand, VeluxCommand notificationCommand, int32_t remainingPacketsByte, const PVeluxPacket& requestPacket, int32_t waitForSeconds)
{
    try
    {
        std::pair<PVeluxPacket, std::list<PVeluxPacket>> returnValue;
        if(_stopped) return returnValue;

        std::lock_guard<std::mutex> sendPacketGuard(_sendPacketMutex);
        std::lock_guard<std::mutex> getResponseGuard(_getResponseMutex);
        std::shared_ptr<Request> request = std::make_shared<Request>();
        std::shared_ptr<Request> finishedRequest = std::make_shared<Request>();
        std::unique_lock<std::mutex> responsesGuard(_responsesMutex);
        _responses[responseCommand] = request;
        _responseCollections[notificationCommand] = std::list<PVeluxPacket>();
        responsesGuard.unlock();
        std::unique_lock<std::mutex> lock(request->mutex);

        auto requestBinary = requestPacket->getBinary();
        auto slipPacket = slipEncode(requestBinary);

        {
            try
            {
                GD::out.printInfo("Info: Sending packet " + BaseLib::HelperFunctions::getHexString(slipPacket));
                _tcpSocket->proofwrite((char*) slipPacket.data(), slipPacket.size());
            }
            catch(const BaseLib::SocketOperationException& ex)
            {
                _out.printError("Error sending packet: " + std::string(ex.what()));
                return returnValue;
            }

            int32_t i = 0;
            while(!request->conditionVariable.wait_for(lock, std::chrono::milliseconds(1000), [&]
            {
                i++;
                return request->mutexReady || _stopped || i == 15;
            }));

            if(i == 15 || !request->response)
            {
                _out.printError("Error: No response received to packet: " + BaseLib::HelperFunctions::getHexString(slipPacket));
                return returnValue;
            }

            returnValue.first = request->response;

            responsesGuard.lock();
            _responses.erase(responseCommand);
            responsesGuard.unlock();
        }

        {
            int32_t i = 0;
            int32_t remainingPackets = 1;
            for(; i < waitForSeconds; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                responsesGuard.lock();
                std::list<PVeluxPacket>& collection = _responseCollections[notificationCommand];
                if(!collection.empty())
                {
                    auto payload = collection.back()->getPayload();
                    if(remainingPackets < 0)
                    {
                        if(payload.size() - remainingPackets >= 0)
                        {
                            remainingPackets = payload.at(payload.size() - remainingPackets);
                            if(remainingPackets == 0) break;
                        }
                    }
                    else if(remainingPacketsByte < payload.size())
                    {
                        remainingPackets = payload.at(remainingPacketsByte);
                        if(remainingPackets == 0) break;
                    }
                }
                responsesGuard.unlock();
            }

            if(remainingPackets != 0)
            {
                _out.printWarning("Warning: Not all response packets (" + std::to_string(remainingPackets) + " still missing) have been received before timeout for request: " + BaseLib::HelperFunctions::getHexString(slipPacket));
            }

            responsesGuard.lock();
            returnValue.second = _responseCollections[notificationCommand];
            _responseCollections.erase(notificationCommand);
            responsesGuard.unlock();
        }

        return returnValue;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::pair<PVeluxPacket, std::list<PVeluxPacket>>();
}

std::list<PVeluxPacket> Klf200::getNodeInfo()
{
    try
    {
        std::vector<uint8_t> payload;
        auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_ALL_NODES_INFORMATION_REQ, payload);
        auto result = getMultipleResponses(VeluxCommand::GW_GET_ALL_NODES_INFORMATION_CFM, VeluxCommand::GW_GET_ALL_NODES_INFORMATION_NTF, VeluxCommand::GW_GET_ALL_NODES_INFORMATION_FINISHED_NTF, veluxPacket);
        if(!result.first || result.first->getPayload().size() < 2)
        {
            _out.printError("Error: Could get nodes from KLF200.");
            _stopped = true;
            return std::list<PVeluxPacket>();
        }

        payload = result.first->getPayload();
        auto state = payload.at(0);
        auto nodeCount = payload.at(1);
        if(state == 1)
        {
            _out.printInfo("Info: Node table is empty.");
        }

        if(result.second.size() != nodeCount) _out.printWarning("Warning: Expected to receive information for " + std::to_string(nodeCount) + " nodes, but only received information for " + std::to_string(result.second.size()) + " nodes.");

        return result.second;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::list<PVeluxPacket>();
}

std::list<PVeluxPacket> Klf200::getSceneInfo()
{
    try
    {
        std::vector<uint8_t> payload;
        auto veluxPacket = std::make_shared<VeluxPacket>(VeluxCommand::GW_GET_SCENE_LIST_REQ, payload);
        auto result = getMultipleResponses(VeluxCommand::GW_GET_SCENE_LIST_CFM, VeluxCommand::GW_GET_SCENE_LIST_NTF, -1, veluxPacket);
        if(!result.first || result.first->getPayload().size() < 2)
        {
            _out.printError("Error: Could get scenes from KLF200.");
            _stopped = true;
            return std::list<PVeluxPacket>();
        }

        payload = result.first->getPayload();
        auto sceneCount = payload.at(0);

        return result.second;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::list<PVeluxPacket>();
}

}
