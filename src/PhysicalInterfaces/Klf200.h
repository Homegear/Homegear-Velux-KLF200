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

#ifndef KLF200_H
#define KLF200_H

#include "../VeluxPacket.h"

namespace Velux
{

class Klf200 : public BaseLib::Systems::IPhysicalInterface
{
public:
    explicit Klf200(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
    ~Klf200() override;
    void startListening() override;
    void stopListening() override;
    void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet) override;
    bool isOpen() override { return !_stopped; }
    std::list<PVeluxPacket> getNodeInfo();
    uint16_t getMessageCounter();
protected:
    struct Request
    {
        std::mutex mutex;
        std::condition_variable conditionVariable;
        bool mutexReady = false;
        PVeluxPacket response;
    };

    BaseLib::Output _out;
    int32_t _port = 51200;
    std::shared_ptr<BaseLib::TcpSocket> _tcpSocket;

    std::atomic<uint16_t> _messageCounter{ 0 };

    std::thread _initThread;
    std::thread _heartbeatThread;

    int64_t _lastHeartBeat;

    std::mutex _sendPacketMutex;
    std::mutex _getResponseMutex;
    std::mutex _responsesMutex;
    std::unordered_map<VeluxCommand, std::shared_ptr<Request>> _responses;
    std::unordered_map<VeluxCommand, std::list<PVeluxPacket>> _responseCollections;


    void listen();
    void init();
    void heartbeat();

    std::vector<uint8_t> slipEncode(const std::vector<uint8_t>& data);
    void processPacket(std::vector<uint8_t>& data);
    PVeluxPacket getResponse(VeluxCommand responseCommand, const PVeluxPacket& requestPacket, int32_t waitForSeconds = 15);
    std::pair<PVeluxPacket, std::list<PVeluxPacket>> getMultipleResponses(VeluxCommand responseCommand, VeluxCommand notificationCommand, VeluxCommand finishedCommand, const PVeluxPacket& requestPacket, int32_t waitForSeconds = 15);
};

}
#endif
