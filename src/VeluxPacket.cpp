#include <utility>

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

#include "VeluxPacket.h"
#include "GD.h"

namespace Velux
{

const std::unordered_map<VeluxCommand, VeluxCommand> VeluxPacket::_requestResponseMapping
{
    { VeluxCommand::GW_REBOOT_REQ, VeluxCommand::GW_REBOOT_CFM },
    { VeluxCommand::GW_SET_FACTORY_DEFAULT_REQ, VeluxCommand::GW_SET_FACTORY_DEFAULT_CFM },
    { VeluxCommand::GW_GET_VERSION_REQ, VeluxCommand::GW_GET_VERSION_CFM },
    { VeluxCommand::GW_GET_PROTOCOL_VERSION_REQ, VeluxCommand::GW_GET_PROTOCOL_VERSION_CFM },
    { VeluxCommand::GW_GET_STATE_REQ, VeluxCommand::GW_GET_STATE_CFM },
    { VeluxCommand::GW_LEAVE_LEARN_STATE_REQ, VeluxCommand::GW_LEAVE_LEARN_STATE_CFM },
    { VeluxCommand::GW_GET_NETWORK_SETUP_REQ, VeluxCommand::GW_GET_NETWORK_SETUP_CFM },
    { VeluxCommand::GW_SET_NETWORK_SETUP_REQ, VeluxCommand::GW_SET_FACTORY_DEFAULT_CFM },
    { VeluxCommand::GW_CS_GET_SYSTEMTABLE_DATA_REQ, VeluxCommand::GW_CS_GET_SYSTEMTABLE_DATA_CFM },
    { VeluxCommand::GW_CS_DISCOVER_NODES_REQ, VeluxCommand::GW_CS_DISCOVER_NODES_CFM },
    { VeluxCommand::GW_CS_REMOVE_NODES_REQ, VeluxCommand::GW_CS_REMOVE_NODES_CFM },
    { VeluxCommand::GW_CS_VIRGIN_STATE_REQ, VeluxCommand::GW_CS_VIRGIN_STATE_CFM },
    { VeluxCommand::GW_CS_CONTROLLER_COPY_REQ, VeluxCommand::GW_CS_CONTROLLER_COPY_CFM },
    { VeluxCommand::GW_CS_RECEIVE_KEY_REQ, VeluxCommand::GW_CS_RECEIVE_KEY_CFM },
    { VeluxCommand::GW_CS_GENERATE_NEW_KEY_REQ, VeluxCommand::GW_CS_GENERATE_NEW_KEY_CFM },
    { VeluxCommand::GW_CS_REPAIR_KEY_REQ, VeluxCommand::GW_CS_REPAIR_KEY_CFM },
    { VeluxCommand::GW_CS_ACTIVATE_CONFIGURATION_MODE_REQ, VeluxCommand::GW_CS_ACTIVATE_CONFIGURATION_MODE_CFM },
    { VeluxCommand::GW_GET_NODE_INFORMATION_REQ, VeluxCommand::GW_GET_NODE_INFORMATION_CFM },
    { VeluxCommand::GW_GET_ALL_NODES_INFORMATION_REQ, VeluxCommand::GW_GET_ALL_NODES_INFORMATION_CFM },
    { VeluxCommand::GW_SET_NODE_VARIATION_REQ, VeluxCommand::GW_SET_NODE_VARIATION_CFM },
    { VeluxCommand::GW_SET_NODE_NAME_REQ, VeluxCommand::GW_SET_NODE_NAME_CFM },
    { VeluxCommand::GW_SET_NODE_VELOCITY_REQ, VeluxCommand::GW_SET_NODE_VELOCITY_CFM },
    { VeluxCommand::GW_SET_NODE_ORDER_AND_PLACEMENT_REQ, VeluxCommand::GW_SET_NODE_ORDER_AND_PLACEMENT_CFM },
    { VeluxCommand::GW_GET_GROUP_INFORMATION_REQ, VeluxCommand::GW_GET_GROUP_INFORMATION_CFM },
    { VeluxCommand::GW_SET_GROUP_INFORMATION_REQ, VeluxCommand::GW_SET_GROUP_INFORMATION_CFM },
    { VeluxCommand::GW_DELETE_GROUP_REQ, VeluxCommand::GW_DELETE_GROUP_CFM },
    { VeluxCommand::GW_NEW_GROUP_REQ, VeluxCommand::GW_NEW_GROUP_CFM },
    { VeluxCommand::GW_GET_ALL_GROUPS_INFORMATION_REQ, VeluxCommand::GW_GET_ALL_GROUPS_INFORMATION_CFM },
    { VeluxCommand::GW_HOUSE_STATUS_MONITOR_ENABLE_REQ, VeluxCommand::GW_HOUSE_STATUS_MONITOR_ENABLE_CFM },
    { VeluxCommand::GW_HOUSE_STATUS_MONITOR_DISABLE_REQ, VeluxCommand::GW_HOUSE_STATUS_MONITOR_DISABLE_CFM },
    { VeluxCommand::GW_COMMAND_SEND_REQ, VeluxCommand::GW_COMMAND_SEND_CFM },
    { VeluxCommand::GW_STATUS_REQUEST_REQ, VeluxCommand::GW_STATUS_REQUEST_CFM },
    { VeluxCommand::GW_WINK_SEND_REQ, VeluxCommand::GW_WINK_SEND_CFM },
    { VeluxCommand::GW_SET_LIMITATION_REQ, VeluxCommand::GW_SET_LIMITATION_CFM },
    { VeluxCommand::GW_GET_LIMITATION_STATUS_REQ, VeluxCommand::GW_GET_LIMITATION_STATUS_CFM },
    { VeluxCommand::GW_MODE_SEND_REQ, VeluxCommand::GW_MODE_SEND_CFM },
    { VeluxCommand::GW_INITIALIZE_SCENE_REQ, VeluxCommand::GW_INITIALIZE_SCENE_CFM },
    { VeluxCommand::GW_INITIALIZE_SCENE_CANCEL_REQ, VeluxCommand::GW_INITIALIZE_SCENE_CANCEL_CFM },
    { VeluxCommand::GW_RECORD_SCENE_REQ, VeluxCommand::GW_RECORD_SCENE_CFM },
    { VeluxCommand::GW_DELETE_SCENE_REQ, VeluxCommand::GW_DELETE_SCENE_CFM },
    { VeluxCommand::GW_RENAME_SCENE_REQ, VeluxCommand::GW_RENAME_SCENE_CFM },
    { VeluxCommand::GW_GET_SCENE_LIST_REQ, VeluxCommand::GW_GET_SCENE_LIST_CFM },
    { VeluxCommand::GW_GET_SCENE_INFORMATION_REQ, VeluxCommand::GW_GET_SCENE_INFORMATION_CFM },
    { VeluxCommand::GW_ACTIVATE_SCENE_REQ, VeluxCommand::GW_ACTIVATE_SCENE_CFM },
    { VeluxCommand::GW_STOP_SCENE_REQ, VeluxCommand::GW_STOP_SCENE_CFM },
    { VeluxCommand::GW_ACTIVATE_PRODUCTGROUP_REQ, VeluxCommand::GW_ACTIVATE_PRODUCTGROUP_CFM },
    { VeluxCommand::GW_GET_CONTACT_INPUT_LINK_LIST_REQ, VeluxCommand::GW_GET_CONTACT_INPUT_LINK_LIST_CFM },
    { VeluxCommand::GW_SET_CONTACT_INPUT_LINK_REQ, VeluxCommand::GW_SET_CONTACT_INPUT_LINK_CFM },
    { VeluxCommand::GW_REMOVE_CONTACT_INPUT_LINK_REQ, VeluxCommand::GW_REMOVE_CONTACT_INPUT_LINK_CFM },
    { VeluxCommand::GW_GET_ACTIVATION_LOG_HEADER_REQ, VeluxCommand::GW_GET_ACTIVATION_LOG_HEADER_CFM },
    { VeluxCommand::GW_GET_MULTIPLE_ACTIVATION_LOG_LINES_REQ, VeluxCommand::GW_GET_MULTIPLE_ACTIVATION_LOG_LINES_CFM },
    { VeluxCommand::GW_SET_UTC_REQ, VeluxCommand::GW_SET_UTC_CFM },
    { VeluxCommand::GW_RTC_SET_TIME_ZONE_REQ, VeluxCommand::GW_RTC_SET_TIME_ZONE_CFM },
    { VeluxCommand::GW_GET_LOCAL_TIME_REQ, VeluxCommand::GW_GET_LOCAL_TIME_CFM },
    { VeluxCommand::GW_PASSWORD_ENTER_REQ, VeluxCommand::GW_PASSWORD_ENTER_CFM },
    { VeluxCommand::GW_PASSWORD_CHANGE_REQ, VeluxCommand::GW_PASSWORD_CHANGE_CFM }
};

VeluxPacket::VeluxPacket(const std::vector<uint8_t>& binaryPacket)
{
    _binaryPacket = binaryPacket;

    if(binaryPacket.size() < 4) throw InvalidVeluxPacketException("Packet too small");
    if(binaryPacket.at(0) != 0) throw InvalidVeluxPacketException("Invalid ProtocolID");

    _length = binaryPacket.at(1);
    if(binaryPacket.size() - 2 != _length) throw InvalidVeluxPacketException("Invalid length byte");

    uint8_t checksum = binaryPacket[0];
    for(int32_t i = 1; i < (signed)binaryPacket.size() - 1; i++)
    {
        checksum ^= binaryPacket[i];
    }
    if(checksum != binaryPacket.back()) throw InvalidVeluxPacketException("Invalid checksum");

    _command = (VeluxCommand)((((uint16_t)binaryPacket[2]) << 8) | binaryPacket[3]);
    if(binaryPacket.size() > 5) _payload = std::vector<uint8_t>(binaryPacket.begin() + 4, binaryPacket.end() - 1);

    setNodeId();
}

VeluxPacket::VeluxPacket(VeluxCommand command, std::vector<uint8_t>  payload) : _command(command), _payload(std::move(payload))
{
}

VeluxCommand VeluxPacket::getResponseCommand()
{
    auto iterator = _requestResponseMapping.find(_command);
    if(iterator != _requestResponseMapping.end()) return iterator->second;
    return VeluxCommand::UNSET;
}

void VeluxPacket::reset()
{
    _binaryPacket.clear();
    _command = VeluxCommand::UNSET;
    _payload.clear();
}

void VeluxPacket::setNodeId()
{
    switch(_command)
    {
        case VeluxCommand::GW_LIMITATION_STATUS_NTF:
            _nodeId = _payload.at(2);
            break;
        case VeluxCommand::GW_GET_NODE_INFORMATION_REQ:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_GET_NODE_INFORMATION_CFM:
            _nodeId = _payload.at(1);
            break;
        case VeluxCommand::GW_GET_NODE_INFORMATION_NTF:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_SET_NODE_VARIATION_REQ:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_SET_NODE_VARIATION_CFM:
            _nodeId = _payload.at(1);
            break;
        case VeluxCommand::GW_SET_NODE_NAME_REQ:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_SET_NODE_NAME_CFM:
            _nodeId = _payload.at(1);
            break;
        case VeluxCommand::GW_NODE_INFORMATION_CHANGED_NTF:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_NODE_STATE_POSITION_CHANGED_NTF:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_GET_ALL_NODES_INFORMATION_NTF:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_SET_NODE_ORDER_AND_PLACEMENT_REQ:
            _nodeId = _payload.at(0);
            break;
        case VeluxCommand::GW_SET_NODE_ORDER_AND_PLACEMENT_CFM:
            _nodeId = _payload.at(1);
            break;
        default:
            _nodeId = -1;
    }
}

std::vector<uint8_t> VeluxPacket::getBinary()
{
    if(!_binaryPacket.empty()) return _binaryPacket;

    _binaryPacket.reserve(_length + 2);
    _length = _payload.size() + 3;
    _binaryPacket.push_back(0);
    _binaryPacket.push_back(_length);
    _binaryPacket.push_back(((uint16_t)_command) >> 8);
    _binaryPacket.push_back(((uint16_t)_command) & 0xFF);
    if(!_payload.empty()) _binaryPacket.insert(_binaryPacket.end(), _payload.begin(), _payload.end());

    uint8_t checksum = _binaryPacket[0];
    for(int32_t i = 1; i < (signed)_binaryPacket.size(); i++)
    {
        checksum ^= _binaryPacket[i];
    }
    _binaryPacket.push_back(checksum);

    return _binaryPacket;
}

std::vector<uint8_t> VeluxPacket::getPosition(uint32_t position, uint32_t size)
{
    try
    {
        return BaseLib::BitReaderWriter::getPosition(_payload, position, size);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::vector<uint8_t>();
}

void VeluxPacket::setPosition(uint32_t position, uint32_t size, const std::vector<uint8_t>& source)
{
    try
    {
        std::vector<uint8_t> sourceCopy;
        sourceCopy.reserve(source.size());
        for(int32_t i = source.size() - 1; i >= 0; i--)
        {
            sourceCopy.push_back(source.at(i));
        }
        BaseLib::BitReaderWriter::setPosition(position, size, _payload, sourceCopy);
    }
    catch(const std::exception& ex)
    {
        GD::bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

}
