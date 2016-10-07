/* Copyright 2013-2016 Sathya Laufer
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

#ifndef MAXCENTRAL_H_
#define MAXCENTRAL_H_

#include <homegear-base/BaseLib.h>
#include "MAXPeer.h"
#include "MAXPacket.h"
#include "MAXMessages.h"
#include "QueueManager.h"
#include "PacketManager.h"

#include <memory>
#include <mutex>
#include <string>

namespace MAX
{
class MAXMessages;

class MAXCentral : public BaseLib::Systems::ICentral
{
public:
	//In table variables
	int32_t getCentralAddress() { return _centralAddress; }
	void setCentralAddress(int32_t value) { _centralAddress = value; saveVariable(1, value); }
	//End

	MAXCentral(ICentralEventSink* eventHandler);
	MAXCentral(uint32_t deviceType, std::string serialNumber, int32_t address, ICentralEventSink* eventHandler);
	virtual ~MAXCentral();
	virtual void stopThreads();
	virtual void dispose(bool wait = true);

	std::unordered_map<int32_t, uint8_t>* messageCounter() { return &_messageCounter; }
	static bool isSwitch(uint32_t type);

	std::shared_ptr<MAXPeer> getPeer(int32_t address);
	std::shared_ptr<MAXPeer> getPeer(uint64_t id);
	std::shared_ptr<MAXPeer> getPeer(std::string serialNumber);
	virtual bool isInPairingMode() { return _pairing; }
	virtual std::shared_ptr<MAXMessages> getMessages() { return _messages; }
	std::shared_ptr<MAXPacket> getSentPacket(int32_t address) { return _sentPackets.get(address); }
	std::shared_ptr<MAXPacket> getTimePacket(uint8_t messageCounter, int32_t receiverAddress, bool burst);

	virtual void loadVariables();
	virtual void saveVariables();
	virtual void saveMessageCounters();
	virtual void serializeMessageCounters(std::vector<uint8_t>& encodedData);
	virtual void unserializeMessageCounters(std::shared_ptr<std::vector<char>> serializedData);
	virtual void loadPeers();
	virtual void savePeers(bool full);

	virtual bool onPacketReceived(std::string& senderID, std::shared_ptr<BaseLib::Systems::Packet> packet);
	virtual std::string handleCliCommand(std::string command);
	virtual uint64_t getPeerIdFromSerial(std::string serialNumber) { std::shared_ptr<MAXPeer> peer = getPeer(serialNumber); if(peer) return peer->getID(); else return 0; }
	virtual bool enqueuePendingQueues(int32_t deviceAddress, bool wait = false);
	void reset(uint64_t id);

	virtual void sendPacket(std::shared_ptr<BaseLib::Systems::IPhysicalInterface> physicalInterface, std::shared_ptr<MAXPacket> packet, bool stealthy = false);
	virtual void sendOK(int32_t messageCounter, int32_t destinationAddress);

	virtual void handleAck(int32_t messageCounter, std::shared_ptr<MAXPacket>);
	virtual void handlePairingRequest(int32_t messageCounter, std::shared_ptr<MAXPacket>);
	virtual void handleTimeRequest(int32_t messageCounter, std::shared_ptr<MAXPacket> packet);

	virtual PVariable addLink(BaseLib::PRpcClientInfo clientInfo, std::string senderSerialNumber, int32_t senderChannel, std::string receiverSerialNumber, int32_t receiverChannel, std::string name, std::string description);
	virtual PVariable addLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannel, uint64_t receiverID, int32_t receiverChannel, std::string name, std::string description);
	virtual PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags);
	virtual PVariable deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t flags);
	virtual PVariable getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, uint64_t id, std::map<std::string, bool> fields);
	virtual PVariable getInstallMode(BaseLib::PRpcClientInfo clientInfo);
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t channel, ParameterGroup::Type::Enum type, std::string remoteSerialNumber, int32_t remoteChannel, PVariable paramset);
	virtual PVariable putParamset(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable paramset);
	virtual PVariable removeLink(BaseLib::PRpcClientInfo clientInfo, std::string senderSerialNumber, int32_t senderChannel, std::string receiverSerialNumber, int32_t receiverChannel);
	virtual PVariable removeLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannel, uint64_t receiverID, int32_t receiverChannel);
	virtual PVariable setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration = 60, bool debugOutput = true);
	virtual PVariable setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, std::string interfaceID);
protected:
	//In table variables
	int32_t _centralAddress = 0;
	std::unordered_map<int32_t, uint8_t> _messageCounter;
	//End

	bool _stopWorkerThread = false;
	std::thread _workerThread;

	bool _pairing = false;
	QueueManager _queueManager;
	PacketManager _receivedPackets;
	PacketManager _sentPackets;
	std::shared_ptr<MAXMessages> _messages;

	uint32_t _timeLeftInPairingMode = 0;
	void pairingModeTimer(int32_t duration, bool debugOutput = true);
	bool _stopPairingModeThread = false;
	std::mutex _pairingModeThreadMutex;
	std::thread _pairingModeThread;
	std::mutex _unpairThreadMutex;
	std::thread _unpairThread;

	std::shared_ptr<MAXPeer> createPeer(int32_t address, int32_t firmwareVersion, uint32_t deviceType, std::string serialNumber, bool save = true);
	void deletePeer(uint64_t id);
	std::mutex _peerInitMutex;
	std::mutex _enqueuePendingQueuesMutex;
	virtual void setUpMAXMessages();
	virtual void worker();
	virtual void init();

	void addHomegearFeatures(std::shared_ptr<MAXPeer> peer);
	void addHomegearFeaturesValveDrive(std::shared_ptr<MAXPeer> peer);

	virtual std::shared_ptr<IPhysicalInterface> getPhysicalInterface(int32_t peerAddress);
};

}

#endif
