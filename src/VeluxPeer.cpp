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

#include "VeluxPeer.h"
#include "VeluxCentral.h"
#include "Velux.h"
#include "PhysicalInterfaces/Klf200.h"
#include "GD.h"

namespace Velux
{
std::shared_ptr<BaseLib::Systems::ICentral> VeluxPeer::getCentral()
{
	try
	{
		if(_central) return _central;
		_central = GD::family->getCentral();
		return _central;
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	return std::shared_ptr<BaseLib::Systems::ICentral>();
}

VeluxPeer::VeluxPeer(uint32_t parentID, IPeerEventSink* eventHandler) : Peer(GD::bl, parentID, eventHandler)
{
}

VeluxPeer::VeluxPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler) : Peer(GD::bl, id, address, serialNumber, parentID, eventHandler)
{
}

VeluxPeer::~VeluxPeer()
{
	dispose();
}

std::string VeluxPeer::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;

		if(command == "help")
		{
			stringStream << "List of commands:" << std::endl << std::endl;
			stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
			stringStream << "unselect\t\tUnselect this peer" << std::endl;
			return stringStream.str();
		}
		return "Unknown command.\n";
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return "Error executing command. See log file for more details.\n";
}

void VeluxPeer::save(bool savePeer, bool variables, bool centralConfig)
{
	try
	{
		Peer::save(savePeer, variables, centralConfig);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void VeluxPeer::setPhysicalInterfaceId(std::string id)
{
    if(id.empty() || (GD::physicalInterfaces.find(id) != GD::physicalInterfaces.end() && GD::physicalInterfaces.at(id)))
    {
        _physicalInterfaceId = id;
        setPhysicalInterface(id.empty() ? GD::defaultPhysicalInterface : GD::physicalInterfaces.at(_physicalInterfaceId));
        saveVariable(19, _physicalInterfaceId);
    }
    if(!_physicalInterface) _physicalInterface = GD::defaultPhysicalInterface;
}

void VeluxPeer::setPhysicalInterface(std::shared_ptr<Klf200> interface)
{
	try
	{
		if(!interface) return;
		_physicalInterface = interface;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void VeluxPeer::loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows)
{
	try
	{
		if(!rows) rows = _bl->db->getPeerVariables(_peerID);
		Peer::loadVariables(central, rows);

		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			switch(row->second.at(2)->intValue)
			{
			case 19:
                _physicalInterfaceId = row->second.at(4)->textValue;
                if(!_physicalInterfaceId.empty() && GD::physicalInterfaces.find(_physicalInterfaceId) != GD::physicalInterfaces.end()) setPhysicalInterface(GD::physicalInterfaces.at(_physicalInterfaceId));
                break;
			}
		}
		if(!_physicalInterface)
		{
			GD::out.printError("Error: Could not find correct physical interface for peer " + std::to_string(_peerID) + ". The peer might not work correctly. The expected interface ID is: " + _physicalInterfaceId);
            _physicalInterface = GD::defaultPhysicalInterface;
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

bool VeluxPeer::load(BaseLib::Systems::ICentral* central)
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows;
		loadVariables(central, rows);

		_rpcDevice = GD::family->getRpcDevices()->find(_deviceType, _firmwareVersion, -1);
		if(!_rpcDevice)
		{
			GD::out.printError("Error loading peer " + std::to_string(_peerID) + ": Device type not found: 0x" + BaseLib::HelperFunctions::getHexString(_deviceType) + " Firmware version: " + std::to_string(_firmwareVersion));
			return false;
		}
		initializeTypeString();
		std::string entry;
		loadConfig();
		initializeCentralConfig();

		serviceMessages.reset(new BaseLib::Systems::ServiceMessages(_bl, _peerID, _serialNumber, this));
		serviceMessages->load();

		return true;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void VeluxPeer::saveVariables()
{
	try
	{
		if(_peerID == 0) return;
		Peer::saveVariables();

		saveVariable(19, _physicalInterfaceId);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

PParameterGroup VeluxPeer::getParameterSet(int32_t channel, ParameterGroup::Type::Enum type)
{
	try
	{
		PParameterGroup parameterGroup = _rpcDevice->functions.at(channel)->getParameterGroup(type);
		if(!parameterGroup || parameterGroup->parameters.empty())
		{
			GD::out.printDebug("Debug: Parameter set of type " + std::to_string(type) + " not found for channel " + std::to_string(channel));
			return PParameterGroup();
		}
		return parameterGroup;
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	return PParameterGroup();
}

bool VeluxPeer::getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
	try
	{
		if(channel == 1)
		{
			if(parameter->id == "PEER_ID")
			{
				std::vector<uint8_t> parameterData;
				auto& rpcConfigurationParameter = valuesCentral[channel][parameter->id];
				parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), rpcConfigurationParameter.invert(), parameterData);
                rpcConfigurationParameter.setBinaryData(parameterData);
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return false;
}

void VeluxPeer::getValuesFromPacket(PVeluxPacket packet, std::vector<FrameValues>& frameValues)
{
    try
    {
        if(!_rpcDevice) return;
        //equal_range returns all elements with "0" or an unknown element as argument
        if(_rpcDevice->packetsByMessageType.find((uint32_t)packet->getCommand()) == _rpcDevice->packetsByMessageType.end()) return;
        std::pair<PacketsByMessageType::iterator, PacketsByMessageType::iterator> range = _rpcDevice->packetsByMessageType.equal_range((uint32_t)packet->getCommand());
        if(range.first == _rpcDevice->packetsByMessageType.end()) return;
        PacketsByMessageType::iterator i = range.first;
        do
        {
            FrameValues currentFrameValues;
            PPacket frame(i->second);
            if(!frame) continue;
            std::vector<uint8_t> payload = packet->getPayload();
            if(payload.empty()) break;
            uint32_t erpPacketBitSize = payload.size() * 8;
            int32_t channelIndex = frame->channelIndex;
            int32_t channel = -1;
            if(channelIndex >= 0 && channelIndex < (signed)payload.size()) channel = payload.at(channelIndex);
            if(channel > -1 && frame->channelSize < 8.0) channel &= (0xFF >> (8 - std::lround(frame->channelSize)));
            channel += frame->channelIndexOffset;
            if(frame->channel > -1) channel = frame->channel;
            if(channel == -1) continue;
            currentFrameValues.frameID = frame->id;
            bool abort = false;

            for(BinaryPayloads::iterator j = frame->binaryPayloads.begin(); j != frame->binaryPayloads.end(); ++j)
            {
                std::vector<uint8_t> data;
                if((*j)->bitSize > 0 && (*j)->bitIndex > 0)
                {
                    if((*j)->bitIndex >= erpPacketBitSize) continue;
                    data = packet->getPosition((*j)->bitIndex, (*j)->bitSize);

                    if((*j)->constValueInteger > -1)
                    {
                        int32_t intValue = 0;
                        _bl->hf.memcpyBigEndian(intValue, data);
                        if(intValue != (*j)->constValueInteger)
                        {
                            abort = true;
                            break;
                        }
                        else if((*j)->parameterId.empty()) continue;
                    }
                }
                else if((*j)->constValueInteger > -1)
                {
                    _bl->hf.memcpyBigEndian(data, (*j)->constValueInteger);
                }
                else continue;

                for(std::vector<PParameter>::iterator k = frame->associatedVariables.begin(); k != frame->associatedVariables.end(); ++k)
                {
                    if((*k)->physical->groupId != (*j)->parameterId) continue;
                    currentFrameValues.parameterSetType = (*k)->parent()->type();
                    bool setValues = false;
                    if(currentFrameValues.paramsetChannels.empty()) //Fill paramsetChannels
                    {
                        int32_t startChannel = (channel < 0) ? 0 : channel;
                        int32_t endChannel;
                        //When fixedChannel is -2 (means '*') cycle through all channels
                        if(frame->channel == -2)
                        {
                            startChannel = 0;
                            endChannel = _rpcDevice->functions.rbegin()->first;
                        }
                        else endChannel = startChannel;
                        for(int32_t l = startChannel; l <= endChannel; l++)
                        {
                            Functions::iterator functionIterator = _rpcDevice->functions.find(l);
                            if(functionIterator == _rpcDevice->functions.end()) continue;
                            PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(currentFrameValues.parameterSetType);
                            if(!parameterGroup || parameterGroup->parameters.find((*k)->id) == parameterGroup->parameters.end()) continue;
                            currentFrameValues.paramsetChannels.push_back(l);
                            currentFrameValues.values[(*k)->id].channels.push_back(l);
                            setValues = true;
                        }
                    }
                    else //Use paramsetChannels
                    {
                        for(std::list<uint32_t>::const_iterator l = currentFrameValues.paramsetChannels.begin(); l != currentFrameValues.paramsetChannels.end(); ++l)
                        {
                            Functions::iterator functionIterator = _rpcDevice->functions.find(*l);
                            if(functionIterator == _rpcDevice->functions.end()) continue;
                            PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(currentFrameValues.parameterSetType);
                            if(!parameterGroup || parameterGroup->parameters.find((*k)->id) == parameterGroup->parameters.end()) continue;
                            currentFrameValues.values[(*k)->id].channels.push_back(*l);
                            setValues = true;
                        }
                    }
                    if(setValues) currentFrameValues.values[(*k)->id].value = data;
                }
            }
            if(abort) continue;
            if(!currentFrameValues.values.empty()) frameValues.push_back(currentFrameValues);
        } while(++i != range.second && i != _rpcDevice->packetsByMessageType.end());
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void VeluxPeer::packetReceived(std::shared_ptr<VeluxPacket> packet)
{
    try
    {
        if(_disposing || !packet || !_rpcDevice) return;
        auto central = std::dynamic_pointer_cast<VeluxCentral>(getCentral());
        if(!central) return;
        setLastPacketReceived();
        serviceMessages->endUnreach();

        std::vector<FrameValues> frameValues;
        getValuesFromPacket(packet, frameValues);
        std::map<uint32_t, std::shared_ptr<std::vector<std::string>>> valueKeys;
        std::map<uint32_t, std::shared_ptr<std::vector<PVariable>>> rpcValues;

        //Loop through all matching frames
        for(std::vector<FrameValues>::iterator a = frameValues.begin(); a != frameValues.end(); ++a)
        {
            PPacket frame;
            if(!a->frameID.empty()) frame = _rpcDevice->packetsById.at(a->frameID);
            if(!frame) continue;

            for(std::map<std::string, FrameValue>::iterator i = a->values.begin(); i != a->values.end(); ++i)
            {
                for(std::list<uint32_t>::const_iterator j = a->paramsetChannels.begin(); j != a->paramsetChannels.end(); ++j)
                {
                    if(std::find(i->second.channels.begin(), i->second.channels.end(), *j) == i->second.channels.end()) continue;
                    if(!valueKeys[*j] || !rpcValues[*j])
                    {
                        valueKeys[*j].reset(new std::vector<std::string>());
                        rpcValues[*j].reset(new std::vector<PVariable>());
                    }

                    BaseLib::Systems::RpcConfigurationParameter& parameter = valuesCentral[*j][i->first];
                    parameter.setBinaryData(i->second.value);
                    if(parameter.databaseId > 0) saveParameter(parameter.databaseId, i->second.value);
                    else saveParameter(0, ParameterGroup::Type::Enum::variables, *j, i->first, i->second.value);
                    if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + i->first + " on channel " + std::to_string(*j) + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber  + " was set to 0x" + BaseLib::HelperFunctions::getHexString(i->second.value) + ".");

                    if(parameter.rpcParameter)
                    {
                        //Process service messages
                        if(parameter.rpcParameter->service && !i->second.value.empty())
                        {
                            if(parameter.rpcParameter->logical->type == ILogical::Type::Enum::tEnum)
                            {
                                serviceMessages->set(i->first, i->second.value.at(0), *j);
                            }
                            else if(parameter.rpcParameter->logical->type == ILogical::Type::Enum::tBoolean)
                            {
                                serviceMessages->set(i->first, parameter.rpcParameter->convertFromPacket(i->second.value, parameter.invert(), true)->booleanValue);
                            }
                        }

                        valueKeys[*j]->push_back(i->first);
                        rpcValues[*j]->push_back(parameter.rpcParameter->convertFromPacket(i->second.value, parameter.invert(), true));
                    }
                }
            }
        }

        if(!rpcValues.empty())
        {
            for(std::map<uint32_t, std::shared_ptr<std::vector<std::string>>>::iterator j = valueKeys.begin(); j != valueKeys.end(); ++j)
            {
                if(j->second->empty()) continue;
                std::string eventSource = "device-" + std::to_string(_peerID);
                std::string address(_serialNumber + ":" + std::to_string(j->first));
                raiseEvent(eventSource, _peerID, j->first, j->second, rpcValues.at(j->first));
                raiseRPCEvent(eventSource, _peerID, j->first, address, j->second, rpcValues.at(j->first));
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::string VeluxPeer::getFirmwareVersionString(int32_t firmwareVersion)
{
	try
	{
		return std::to_string(firmwareVersion);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
	return "";
}

//RPC Methods
PVariable VeluxPeer::getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields)
{
	try
	{
		PVariable info(Peer::getDeviceInfo(clientInfo, fields));
		if(info->errorStruct) return info;

		if(fields.empty() || fields.find("INTERFACE") != fields.end()) info->structValue->insert(StructElement("INTERFACE", PVariable(new Variable(_physicalInterface->getID()))));

		return info;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return PVariable();
}

PVariable VeluxPeer::getParamsetDescription(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(channel < 0) channel = 0;
		Functions::iterator functionIterator = _rpcDevice->functions.find(channel);
		if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel");
		PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
		if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set");

		return Peer::getParamsetDescription(clientInfo, channel, parameterGroup, checkAcls);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable VeluxPeer::putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(channel < 0) channel = 0;
		Functions::iterator functionIterator = _rpcDevice->functions.find(channel);
		if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel");
		PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
		if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set");
		if(variables->structValue->empty()) return PVariable(new Variable(VariableType::tVoid));

		auto central = getCentral();
		if(!central) return Variable::createError(-32500, "Could not get central.");

		if(type == ParameterGroup::Type::Enum::variables)
		{
			for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
			{
				if(i->first.empty() || !i->second) continue;

				if(checkAcls && !clientInfo->acls->checkVariableWriteAccess(central->getPeer(_peerID), channel, i->first)) continue;

				setValue(clientInfo, channel, i->first, i->second, true);
			}
		}
		else
		{
			return Variable::createError(-3, "Parameter set type is not supported.");
		}
		return std::make_shared<Variable>(VariableType::tVoid);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable VeluxPeer::getParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls)
{
	try
	{
		if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
		if(channel < 0) channel = 0;
		if(remoteChannel < 0) remoteChannel = 0;
		Functions::iterator functionIterator = _rpcDevice->functions.find(channel);
		if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel");
		PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
		if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set");
		PVariable variables(new Variable(VariableType::tStruct));

		auto central = getCentral();
		if(!central) return Variable::createError(-32500, "Could not get central.");

		for(Parameters::iterator i = parameterGroup->parameters.begin(); i != parameterGroup->parameters.end(); ++i)
		{
			if(i->second->id.empty()) continue;
			if(!i->second->visible && !i->second->service && !i->second->internal && !i->second->transform)
			{
				GD::out.printDebug("Debug: Omitting parameter " + i->second->id + " because of it's ui flag.");
				continue;
			}
			PVariable element;
			if(type == ParameterGroup::Type::Enum::variables)
			{
				if(checkAcls && !clientInfo->acls->checkVariableReadAccess(central->getPeer(_peerID), channel, i->first)) continue;
				if(!i->second->readable) continue;
				if(valuesCentral.find(channel) == valuesCentral.end()) continue;
				if(valuesCentral[channel].find(i->second->id) == valuesCentral[channel].end()) continue;
				auto& parameter = valuesCentral[channel][i->second->id];
				std::vector<uint8_t> parameterData = parameter.getBinaryData();
				element = i->second->convertFromPacket(parameterData, parameter.invert(), false);
			}

			if(!element) continue;
			if(element->type == VariableType::tVoid) continue;
			variables->structValue->insert(StructElement(i->second->id, element));
		}
		return variables;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable VeluxPeer::setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait)
{
    try
    {
        if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
        if(!value) return Variable::createError(-32500, "value is nullptr.");
        Peer::setValue(clientInfo, channel, valueKey, value, wait); //Ignore result, otherwise setHomegerValue might not be executed
        std::shared_ptr<VeluxCentral> central = std::dynamic_pointer_cast<VeluxCentral>(getCentral());
        if(!central) return Variable::createError(-32500, "Could not get central object.");;
        if(valueKey.empty()) return Variable::createError(-5, "Value key is empty.");
        if(channel == 0 && serviceMessages->set(valueKey, value->booleanValue)) return PVariable(new Variable(VariableType::tVoid));
        std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
        if(channelIterator == valuesCentral.end()) return Variable::createError(-2, "Unknown channel.");
        std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(valueKey);
        if(parameterIterator == valuesCentral[channel].end()) return Variable::createError(-5, "Unknown parameter.");
        PParameter rpcParameter = parameterIterator->second.rpcParameter;
        if(!rpcParameter) return Variable::createError(-5, "Unknown parameter.");
        BaseLib::Systems::RpcConfigurationParameter& parameter = valuesCentral[channel][valueKey];
        std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>());
        std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable>());

        if(rpcParameter->physical->operationType == IPhysical::OperationType::Enum::store)
        {
            std::vector<uint8_t> parameterData;
            rpcParameter->convertToPacket(value, parameter.invert(), parameterData);
            parameter.setBinaryData(parameterData);
            if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
            else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);

            if(rpcParameter->readable)
            {
                valueKeys->push_back(valueKey);
                values->push_back(rpcParameter->convertFromPacket(parameterData, parameter.invert(), true));
            }

            if(!valueKeys->empty())
            {
                std::string address(_serialNumber + ":" + std::to_string(channel));
                raiseEvent(clientInfo->initInterfaceId, _peerID, channel, valueKeys, values);
                raiseRPCEvent(clientInfo->initInterfaceId, _peerID, channel, address, valueKeys, values);
            }
            return std::make_shared<Variable>(VariableType::tVoid);
        }
        else if(rpcParameter->physical->operationType != IPhysical::OperationType::Enum::command) return Variable::createError(-6, "Parameter is not settable.");
        if(rpcParameter->setPackets.empty() && !rpcParameter->writeable) return Variable::createError(-6, "parameter is read only");
        std::vector<std::shared_ptr<Parameter::Packet>> setRequests;
        if(!rpcParameter->setPackets.empty())
        {
            for(std::vector<std::shared_ptr<Parameter::Packet>>::iterator i = rpcParameter->setPackets.begin(); i != rpcParameter->setPackets.end(); ++i)
            {
                if((*i)->conditionOperator != Parameter::Packet::ConditionOperator::none)
                {
                    int32_t intValue = value->integerValue;
                    if(parameter.rpcParameter->logical->type == BaseLib::DeviceDescription::ILogical::Type::Enum::tBoolean) intValue = value->booleanValue;
                    if(!(*i)->checkCondition(intValue)) continue;
                }
                setRequests.push_back(*i);
            }
        }

        std::vector<uint8_t> parameterData;
        rpcParameter->convertToPacket(value, parameter.invert(), parameterData);
        parameter.setBinaryData(parameterData);
        if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
        else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);
        if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + valueKey + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

        if(rpcParameter->readable)
        {
            valueKeys->push_back(valueKey);
            values->push_back(rpcParameter->convertFromPacket(parameterData, parameter.invert(), true));
        }

        for(std::shared_ptr<Parameter::Packet> setRequest : setRequests)
        {
            PacketsById::iterator packetIterator = _rpcDevice->packetsById.find(setRequest->id);
            if(packetIterator == _rpcDevice->packetsById.end()) return Variable::createError(-6, "No frame was found for parameter " + valueKey);
            PPacket frame = packetIterator->second;

            auto packet = std::make_shared<VeluxPacket>((VeluxCommand)frame->type, std::vector<uint8_t>());

            for(BinaryPayloads::iterator i = frame->binaryPayloads.begin(); i != frame->binaryPayloads.end(); ++i)
            {
                if((*i)->parameterId == "SESSION_ID")
                {
                    int32_t messageCounter = _physicalInterface->getMessageCounter();
                    std::vector<uint8_t> data;
                    _bl->hf.memcpyBigEndian(data, messageCounter);
                    packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
                    continue;
                }
                else if((*i)->parameterId == "NODE_ID")
                {
                    std::vector<uint8_t> data;
                    _bl->hf.memcpyBigEndian(data, (int32_t)_address);
                    packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
                    continue;
                }

                if((*i)->constValueInteger > -1)
                {
                    std::vector<uint8_t> data;
                    _bl->hf.memcpyBigEndian(data, (*i)->constValueInteger);
                    packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
                    continue;
                }
                //We can't just search for param, because it is ambiguous (see for example LEVEL for HM-CC-TC.
                if((*i)->parameterId == rpcParameter->physical->groupId)
                {
                    std::vector<uint8_t> data = valuesCentral[channel][valueKey].getBinaryData();
                    packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
                }
                    //Search for all other parameters
                else
                {
                    bool paramFound = false;
                    int32_t currentChannel = (*i)->parameterChannel;
                    if(currentChannel == -1) currentChannel = channel;
                    for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = valuesCentral[currentChannel].begin(); j != valuesCentral[currentChannel].end(); ++j)
                    {
                        //Only compare id. Till now looking for value_id was not necessary.
                        if((*i)->parameterId == j->second.rpcParameter->physical->groupId)
                        {
                            std::vector<uint8_t> data = j->second.getBinaryData();
                            packet->setPosition((*i)->bitIndex, (*i)->bitSize, data);
                            paramFound = true;
                            break;
                        }
                    }
                    if(!paramFound) GD::out.printError("Error constructing packet. param \"" + (*i)->parameterId + "\" not found. Peer: " + std::to_string(_peerID) + " Serial number: " + _serialNumber + " Frame: " + frame->id);
                }
            }

            if(!setRequest->autoReset.empty())
            {
                for(std::vector<std::string>::iterator j = setRequest->autoReset.begin(); j != setRequest->autoReset.end(); ++j)
                {
                    std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator resetParameterIterator = channelIterator->second.find(*j);
                    if(resetParameterIterator == channelIterator->second.end()) continue;
                    PVariable logicalDefaultValue = resetParameterIterator->second.rpcParameter->logical->getDefaultValue();
                    std::vector<uint8_t> defaultValue;
                    resetParameterIterator->second.rpcParameter->convertToPacket(logicalDefaultValue, false, defaultValue);
                    if(!resetParameterIterator->second.equals(defaultValue))
                    {
                        resetParameterIterator->second.setBinaryData(defaultValue);
                        if(resetParameterIterator->second.databaseId > 0) saveParameter(resetParameterIterator->second.databaseId, defaultValue);
                        else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, *j, defaultValue);
                        GD::out.printInfo( "Info: Parameter \"" + *j + "\" was reset to " + BaseLib::HelperFunctions::getHexString(defaultValue) + ". Peer: " + std::to_string(_peerID) + " Serial number: " + _serialNumber + " Frame: " + frame->id);
                        if(rpcParameter->readable)
                        {
                            valueKeys->push_back(*j);
                            values->push_back(logicalDefaultValue);
                        }
                    }
                }
            }

            _physicalInterface->sendPacket(packet);
        }

        if(!valueKeys->empty())
        {
            std::string address(_serialNumber + ":" + std::to_string(channel));
            raiseEvent(clientInfo->initInterfaceId, _peerID, channel, valueKeys, values);
            raiseRPCEvent(clientInfo->initInterfaceId, _peerID, channel, address, valueKeys, values);
        }

        return std::make_shared<Variable>(VariableType::tVoid);
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return Variable::createError(-32500, "Unknown application error. See error log for more details.");
}
//End RPC methods
}
