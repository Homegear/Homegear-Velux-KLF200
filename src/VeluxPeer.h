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

#ifndef VELUXPEER_H_
#define VELUXPEER_H_

#include "VeluxPacket.h"

#include <homegear-base/BaseLib.h>

#include <list>

using namespace BaseLib;
using namespace BaseLib::DeviceDescription;
using namespace BaseLib::Systems;

namespace Velux
{
class VeluxCentral;
class Klf200;

class VeluxPeer : public BaseLib::Systems::Peer
{
public:
	VeluxPeer(uint32_t parentID, IPeerEventSink* eventHandler);
	VeluxPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler);
	virtual ~VeluxPeer();

	//Features
	virtual bool wireless() { return true; }
	//End features

	//{{{ In table variables
	std::string getPhysicalInterfaceId() { return _physicalInterfaceId; }
	void setPhysicalInterfaceId(std::string);
	//}}}

	std::shared_ptr<Klf200>& getPhysicalInterface() { return _physicalInterface; }

	virtual std::string handleCliCommand(std::string command);

	virtual bool load(BaseLib::Systems::ICentral* central);
	virtual void save(bool savePeer, bool saveVariables, bool saveCentralConfig);
	virtual void savePeers() {};
	virtual int32_t getChannelGroupedWith(int32_t channel) { return -1; }
	virtual int32_t getNewFirmwareVersion() { return 0; }
	virtual std::string getFirmwareVersionString(int32_t firmwareVersion);
    virtual bool firmwareUpdateAvailable() { return false; }

	void packetReceived(std::shared_ptr<VeluxPacket> packet);

	//RPC methods
	/**
	 * {@inheritDoc}
	 */
    virtual PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields);

    /**
	 * {@inheritDoc}
	 */
	virtual PVariable getParamsetDescription(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls);

	/**
	 * {@inheritDoc}
	 */
	virtual PVariable getParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls);

	/**
	 * {@inheritDoc}
	 */
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing = false);

	/**
	 * {@inheritDoc}
	 */
	virtual PVariable setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait);
	//End RPC methods
protected:
    struct FrameValue
    {
        std::list<uint32_t> channels;
        std::vector<uint8_t> value;
    };

    struct FrameValues
    {
        std::string frameID;
        std::list<uint32_t> paramsetChannels;
        ParameterGroup::Type::Enum parameterSetType;
        std::map<std::string, FrameValue> values;
    };

	//In table variables:
	std::string _physicalInterfaceId;
	//End

	std::shared_ptr<Klf200> _physicalInterface;

	virtual void setPhysicalInterface(std::shared_ptr<Klf200> interface);

	virtual std::shared_ptr<BaseLib::Systems::ICentral> getCentral();

    void getValuesFromPacket(PVeluxPacket packet, std::vector<FrameValues>& frameValue);

	virtual PParameterGroup getParameterSet(int32_t channel, ParameterGroup::Type::Enum type);

	virtual void loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows);
    virtual void saveVariables();

    // {{{ Hooks
		/**
		 * {@inheritDoc}
		 */
		virtual bool getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters);
	// }}}
};

}

#endif
