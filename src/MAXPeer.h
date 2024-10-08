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

#ifndef MAXPEER_H_
#define MAXPEER_H_

#include <homegear-base/BaseLib.h>
#include "MAXPacket.h"
#include "PendingQueues.h"

#include <list>

using namespace BaseLib;
using namespace BaseLib::DeviceDescription;
using namespace BaseLib::Systems;

namespace MAX
{
class MAXCentral;

class FrameValue
{
public:
	std::list<uint32_t> channels;
	std::vector<uint8_t> value;
};

class FrameValues
{
public:
	std::string frameID;
	std::list<uint32_t> paramsetChannels;
	ParameterGroup::Type::Enum parameterSetType;
	std::map<std::string, FrameValue> values;
};

class MAXPeer : public BaseLib::Systems::Peer
{
public:
	bool ignorePackets = false;

	MAXPeer(uint32_t parentID, IPeerEventSink* eventHandler);
	MAXPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler);
	virtual ~MAXPeer();

	//Features
	virtual bool wireless() { return true; }
	//End features

	//In table variables:
	int32_t getMessageCounter() { return _messageCounter; }
	void setMessageCounter(int32_t value) { _messageCounter = value; saveVariable(5, value); }
	std::string getPhysicalInterfaceID() { return _physicalInterfaceID; }
	void setPhysicalInterfaceID(std::string);
	//End

	std::shared_ptr<PendingQueues> pendingQueues;

	virtual void worker();
	virtual std::string handleCliCommand(std::string command);

	virtual bool load(BaseLib::Systems::ICentral* central);
	virtual void save(bool savePeer, bool saveVariables, bool saveCentralConfig);
    void serializePeers(std::vector<uint8_t>& encodedData);
    void unserializePeers(std::shared_ptr<std::vector<char>> serializedData);
	virtual void savePeers();
	void savePendingQueues();
	bool hasPeers(int32_t channel) { if(_peers.find(channel) == _peers.end() || _peers[channel].empty()) return false; else return true; }
	void addPeer(int32_t channel, std::shared_ptr<BaseLib::Systems::BasicPeer> peer);
	void removePeer(int32_t channel, uint64_t id, int32_t remoteChannel);
	virtual int32_t getChannelGroupedWith(int32_t channel) { return -1; }
	virtual int32_t getNewFirmwareVersion() { return 0; }
	virtual std::string getFirmwareVersionString(int32_t firmwareVersion);
    virtual bool firmwareUpdateAvailable() { return false; }
    std::string printConfig();
    virtual bool pendingQueuesEmpty();

    std::shared_ptr<IPhysicalInterface> getPhysicalInterface() { return _physicalInterface; }
    void setRSSIDevice(uint8_t rssi);
	void getValuesFromPacket(std::shared_ptr<MAXPacket> packet, std::vector<FrameValues>& frameValue);
	void packetReceived(std::shared_ptr<MAXPacket> packet);
	void sendTime();

	//RPC methods
	virtual PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields);
	virtual PVariable getParamsetDescription(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls);
	virtual PVariable getParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls);
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing = false);
	PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, std::string interfaceID);
	virtual PVariable setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait);
	//End RPC methods
protected:
	uint32_t _lastRSSIDevice = 0;
	std::shared_ptr<IPhysicalInterface> _physicalInterface;
	int64_t _lastTimePacket = 0;
	int32_t _randomSleep = 0;
	int32_t _lastReceivedMessageCounter = -1;

	//In table variables:
	uint8_t _messageCounter = 0;
	std::string _physicalInterfaceID;
	//End

	virtual void setPhysicalInterface(std::shared_ptr<IPhysicalInterface> interface);

	virtual std::shared_ptr<BaseLib::Systems::ICentral> getCentral();

	virtual PParameterGroup getParameterSet(int32_t channel, ParameterGroup::Type::Enum type);

	virtual void loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows);
    virtual void saveVariables();
};

}

#endif
