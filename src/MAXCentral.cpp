/* Copyright 2013-2017 Sathya Laufer
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

#include "MAXCentral.h"
#include "MAXDeviceTypes.h"
#include "GD.h"

namespace MAX {

MAXCentral::MAXCentral(ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MAX_FAMILY_ID, GD::bl, eventHandler)
{
	init();
}

MAXCentral::MAXCentral(uint32_t deviceID, std::string serialNumber, int32_t address, ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MAX_FAMILY_ID, GD::bl, deviceID, serialNumber, address, eventHandler)
{
	init();
}

MAXCentral::~MAXCentral()
{
	dispose();
}

void MAXCentral::dispose(bool wait)
{
	try
	{
		if(_disposing) return;
		_disposing = true;
		GD::out.printDebug("Removing device " + std::to_string(_deviceId) + " from physical device's event queue...");
		for(std::map<std::string, std::shared_ptr<IPhysicalInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); ++i)
		{
			//Just to make sure cycle through all physical devices. If event handler is not removed => segfault
			i->second->removeEventHandler(_physicalInterfaceEventhandlers[i->first]);
		}

		stopThreads();

		_queueManager.dispose(false);
		_receivedPackets.dispose(false);
		_sentPackets.dispose(false);

		_peersMutex.lock();
		for(std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::const_iterator i = _peers.begin(); i != _peers.end(); ++i)
		{
			i->second->dispose();
		}
		_peersMutex.unlock();
	}
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::stopThreads()
{
	try
	{
		_unpairThreadMutex.lock();
		_bl->threadManager.join(_unpairThread);
		_unpairThreadMutex.unlock();
		_pairingModeThreadMutex.lock();
		_stopPairingModeThread = true;
		_bl->threadManager.join(_pairingModeThread);
		_pairingModeThreadMutex.unlock();
		_stopWorkerThread = true;
		GD::out.printDebug("Debug: Waiting for worker thread of device " + std::to_string(_deviceId) + "...");
		_bl->threadManager.join(_workerThread);
	}
    catch(const std::exception& ex)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::init()
{
	try
	{
		if(_initialized) return; //Prevent running init two times
		_initialized = true;

		_messages = std::shared_ptr<MAXMessages>(new MAXMessages());

		_messageCounter[0] = 0; //Broadcast message counter
		_stopWorkerThread = false;
		_pairing = false;
		_stopPairingModeThread = false;
		_timeLeftInPairingMode = 0;

		setUpMAXMessages();

		for(std::map<std::string, std::shared_ptr<IPhysicalInterface>>::iterator i = GD::physicalInterfaces.begin(); i != GD::physicalInterfaces.end(); ++i)
		{
			_physicalInterfaceEventhandlers[i->first] = i->second->addEventHandler((IPhysicalInterface::IPhysicalInterfaceEventSink*)this);
		}

		GD::bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &MAXCentral::worker, this);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

bool MAXCentral::isSwitch(uint32_t type)
{
	switch((DeviceType)type)
	{
	case DeviceType::BCTSSWPL:
		return true;
	default:
		return false;
	}
	return false;
}

void MAXCentral::setUpMAXMessages()
{
	try
	{
		_messages->add(std::shared_ptr<MAXMessage>(new MAXMessage(0x00, 0x04, ACCESSPAIREDTOSENDER, FULLACCESS, &MAXCentral::handlePairingRequest)));

		_messages->add(std::shared_ptr<MAXMessage>(new MAXMessage(0x02, -1, ACCESSPAIREDTOSENDER | ACCESSDESTISME, ACCESSPAIREDTOSENDER | ACCESSDESTISME, &MAXCentral::handleAck)));

		_messages->add(std::shared_ptr<MAXMessage>(new MAXMessage(0x03, 0x0A, ACCESSPAIREDTOSENDER | ACCESSDESTISME, NOACCESS, &MAXCentral::handleTimeRequest)));
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::worker()
{
	try
	{
		std::chrono::milliseconds sleepingTime(10);
		uint32_t counter = 0;
		int32_t lastPeer;
		lastPeer = 0;
		//One loop on the Raspberry Pi takes about 30Âµs
		while(!_stopWorkerThread)
		{
			try
			{
				std::this_thread::sleep_for(sleepingTime);
				if(_stopWorkerThread) return;
				if(counter > 10000)
				{
					counter = 0;
					_peersMutex.lock();
					if(_peers.size() > 0)
					{
						int32_t windowTimePerPeer = _bl->settings.workerThreadWindow() / _peers.size();
						if(windowTimePerPeer > 2) windowTimePerPeer -= 2;
						sleepingTime = std::chrono::milliseconds(windowTimePerPeer);
					}
					_peersMutex.unlock();
				}
				_peersMutex.lock();
				if(!_peers.empty())
				{
					if(!_peers.empty())
					{
						std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator nextPeer = _peers.find(lastPeer);
						if(nextPeer != _peers.end())
						{
							nextPeer++;
							if(nextPeer == _peers.end()) nextPeer = _peers.begin();
						}
						else nextPeer = _peers.begin();
						lastPeer = nextPeer->first;
					}
				}
				_peersMutex.unlock();
				std::shared_ptr<MAXPeer> peer(getPeer(lastPeer));
				if(peer && !peer->deleting) peer->worker();
				counter++;
			}
			catch(const std::exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<IPhysicalInterface> MAXCentral::getPhysicalInterface(int32_t peerAddress)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(peerAddress);
		if(queue) return queue->getPhysicalInterface();
		std::shared_ptr<MAXPeer> peer = getPeer(peerAddress);
		return peer ? peer->getPhysicalInterface() : GD::defaultPhysicalInterface;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return GD::defaultPhysicalInterface;
}

bool MAXCentral::onPacketReceived(std::string& senderID, std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(_disposing) return false;
		std::shared_ptr<MAXPacket> maxPacket(std::dynamic_pointer_cast<MAXPacket>(packet));
		if(!maxPacket) return false;
		if(GD::bl->debugLevel >= 4) std::cout << BaseLib::HelperFunctions::getTimeString(maxPacket->timeReceived()) << " MAX packet received (" + senderID + (maxPacket->rssiDevice() ? ", RSSI: 0x" + _bl->hf.getHexString(maxPacket->rssiDevice(), 2) : "") + "): " + maxPacket->hexString() << std::endl;
		if(maxPacket->senderAddress() == _address) //Packet spoofed
		{
			std::shared_ptr<MAXPeer> peer(getPeer(maxPacket->destinationAddress()));
			if(peer)
			{
				if(senderID != peer->getPhysicalInterfaceID()) return true; //Packet we sent was received by another interface
				GD::out.printWarning("Warning: Central address of packet to peer " + std::to_string(peer->getID()) + " was spoofed. Packet was: " + packet->hexString());
				peer->serviceMessages->set("CENTRAL_ADDRESS_SPOOFED", 1, 0);
				std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string> { "CENTRAL_ADDRESS_SPOOFED" });
				std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable> { PVariable(new Variable((int32_t)1)) });
                std::string eventSource = "device-" + std::to_string(peer->getID());
                std::string address = peer->getSerialNumber() + ":0";
				raiseRPCEvent(eventSource, peer->getID(), 0, address, valueKeys, values);
				return true;
			}
			return false;
		}
		std::shared_ptr<IPhysicalInterface> physicalInterface = getPhysicalInterface(maxPacket->senderAddress());
		if(physicalInterface->getID() != senderID) return true;

		bool handled = false;
		if(_receivedPackets.set(maxPacket->senderAddress(), maxPacket, maxPacket->timeReceived())) handled = true;
		std::shared_ptr<MAXMessage> message = _messages->find(maxPacket);
		if(message && message->checkAccess(maxPacket, _queueManager.get(maxPacket->senderAddress())))
		{
			if(_bl->debugLevel >= 6) GD::out.printDebug("Debug: Device " + std::to_string(_deviceId) + ": Access granted for packet " + maxPacket->hexString());
			message->invokeMessageHandler(maxPacket);
			handled = true;
		}

		std::shared_ptr<MAXPeer> peer(getPeer(maxPacket->senderAddress()));
		if(!peer) return false;
		std::shared_ptr<MAXPeer> team;
		if(handled)
		{
			//This block is not necessary for teams as teams will never have queues.
			std::shared_ptr<PacketQueue> queue = _queueManager.get(maxPacket->senderAddress());
			if(queue && queue->getQueueType() != PacketQueueType::PEER)
			{
				peer->setLastPacketReceived();
				peer->serviceMessages->endUnreach();
				peer->setRSSIDevice(maxPacket->rssiDevice());
				return true; //Packet is handled by queue. Don't check if queue is empty!
			}
		}
		peer->packetReceived(maxPacket);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return false;
}

void MAXCentral::reset(uint64_t id)
{
	try
	{
		std::shared_ptr<MAXPeer> peer(getPeer(id));
		if(!peer) return;
		std::shared_ptr<PacketQueue> pendingQueue(new PacketQueue(peer->getPhysicalInterface(), PacketQueueType::UNPAIRING));
		pendingQueue->noSending = true;

		//RESET
		std::vector<uint8_t> payload;
		payload.push_back(0);
		std::shared_ptr<MAXPacket> resetPacket(new MAXPacket(_messageCounter[0], 0xF0, 0, _address, peer->getAddress(), payload, false));
		pendingQueue->push(resetPacket);
		pendingQueue->push(_messages->find(0x02, 0x02, std::vector<std::pair<uint32_t, int32_t>>()));
		_messageCounter[0]++; //Count resends in

		while(!peer->pendingQueues->empty()) peer->pendingQueues->pop();
		peer->pendingQueues->push(pendingQueue);
		peer->serviceMessages->setConfigPending(true);

		if((peer->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio) || (peer->getRXModes() & HomegearDevice::ReceiveModes::always))
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(peer->getPhysicalInterface(), PacketQueueType::UNPAIRING, peer->getAddress());
			queue->peer = peer;
			queue->push(peer->pendingQueues);
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::loadPeers()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			int32_t peerID = row->second.at(0)->intValue;
			GD::out.printMessage("Loading MAX! peer " + std::to_string(peerID));
			int32_t address = row->second.at(2)->intValue;
			std::shared_ptr<MAXPeer> peer(new MAXPeer(peerID, address, row->second.at(3)->textValue, _deviceId, this));
			if(!peer->load(this)) continue;
			if(!peer->getRpcDevice()) continue;
			_peersMutex.lock();
			_peers[peer->getAddress()] = peer;
			if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
			_peersById[peerID] = peer;
			_peersMutex.unlock();
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    	_peersMutex.unlock();
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    	_peersMutex.unlock();
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    	_peersMutex.unlock();
    }
}

void MAXCentral::loadVariables()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getDeviceVariables(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			_variableDatabaseIds[row->second.at(2)->intValue] = row->second.at(0)->intValue;
			switch(row->second.at(2)->intValue)
			{
			case 1:
				_centralAddress = row->second.at(3)->intValue;
				break;
			case 2:
				unserializeMessageCounters(row->second.at(5)->binaryValue);
				break;
			}
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::saveMessageCounters()
{
	try
	{
		std::vector<uint8_t> serializedData;
		serializeMessageCounters(serializedData);
		saveVariable(2, serializedData);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::serializeMessageCounters(std::vector<uint8_t>& encodedData)
{
	try
	{
		BaseLib::BinaryEncoder encoder(_bl);
		encoder.encodeInteger(encodedData, _messageCounter.size());
		for(std::unordered_map<int32_t, uint8_t>::const_iterator i = _messageCounter.begin(); i != _messageCounter.end(); ++i)
		{
			encoder.encodeInteger(encodedData, i->first);
			encoder.encodeByte(encodedData, i->second);
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::unserializeMessageCounters(std::shared_ptr<std::vector<char>> serializedData)
{
	try
	{
		BaseLib::BinaryDecoder decoder(_bl);
		uint32_t position = 0;
		uint32_t messageCounterSize = decoder.decodeInteger(*serializedData, position);
		for(uint32_t i = 0; i < messageCounterSize; i++)
		{
			int32_t index = decoder.decodeInteger(*serializedData, position);
			_messageCounter[index] = decoder.decodeByte(*serializedData, position);
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::savePeers(bool full)
{
	try
	{
		_peersMutex.lock();
		for(std::unordered_map<int32_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peers.begin(); i != _peers.end(); ++i)
		{
			//Necessary, because peers can be assigned to multiple virtual devices
			if(i->second->getParentID() != _deviceId) continue;
			//We are always printing this, because the init script needs it
			GD::out.printMessage("(Shutdown) => Saving MAX! peer " + std::to_string(i->second->getID()));
			i->second->save(full, full, full);
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
	_peersMutex.unlock();
}

void MAXCentral::saveVariables()
{
	try
	{
		if(_deviceId == 0) return;
		saveVariable(1, _centralAddress);
		saveMessageCounters(); //2
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<MAXPeer> MAXCentral::getPeer(int32_t address)
{
	try
	{
		_peersMutex.lock();
		if(_peers.find(address) != _peers.end())
		{
			std::shared_ptr<MAXPeer> peer(std::dynamic_pointer_cast<MAXPeer>(_peers.at(address)));
			_peersMutex.unlock();
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
    return std::shared_ptr<MAXPeer>();
}

std::shared_ptr<MAXPeer> MAXCentral::getPeer(uint64_t id)
{
	try
	{
		_peersMutex.lock();
		if(_peersById.find(id) != _peersById.end())
		{
			std::shared_ptr<MAXPeer> peer(std::dynamic_pointer_cast<MAXPeer>(_peersById.at(id)));
			_peersMutex.unlock();
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
    return std::shared_ptr<MAXPeer>();
}

std::shared_ptr<MAXPeer> MAXCentral::getPeer(std::string serialNumber)
{
	try
	{
		_peersMutex.lock();
		if(_peersBySerial.find(serialNumber) != _peersBySerial.end())
		{
			std::shared_ptr<MAXPeer> peer(std::dynamic_pointer_cast<MAXPeer>(_peersBySerial.at(serialNumber)));
			_peersMutex.unlock();
			return peer;
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _peersMutex.unlock();
    return std::shared_ptr<MAXPeer>();
}

void MAXCentral::deletePeer(uint64_t id)
{
	try
	{
		std::shared_ptr<MAXPeer> peer(getPeer(id));
		if(!peer) return;
		peer->deleting = true;
		PVariable deviceAddresses(new Variable(VariableType::tArray));
		deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber())));

		PVariable deviceInfo(new Variable(VariableType::tStruct));
		deviceInfo->structValue->insert(StructElement("ID", PVariable(new Variable((int32_t)peer->getID()))));
		PVariable channels(new Variable(VariableType::tArray));
		deviceInfo->structValue->insert(StructElement("CHANNELS", channels));

		std::shared_ptr<HomegearDevice> rpcDevice = peer->getRpcDevice();
		for(Functions::iterator i = rpcDevice->functions.begin(); i != rpcDevice->functions.end(); ++i)
		{
			deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber() + ":" + std::to_string(i->first))));
			channels->arrayValue->push_back(PVariable(new Variable(i->first)));
		}

        std::vector<uint64_t> deletedIds{ id };
		raiseRPCDeleteDevices(deletedIds, deviceAddresses, deviceInfo);

		{
			std::lock_guard<std::mutex> peersGuard(_peersMutex);
			if(_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
			if(_peersById.find(id) != _peersById.end()) _peersById.erase(id);
			if(_peers.find(peer->getAddress()) != _peers.end()) _peers.erase(peer->getAddress());
		}

		if(_currentPeer && _currentPeer->getID() == id) _currentPeer.reset();

		int32_t i = 0;
		while(peer.use_count() > 1 && i < 600)
		{
			if(_currentPeer && _currentPeer->getID() == id) _currentPeer.reset();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			i++;
		}
		if(i == 600) GD::out.printError("Error: Peer deletion took too long.");

		peer->deleteFromDatabase();

		GD::out.printMessage("Removed peer " + std::to_string(peer->getID()));
	}
	catch(const std::exception& ex)
    {
		_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	_peersMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::string MAXCentral::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;
		if(_currentPeer)
		{
			if(command == "unselect" || command == "u")
			{
				_currentPeer.reset();
				return "Peer unselected.\n";
			}
			return _currentPeer->handleCliCommand(command);
		}
		if(command == "help" || command == "h")
		{
			stringStream << "List of commands (shortcut in brackets):" << std::endl << std::endl;
			stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
			stringStream << "pairing on (pon)\tEnables pairing mode" << std::endl;
			stringStream << "pairing off (pof)\tDisables pairing mode" << std::endl;
			stringStream << "peers list (ls)\t\tList all peers" << std::endl;
			stringStream << "peers remove (prm)\tRemove a peer (without unpairing)" << std::endl;
			stringStream << "peers select (ps)\tSelect a peer" << std::endl;
			stringStream << "peers setname (pn)\tName a peer" << std::endl;
			stringStream << "peers unpair (pup)\tUnpair a peer" << std::endl;
			stringStream << "unselect (u)\t\tUnselect this device" << std::endl;
			return stringStream.str();
		}
		if(command.compare(0, 10, "pairing on") == 0 || command.compare(0, 3, "pon") == 0)
		{
			int32_t duration = 60;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'o') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help")
					{
						stringStream << "Description: This command enables pairing mode." << std::endl;
						stringStream << "Usage: pairing on [DURATION]" << std::endl << std::endl;
						stringStream << "Parameters:" << std::endl;
						stringStream << "  DURATION:\tOptional duration in seconds to stay in pairing mode." << std::endl;
						return stringStream.str();
					}
					duration = BaseLib::Math::getNumber(element, false);
					if(duration < 5 || duration > 3600) return "Invalid duration. Duration has to be greater than 5 and less than 3600.\n";
				}
				index++;
			}

			setInstallMode(nullptr, true, duration, nullptr, false);
			stringStream << "Pairing mode enabled." << std::endl;
			return stringStream.str();
		}
		else if(command.compare(0, 11, "pairing off") == 0 || command.compare(0, 3, "pof") == 0)
		{
			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'o') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help")
					{
						stringStream << "Description: This command disables pairing mode." << std::endl;
						stringStream << "Usage: pairing off" << std::endl << std::endl;
						stringStream << "Parameters:" << std::endl;
						stringStream << "  There are no parameters." << std::endl;
						return stringStream.str();
					}
				}
				index++;
			}

			setInstallMode(nullptr, false, -1, nullptr, false);
			stringStream << "Pairing mode disabled." << std::endl;
			return stringStream.str();
		}
		else if(command.compare(0, 12, "peers remove") == 0 || command.compare(0, 3, "prm") == 0)
		{
			uint64_t peerID = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'r') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					peerID = BaseLib::Math::getNumber(element, false);
					if(peerID == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command removes a peer without trying to unpair it first." << std::endl;
				stringStream << "Usage: peers remove PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to remove. Example: 513" << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				if(_currentPeer && _currentPeer->getID() == peerID) _currentPeer.reset();
				deletePeer(peerID);
				stringStream << "Removed peer " << std::to_string(peerID) << "." << std::endl;
			}
			return stringStream.str();
		}
		else if(command.compare(0, 13, "peers setname") == 0 || command.compare(0, 2, "pn") == 0)
		{
			uint64_t peerID = 0;
			std::string name;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'n') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					else
					{
						peerID = BaseLib::Math::getNumber(element, false);
						if(peerID == 0) return "Invalid id.\n";
					}
				}
				else if(index == 2 + offset) name = element;
				else name += ' ' + element;
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command sets or changes the name of a peer to identify it more easily." << std::endl;
				stringStream << "Usage: peers setname PEERID NAME" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to set the name for. Example: 513" << std::endl;
				stringStream << "  NAME:\tThe name to set. Example: \"1st floor light switch\"." << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				std::shared_ptr<MAXPeer> peer = getPeer(peerID);
				peer->setName(name);
				stringStream << "Name set to \"" << name << "\"." << std::endl;
			}
			return stringStream.str();
		}
		else if(command.compare(0, 12, "peers unpair") == 0 || command.compare(0, 3, "pup") == 0)
		{
			uint64_t peerID = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'u') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					peerID = BaseLib::Math::getNumber(element, false);
					if(peerID == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command unpairs a peer." << std::endl;
				stringStream << "Usage: peers unpair PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to unpair. Example: 513" << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				if(_currentPeer && _currentPeer->getID() == peerID) _currentPeer.reset();
				stringStream << "Unpairing peer " << std::to_string(peerID) << std::endl;
				reset(peerID);
			}
			return stringStream.str();
		}
		else if(command.compare(0, 10, "peers list") == 0 || command.compare(0, 2, "pl") == 0 || command.compare(0, 2, "ls") == 0)
		{
			try
			{
				std::string filterType;
				std::string filterValue;

				std::stringstream stream(command);
				std::string element;
				int32_t offset = (command.at(1) == 'l' || command.at(1) == 's') ? 0 : 1;
				int32_t index = 0;
				while(std::getline(stream, element, ' '))
				{
					if(index < 1 + offset)
					{
						index++;
						continue;
					}
					else if(index == 1 + offset)
					{
						if(element == "help")
						{
							index = -1;
							break;
						}
						filterType = BaseLib::HelperFunctions::toLower(element);
					}
					else if(index == 2 + offset)
					{
						filterValue = element;
						if(filterType == "name") BaseLib::HelperFunctions::toLower(filterValue);
					}
					index++;
				}
				if(index == -1)
				{
					stringStream << "Description: This command lists information about all peers." << std::endl;
					stringStream << "Usage: peers list [FILTERTYPE] [FILTERVALUE]" << std::endl << std::endl;
					stringStream << "Parameters:" << std::endl;
					stringStream << "  FILTERTYPE:\tSee filter types below." << std::endl;
					stringStream << "  FILTERVALUE:\tDepends on the filter type. If a number is required, it has to be in hexadecimal format." << std::endl << std::endl;
					stringStream << "Filter types:" << std::endl;
					stringStream << "  ID: Filter by id." << std::endl;
					stringStream << "      FILTERVALUE: The id of the peer to filter (e. g. 513)." << std::endl;
					stringStream << "  ADDRESS: Filter by address." << std::endl;
					stringStream << "      FILTERVALUE: The 4 byte address of the peer to filter (e. g. 001DA44D)." << std::endl;
					stringStream << "  SERIAL: Filter by serial number." << std::endl;
					stringStream << "      FILTERVALUE: The serial number of the peer to filter (e. g. JEQ0554309)." << std::endl;
					stringStream << "  NAME: Filter by name." << std::endl;
					stringStream << "      FILTERVALUE: The part of the name to search for (e. g. \"1st floor\")." << std::endl;
					stringStream << "  TYPE: Filter by device type." << std::endl;
					stringStream << "      FILTERVALUE: The 2 byte device type in hexadecimal format." << std::endl;
					stringStream << "  UNREACH: List all unreachable peers." << std::endl;
					stringStream << "      FILTERVALUE: empty" << std::endl;
					return stringStream.str();
				}

				if(_peers.empty())
				{
					stringStream << "No peers are paired to this central." << std::endl;
					return stringStream.str();
				}
				std::string bar(" │ ");
				const int32_t idWidth = 8;
				const int32_t nameWidth = 25;
				const int32_t addressWidth = 7;
				const int32_t serialWidth = 13;
				const int32_t typeWidth1 = 4;
				const int32_t typeWidth2 = 25;
				const int32_t firmwareWidth = 8;
				const int32_t unreachWidth = 7;
				std::string nameHeader("Name");
				nameHeader.resize(nameWidth, ' ');
				std::string typeStringHeader("Type String");
				typeStringHeader.resize(typeWidth2, ' ');
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << "ID" << bar
					<< nameHeader << bar
					<< std::setw(addressWidth) << "Address" << bar
					<< std::setw(serialWidth) << "Serial Number" << bar
					<< std::setw(typeWidth1) << "Type" << bar
					<< typeStringHeader << bar
					<< std::setw(firmwareWidth) << "Firmware" << bar
					<< std::setw(unreachWidth) << "Unreach"
					<< std::endl;
				stringStream << "─────────┼───────────────────────────┼─────────┼───────────────┼──────┼───────────────────────────┼──────────┼────────" << std::endl;
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << " " << bar
					<< std::setw(nameWidth) << " " << bar
					<< std::setw(addressWidth) << " " << bar
					<< std::setw(serialWidth) << " " << bar
					<< std::setw(typeWidth1) << " " << bar
					<< std::setw(typeWidth2) << " " << bar
					<< std::setw(firmwareWidth) << " " << bar
					<< std::setw(unreachWidth) << " "
					<< std::endl;
				_peersMutex.lock();
				for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
				{
					if(filterType == "id")
					{
						uint64_t id = BaseLib::Math::getNumber(filterValue, false);
						if(i->second->getID() != id) continue;
					}
					else if(filterType == "name")
					{
						std::string name = i->second->getName();
						if((signed)BaseLib::HelperFunctions::toLower(name).find(filterValue) == (signed)std::string::npos) continue;
					}
					else if(filterType == "address")
					{
						int32_t address = BaseLib::Math::getNumber(filterValue, true);
						if(i->second->getAddress() != address) continue;
					}
					else if(filterType == "serial")
					{
						if(i->second->getSerialNumber() != filterValue) continue;
					}
					else if(filterType == "type")
					{
						int32_t deviceType = BaseLib::Math::getNumber(filterValue, true);
						if((int32_t)i->second->getDeviceType() != deviceType) continue;
					}
					else if(filterType == "unreach")
					{
						if(i->second->serviceMessages)
						{
							if(!i->second->serviceMessages->getUnreach()) continue;
						}
					}

					stringStream << std::setw(idWidth) << std::setfill(' ') << std::to_string(i->second->getID()) << bar;
					std::string name = i->second->getName();
					size_t nameSize = BaseLib::HelperFunctions::utf8StringSize(name);
					if(nameSize > (unsigned)nameWidth)
					{
						name = BaseLib::HelperFunctions::utf8Substring(name, 0, nameWidth - 3);
						name += "...";
					}
					else name.resize(nameWidth + (name.size() - nameSize), ' ');
					stringStream << name << bar
						<< std::setw(addressWidth) << BaseLib::HelperFunctions::getHexString(i->second->getAddress(), 6) << bar
						<< std::setw(serialWidth) << i->second->getSerialNumber() << bar
						<< std::setw(typeWidth1) << BaseLib::HelperFunctions::getHexString(i->second->getDeviceType(), 4) << bar;
					if(i->second->getRpcDevice())
					{
						PSupportedDevice type = i->second->getRpcDevice()->getType(i->second->getDeviceType(), i->second->getFirmwareVersion());
						std::string typeID;
						if(type) typeID = type->id;
						if(typeID.size() > (unsigned)typeWidth2)
						{
							typeID.resize(typeWidth2 - 3);
							typeID += "...";
						}
						else typeID.resize(typeWidth2, ' ');
						stringStream << typeID << bar;
					}
					else stringStream << std::setw(typeWidth2) << " " << bar;
					if(i->second->getFirmwareVersion() == 0) stringStream << std::setfill(' ') << std::setw(firmwareWidth) << "?" << bar;
					else if(i->second->firmwareUpdateAvailable())
					{
						stringStream << std::setfill(' ') << std::setw(firmwareWidth) << ("*" + BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() >> 4) + "." + BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() & 0x0F)) << bar;
					}
					else stringStream << std::setfill(' ') << std::setw(firmwareWidth) << (BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() >> 4) + "." + BaseLib::HelperFunctions::getHexString(i->second->getFirmwareVersion() & 0x0F)) << bar;
					if(i->second->serviceMessages)
					{
						std::string unreachable(i->second->serviceMessages->getUnreach() ? "Yes" : "No");
						stringStream << std::setw(unreachWidth) << unreachable;
					}
					stringStream << std::endl << std::dec;
				}
				_peersMutex.unlock();
				stringStream << "─────────┴───────────────────────────┴─────────┴───────────────┴──────┴───────────────────────────┴──────────┴────────" << std::endl;

				return stringStream.str();
			}
			catch(const std::exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
		else if(command.compare(0, 12, "peers select") == 0 || command.compare(0, 2, "ps") == 0)
		{
			uint64_t id = 0;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 's') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					id = BaseLib::Math::getNumber(element, false);
					if(id == 0) return "Invalid id.\n";
				}
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command selects a peer." << std::endl;
				stringStream << "Usage: peers select PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to select. Example: 513" << std::endl;
				return stringStream.str();
			}

			_currentPeer = getPeer(id);
			if(!_currentPeer) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				stringStream << "Peer with id " << std::hex << std::to_string(id) << " and device type 0x" << (int32_t)_currentPeer->getDeviceType() << " selected." << std::dec << std::endl;
				stringStream << "For information about the peer's commands type: \"help\"" << std::endl;
			}
			return stringStream.str();
		}
		else return "Unknown command.\n";
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return "Error executing command. See log file for more details.\n";
}

void MAXCentral::sendPacket(std::shared_ptr<IPhysicalInterface> physicalInterface, std::shared_ptr<MAXPacket> packet, bool stealthy)
{
	try
	{
		if(!packet || !physicalInterface) return;
		uint32_t responseDelay = physicalInterface->responseDelay();
		std::shared_ptr<MAXPacketInfo> packetInfo = _sentPackets.getInfo(packet->destinationAddress());
		if(!stealthy) _sentPackets.set(packet->destinationAddress(), packet);
		if(packetInfo)
		{
			int64_t timeDifference = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - packetInfo->time;
			if(timeDifference < responseDelay)
			{
				packetInfo->time += responseDelay - timeDifference; //Set to sending time
				std::this_thread::sleep_for(std::chrono::milliseconds(responseDelay - timeDifference));
			}
		}
		if(stealthy) _sentPackets.keepAlive(packet->destinationAddress());
		packetInfo = _receivedPackets.getInfo(packet->destinationAddress());
		if(packetInfo)
		{
			int64_t time = BaseLib::HelperFunctions::getTime();
			int64_t timeDifference = time - packetInfo->time;
			if(timeDifference >= 0 && timeDifference < responseDelay)
			{
				int64_t sleepingTime = responseDelay - timeDifference;
				if(sleepingTime > 1) sleepingTime -= 1;
				packet->setTimeSending(time + sleepingTime + 1);
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepingTime));
			}
			//Set time to now. This is necessary if two packets are sent after each other without a response in between
			packetInfo->time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}
		else if(_bl->debugLevel > 4) GD::out.printDebug("Debug: Sending packet " + packet->hexString() + " immediately, because it seems it is no response (no packet information found).", 7);
		physicalInterface->sendPacket(packet);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::sendOK(int32_t messageCounter, int32_t destinationAddress)
{
	try
	{
		std::vector<uint8_t> payload;
		payload.push_back(0);
		payload.push_back(0);
		std::shared_ptr<MAXPacket> ok(new MAXPacket(messageCounter, 0x02, 0x02, _address, destinationAddress, payload, false));
		sendPacket(getPhysicalInterface(destinationAddress), ok);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

bool MAXCentral::enqueuePendingQueues(int32_t deviceAddress, bool wait)
{
	try
	{
		_enqueuePendingQueuesMutex.lock();
		std::shared_ptr<MAXPeer> peer = getPeer(deviceAddress);
		if(!peer || !peer->pendingQueues)
		{
			_enqueuePendingQueuesMutex.unlock();
			return true;
		}
		std::shared_ptr<PacketQueue> queue = _queueManager.get(deviceAddress);
		if(!queue) queue = _queueManager.createQueue(peer->getPhysicalInterface(), PacketQueueType::DEFAULT, deviceAddress);
		if(!queue)
		{
			_enqueuePendingQueuesMutex.unlock();
			return true;
		}
		if(!queue->peer) queue->peer = peer;
		if(queue->pendingQueuesEmpty()) queue->push(peer->pendingQueues);
		_enqueuePendingQueuesMutex.unlock();

		if(wait)
		{
			int32_t waitIndex = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			while(!peer->pendingQueuesEmpty() && waitIndex < 200)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				waitIndex++;
			}

			if(!peer->pendingQueuesEmpty()) return false;
		}

		return true;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _enqueuePendingQueuesMutex.unlock();
    return false;
}

std::shared_ptr<MAXPeer> MAXCentral::createPeer(int32_t address, int32_t firmwareVersion, uint32_t deviceType, std::string serialNumber, bool save)
{
	try
	{
		std::shared_ptr<MAXPeer> peer(new MAXPeer(_deviceId, this));
		peer->setAddress(address);
		peer->setFirmwareVersion(firmwareVersion);
		peer->setDeviceType(deviceType);
		peer->setSerialNumber(serialNumber);
		peer->setRpcDevice(GD::family->getRpcDevices()->find(deviceType, firmwareVersion, -1));
		if(!peer->getRpcDevice()) return std::shared_ptr<MAXPeer>();
		if(save) peer->save(true, true, false); //Save and create peerID
		return peer;
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::shared_ptr<MAXPeer>();
}

void MAXCentral::addHomegearFeatures(std::shared_ptr<MAXPeer> peer)
{
	try
	{
		if(!peer) return;
		//if(peer->getDeviceType().type() == (uint32_t)DeviceType::BCRTTRXCYG3) addHomegearFeaturesValveDrive(peer);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

std::shared_ptr<MAXPacket> MAXCentral::getTimePacket(uint8_t messageCounter, int32_t receiverAddress, bool burst)
{
	try
	{
		const auto timePoint = std::chrono::system_clock::now();
		time_t t = std::chrono::system_clock::to_time_t(timePoint);
		tm* localTime = std::localtime(&t);
		t = std::chrono::system_clock::to_time_t(timePoint - std::chrono::seconds(localTime->tm_gmtoff));
		localTime = std::localtime(&t);

		std::vector<uint8_t> payload;
		payload.push_back(0);
		payload.push_back(localTime->tm_year % 100);
		int32_t gmtOff = localTime->tm_gmtoff / 1800;
		payload.push_back(localTime->tm_mday + ((gmtOff & 0x38) << 2));
		payload.push_back(localTime->tm_hour + ((gmtOff & 7) << 5));
		payload.push_back(localTime->tm_min + (((localTime->tm_mon + 1) & 0x0C) << 4));
		payload.push_back(localTime->tm_min + (((localTime->tm_mon + 1) & 3) << 6));

		return std::shared_ptr<MAXPacket>(new MAXPacket(messageCounter, 0x03, 0, _address, receiverAddress, payload, burst));
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return std::shared_ptr<MAXPacket>();
}

//Packet handlers
void MAXCentral::handleTimeRequest(int32_t messageCounter, std::shared_ptr<MAXPacket> packet)
{
	try
	{
		sendPacket(getPhysicalInterface(packet->senderAddress()), getTimePacket(messageCounter, packet->senderAddress(), false));
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::handleAck(int32_t messageCounter, std::shared_ptr<MAXPacket> packet)
{
	try
	{
		std::shared_ptr<PacketQueue> queue = _queueManager.get(packet->senderAddress());
		if(!queue) return;
		std::shared_ptr<MAXPacket> sentPacket(_sentPackets.get(packet->senderAddress()));
		if(packet->payload()->size() > 1 && (packet->payload()->at(1) & 0x80))
		{
			if(_bl->debugLevel >= 2)
			{
				if(sentPacket) GD::out.printError("Error: NACK received from 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + " in response to " + sentPacket->hexString() + ".");
				else GD::out.printError("Error: NACK received from 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6));
				if(queue->getQueueType() == PacketQueueType::PAIRING) GD::out.printError("Try resetting the device to factory defaults before pairing it to this central.");
			}
			if(queue->getQueueType() == PacketQueueType::PAIRING) queue->clear(); //Abort
			else queue->pop(); //Otherwise the queue might persist forever. NACKS shouldn't be received when not pairing
			return;
		}
		if(queue->getQueueType() == PacketQueueType::PAIRING)
		{
			if(sentPacket && sentPacket->messageType() == 0x01 && sentPacket->messageSubtype() == 0)
			{
				if(!peerExists(packet->senderAddress()))
				{
					if(!queue->peer) return;
					try
					{
						_peersMutex.lock();
						_peers[queue->peer->getAddress()] = queue->peer;
						if(!queue->peer->getSerialNumber().empty()) _peersBySerial[queue->peer->getSerialNumber()] = queue->peer;
						_peersMutex.unlock();
						queue->peer->save(true, true, false);
						queue->peer->initializeCentralConfig();
						_peersMutex.lock();
						_peersById[queue->peer->getID()] = queue->peer;
						_peersMutex.unlock();
					}
					catch(const std::exception& ex)
					{
						_peersMutex.unlock();
						GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
					}
					catch(BaseLib::Exception& ex)
					{
						_peersMutex.unlock();
						GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
					}
					catch(...)
					{
						_peersMutex.unlock();
						GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
					}
					setInstallMode(nullptr, false, -1, nullptr, false);
					PVariable deviceDescriptions(new Variable(VariableType::tArray));
					deviceDescriptions->arrayValue = queue->peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
                    std::vector<uint64_t> newIds{ queue->peer->getID() };
					raiseRPCNewDevices(newIds, deviceDescriptions);
					GD::out.printMessage("Added peer 0x" + BaseLib::HelperFunctions::getHexString(queue->peer->getAddress()) + ".");
					addHomegearFeatures(queue->peer);
				}
			}
		}
		else if(queue->getQueueType() == PacketQueueType::UNPAIRING)
		{
			if(sentPacket && sentPacket->messageType() == 0xF0 && sentPacket->messageSubtype() == 0)
			{
				std::shared_ptr<MAXPeer> peer = getPeer(packet->senderAddress());
				if(peer) deletePeer(peer->getID());
			}
		}
		queue->pop(); //Messages are not popped by default.
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void MAXCentral::handlePairingRequest(int32_t messageCounter, std::shared_ptr<MAXPacket> packet)
{
	try
	{
		if(packet->destinationAddress() != 0 && packet->destinationAddress() != _address)
		{
			GD::out.printError("Error: Pairing packet rejected, because this peer is already paired to another central.");
			return;
		}
		if(packet->payload()->size() < 14)
		{
			GD::out.printError("Error: Pairing packet is too small (payload size has to be at least 14).");
			return;
		}

		std::string serialNumber((char*)&packet->payload()->at(4), 10);
		uint32_t deviceType = (packet->payload()->at(2) << 8) + packet->payload()->at(3);

		std::shared_ptr<MAXPeer> peer(getPeer(packet->senderAddress()));
		if(peer && (peer->getSerialNumber() != serialNumber || peer->getDeviceType() != deviceType))
		{
			GD::out.printError("Error: Pairing packet rejected, because a peer with the same address but different serial number or device type is already paired to this central.");
			return;
		}

		std::vector<uint8_t> payload;
		if(_pairing)
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(getPhysicalInterface(packet->senderAddress()), PacketQueueType::PAIRING, packet->senderAddress());

			if(!peer)
			{
				int32_t firmwareVersion = (packet->payload()->at(0) << 8) + packet->payload()->at(1);
				//Do not save here
				queue->peer = createPeer(packet->senderAddress(), firmwareVersion, deviceType, serialNumber, false);
				if(!queue->peer)
				{
					GD::out.printWarning("Warning: Device type 0x" + GD::bl->hf.getHexString(deviceType, 4) + " with firmware version 0x" + BaseLib::HelperFunctions::getHexString(firmwareVersion, 4) + " not supported. Sender address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + ".");
					return;
				}
				peer = queue->peer;
			}

			if(!peer->getRpcDevice())
			{
				GD::out.printWarning("Warning: Device type not supported. Sender address 0x" + BaseLib::HelperFunctions::getHexString(packet->senderAddress(), 6) + ".");
				return;
			}

			//INCLUSION
			payload.push_back(0);
			payload.push_back(0);
			std::shared_ptr<MAXPacket> configPacket(new MAXPacket(_messageCounter[0], 0x01, 0, _address, packet->senderAddress(), payload, peer->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio));
			queue->push(configPacket);
			queue->push(_messages->find(0x02, -1, std::vector<std::pair<uint32_t, int32_t>>()));
			payload.clear();
			_messageCounter[0]++;

			//WAKEUP
			/*payload.clear();
			payload.push_back(0);
			payload.push_back(0x3F);
			configPacket = std::shared_ptr<MAXPacket>(new MAXPacket(_messageCounter[0], 0xF1, 0, _address, packet->senderAddress(), payload, false));
			queue->push(configPacket);
			queue->push(_messages->find(DIRECTIONIN, 0x02, -1, std::vector<std::pair<uint32_t, int32_t>>()));
			payload.clear();
			_messageCounter[0]++;*/

			if(peer->getRpcDevice()->needsTime)
			{
				//TIME
				queue->push(getTimePacket(_messageCounter[0], packet->senderAddress(), false));
				queue->push(_messages->find(0x02, -1, std::vector<std::pair<uint32_t, int32_t>>()));
				payload.clear();
				_messageCounter[0]++;
			}
		}
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}
//End packet handlers

//RPC functions
PVariable MAXCentral::addLink(BaseLib::PRpcClientInfo clientInfo, std::string senderSerialNumber, int32_t senderChannelIndex, std::string receiverSerialNumber, int32_t receiverChannelIndex, std::string name, std::string description)
{
	try
	{
		if(senderSerialNumber.empty()) return Variable::createError(-2, "Given sender address is empty.");
		if(receiverSerialNumber.empty()) return Variable::createError(-2, "Given receiver address is empty.");
		std::shared_ptr<MAXPeer> sender = getPeer(senderSerialNumber);
		std::shared_ptr<MAXPeer> receiver = getPeer(receiverSerialNumber);
		if(!sender) return Variable::createError(-2, "Sender device not found.");
		if(!receiver) return Variable::createError(-2, "Receiver device not found.");
		return addLink(clientInfo, sender->getID(), senderChannelIndex, receiver->getID(), receiverChannelIndex, name, description);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::addLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannelIndex, uint64_t receiverID, int32_t receiverChannelIndex, std::string name, std::string description)
{
	try
	{
		if(senderID == 0) return Variable::createError(-2, "Sender id is not set.");
		if(receiverID == 0) return Variable::createError(-2, "Receiver is not set.");
		std::shared_ptr<MAXPeer> sender = getPeer(senderID);
		std::shared_ptr<MAXPeer> receiver = getPeer(receiverID);
		if(!sender) return Variable::createError(-2, "Sender device not found.");
		if(!receiver) return Variable::createError(-2, "Receiver device not found.");
		if(senderChannelIndex < 0) senderChannelIndex = 0;
		if(receiverChannelIndex < 0) receiverChannelIndex = 0;
		std::shared_ptr<HomegearDevice> senderRpcDevice = sender->getRpcDevice();
		std::shared_ptr<HomegearDevice> receiverRpcDevice = receiver->getRpcDevice();
		Functions::iterator senderFunctionIterator = senderRpcDevice->functions.find(senderChannelIndex);
		if(senderFunctionIterator == senderRpcDevice->functions.end()) return Variable::createError(-2, "Sender channel not found.");
		Functions::iterator receiverFunctionIterator = receiverRpcDevice->functions.find(receiverChannelIndex);
		if(receiverFunctionIterator == receiverRpcDevice->functions.end()) return Variable::createError(-2, "Receiver channel not found.");
		PFunction senderFunction = senderFunctionIterator->second;
		PFunction receiverFunction = receiverFunctionIterator->second;
		if(!senderFunction || !receiverFunction || senderFunction->linkSenderFunctionTypes.size() == 0 || receiverFunction->linkReceiverFunctionTypes.size() == 0) return Variable::createError(-6, "Link not supported.");
		bool validLink = false;
		for(LinkFunctionTypes::iterator i = senderFunction->linkSenderFunctionTypes.begin(); i != senderFunction->linkSenderFunctionTypes.end(); ++i)
		{
			for(LinkFunctionTypes::iterator j = receiverFunction->linkReceiverFunctionTypes.begin(); j != receiverFunction->linkReceiverFunctionTypes.end(); ++j)
			{
				if(*i == *j)
				{
					validLink = true;
					break;
				}
			}
			if(validLink) break;
		}
		if(!validLink) return Variable::createError(-6, "Link not supported.");

		std::shared_ptr<BaseLib::Systems::BasicPeer> senderPeer(new BaseLib::Systems::BasicPeer());
		senderPeer->address = sender->getAddress();
		senderPeer->channel = senderChannelIndex;
		senderPeer->id = sender->getID();
		senderPeer->serialNumber = sender->getSerialNumber();
		senderPeer->isSender = true;
		senderPeer->linkDescription = description;
		senderPeer->linkName = name;

		std::shared_ptr<BaseLib::Systems::BasicPeer> receiverPeer(new BaseLib::Systems::BasicPeer());
		receiverPeer->address = receiver->getAddress();
		receiverPeer->channel = receiverChannelIndex;
		receiverPeer->id = receiver->getID();
		receiverPeer->serialNumber = receiver->getSerialNumber();
		receiverPeer->linkDescription = description;
		receiverPeer->linkName = name;

		sender->addPeer(senderChannelIndex, receiverPeer);
		receiver->addPeer(receiverChannelIndex, senderPeer);

		std::shared_ptr<PacketQueue> pendingQueue(new PacketQueue(sender->getPhysicalInterface(), PacketQueueType::CONFIG));
		pendingQueue->noSending = true;

		std::vector<uint8_t> payload;

		payload.clear();
		//CONFIG_ADD_PEER
		payload.push_back(0);
		payload.push_back(receiver->getAddress() >> 16);
		payload.push_back((receiver->getAddress() >> 8) & 0xFF);
		payload.push_back(receiver->getAddress() & 0xFF);
		payload.push_back(senderChannelIndex);
		std::shared_ptr<MAXPacket> configPacket(new MAXPacket(_messageCounter[0], 0x20, 0, _address, sender->getAddress(), payload, sender->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio));
		pendingQueue->push(configPacket);
		pendingQueue->push(_messages->find(0x02, 0x02, std::vector<std::pair<uint32_t, int32_t>>()));
		_messageCounter[0]++;

		sender->pendingQueues->push(pendingQueue);
		sender->serviceMessages->setConfigPending(true);

		if((sender->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio) || (sender->getRXModes() & HomegearDevice::ReceiveModes::always))
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(sender->getPhysicalInterface(), PacketQueueType::CONFIG, sender->getAddress());
			queue->peer = sender;
			queue->push(sender->pendingQueues);
		}

		raiseRPCUpdateDevice(sender->getID(), senderChannelIndex, sender->getSerialNumber() + ":" + std::to_string(senderChannelIndex), 1);

		int32_t waitIndex = 0;
		while(_queueManager.get(sender->getAddress()) && waitIndex < 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitIndex++;
		}
		if(!_queueManager.get(sender->getAddress())) sender->serviceMessages->setConfigPending(false);

		pendingQueue.reset(new PacketQueue(receiver->getPhysicalInterface(), PacketQueueType::CONFIG));
		pendingQueue->noSending = true;

		payload.clear();
		//CONFIG_ADD_PEER
		payload.push_back(0x00);
		payload.push_back(sender->getAddress() >> 16);
		payload.push_back((sender->getAddress() >> 8) & 0xFF);
		payload.push_back(sender->getAddress() & 0xFF);
		payload.push_back(receiverChannelIndex);
		configPacket.reset(new MAXPacket(_messageCounter[0], 0x20, 0, _address, receiver->getAddress(), payload, receiver->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio));
		pendingQueue->push(configPacket);
		pendingQueue->push(_messages->find(0x02, 0x02, std::vector<std::pair<uint32_t, int32_t>>()));
		_messageCounter[0]++;

		receiver->pendingQueues->push(pendingQueue);
		receiver->serviceMessages->setConfigPending(true);

		if((receiver->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio) || (receiver->getRXModes() & HomegearDevice::ReceiveModes::always))
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(receiver->getPhysicalInterface(), PacketQueueType::CONFIG, receiver->getAddress());
			queue->peer = receiver;
			queue->push(receiver->pendingQueues);
		}

		raiseRPCUpdateDevice(receiver->getID(), receiverChannelIndex, receiver->getSerialNumber() + ":" + std::to_string(receiverChannelIndex), 1);

		waitIndex = 0;
		while(_queueManager.get(receiver->getAddress()) && waitIndex < 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitIndex++;
		}
		if(!_queueManager.get(receiver->getAddress())) receiver->serviceMessages->setConfigPending(false);

		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags)
{
	try
	{
		if(serialNumber.empty()) return Variable::createError(-2, "Unknown device.");
		if(serialNumber[0] == '*') return Variable::createError(-2, "Cannot delete virtual device.");
		std::shared_ptr<MAXPeer> peer = getPeer(serialNumber);
		if(!peer) return PVariable(new Variable(VariableType::tVoid));
		return deleteDevice(clientInfo, peer->getID(), flags);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t flags)
{
	try
	{
		if(peerID == 0) return Variable::createError(-2, "Unknown device.");
		if(peerID & 0x80000000) return Variable::createError(-2, "Cannot delete virtual device.");
		std::shared_ptr<MAXPeer> peer = getPeer(peerID);
		if(!peer) return PVariable(new Variable(VariableType::tVoid));
		uint64_t id = peer->getID();

		bool defer = flags & 0x04;
		bool force = flags & 0x02;
		_unpairThreadMutex.lock();
		_bl->threadManager.join(_unpairThread);
		_bl->threadManager.start(_unpairThread, false, &MAXCentral::reset, this, id);
		_unpairThreadMutex.unlock();
		//Force delete
		if(force) deletePeer(peer->getID());
		else
		{
			int32_t waitIndex = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			while(_queueManager.get(peer->getAddress()) && peerExists(id) && waitIndex < 20)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				waitIndex++;
			}
		}

		if(!defer && !force && peerExists(id)) return Variable::createError(-1, "No answer from device.");

		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

std::shared_ptr<Variable> MAXCentral::getInstallMode(BaseLib::PRpcClientInfo clientInfo)
{
	try
	{
		return std::shared_ptr<Variable>(new Variable(_timeLeftInPairingMode));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::putParamset(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t channel, ParameterGroup::Type::Enum type, std::string remoteSerialNumber, int32_t remoteChannel, PVariable paramset)
{
	try
	{
		std::shared_ptr<MAXPeer> peer(getPeer(serialNumber));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		uint64_t remoteID = 0;
		if(!remoteSerialNumber.empty())
		{
			std::shared_ptr<MAXPeer> remotePeer(getPeer(remoteSerialNumber));
			if(!remotePeer)
			{
				if(remoteSerialNumber != _serialNumber) return Variable::createError(-3, "Remote peer is unknown.");
			}
			else remoteID = remotePeer->getID();
		}
		PVariable result = peer->putParamset(clientInfo, channel, type, remoteID, remoteChannel, paramset, false);
		if(result->errorStruct) return result;
		int32_t waitIndex = 0;
		while(_queueManager.get(peer->getAddress()) && waitIndex < 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitIndex++;
		}
		if(!_queueManager.get(peer->getAddress())) peer->serviceMessages->setConfigPending(false);
		return result;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::putParamset(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable paramset, bool checkAcls)
{
	try
	{
		std::shared_ptr<MAXPeer> peer(getPeer(peerID));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		PVariable result = peer->putParamset(clientInfo, channel, type, remoteID, remoteChannel, paramset, checkAcls);
		if(result->errorStruct) return result;
		int32_t waitIndex = 0;
		while(_queueManager.get(peer->getAddress()) && waitIndex < 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitIndex++;
		}
		if(!_queueManager.get(peer->getAddress())) peer->serviceMessages->setConfigPending(false);
		return result;
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::removeLink(BaseLib::PRpcClientInfo clientInfo, std::string senderSerialNumber, int32_t senderChannelIndex, std::string receiverSerialNumber, int32_t receiverChannelIndex)
{
	try
	{
		if(senderSerialNumber.empty()) return Variable::createError(-2, "Given sender address is empty.");
		if(receiverSerialNumber.empty()) return Variable::createError(-2, "Given receiver address is empty.");
		std::shared_ptr<MAXPeer> sender = getPeer(senderSerialNumber);
		std::shared_ptr<MAXPeer> receiver = getPeer(receiverSerialNumber);
		if(!sender) return Variable::createError(-2, "Sender device not found.");
		if(!receiver) return Variable::createError(-2, "Receiver device not found.");
		return removeLink(clientInfo, sender->getID(), senderChannelIndex, receiver->getID(), receiverChannelIndex);
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::removeLink(BaseLib::PRpcClientInfo clientInfo, uint64_t senderID, int32_t senderChannelIndex, uint64_t receiverID, int32_t receiverChannelIndex)
{
	try
	{
		if(senderID == 0) return Variable::createError(-2, "Sender id is not set.");
		if(receiverID == 0) return Variable::createError(-2, "Receiver id is not set.");
		std::shared_ptr<MAXPeer> sender = getPeer(senderID);
		std::shared_ptr<MAXPeer> receiver = getPeer(receiverID);
		if(!sender) return Variable::createError(-2, "Sender device not found.");
		if(!receiver) return Variable::createError(-2, "Receiver device not found.");
		if(senderChannelIndex < 0) senderChannelIndex = 0;
		if(receiverChannelIndex < 0) receiverChannelIndex = 0;
		std::string senderSerialNumber = sender->getSerialNumber();
		std::string receiverSerialNumber = receiver->getSerialNumber();
		std::shared_ptr<HomegearDevice> senderRpcDevice = sender->getRpcDevice();
		std::shared_ptr<HomegearDevice> receiverRpcDevice = receiver->getRpcDevice();
		if(senderRpcDevice->functions.find(senderChannelIndex) == senderRpcDevice->functions.end()) return Variable::createError(-2, "Sender channel not found.");
		if(receiverRpcDevice->functions.find(receiverChannelIndex) == receiverRpcDevice->functions.end()) return Variable::createError(-2, "Receiver channel not found.");
		if(!sender->getPeer(senderChannelIndex, receiver->getAddress()) && !receiver->getPeer(receiverChannelIndex, sender->getAddress())) return Variable::createError(-6, "Devices are not paired to each other.");

		sender->removePeer(senderChannelIndex, receiver->getID(), receiverChannelIndex);
		receiver->removePeer(receiverChannelIndex, sender->getID(), senderChannelIndex);

		std::shared_ptr<PacketQueue> pendingQueue(new PacketQueue(sender->getPhysicalInterface(), PacketQueueType::CONFIG));
		pendingQueue->noSending = true;

		std::vector<uint8_t> payload;
		payload.push_back(0);
		payload.push_back(receiver->getAddress() >> 16);
		payload.push_back((receiver->getAddress() >> 8) & 0xFF);
		payload.push_back(receiver->getAddress() & 0xFF);
		payload.push_back(senderChannelIndex);
		std::shared_ptr<MAXPacket> configPacket(new MAXPacket(_messageCounter[0], 0x21, 0, _address, sender->getAddress(), payload, sender->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio));
		pendingQueue->push(configPacket);
		pendingQueue->push(_messages->find(0x02, 0x02, std::vector<std::pair<uint32_t, int32_t>>()));
		_messageCounter[0]++;

		sender->pendingQueues->push(pendingQueue);
		sender->serviceMessages->setConfigPending(true);

		if((sender->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio) || (sender->getRXModes() & HomegearDevice::ReceiveModes::always))
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(sender->getPhysicalInterface(), PacketQueueType::CONFIG, sender->getAddress());
			queue->peer = sender;
			queue->push(sender->pendingQueues);
		}

		raiseRPCUpdateDevice(sender->getID(), senderChannelIndex, senderSerialNumber + ":" + std::to_string(senderChannelIndex), 1);

		int32_t waitIndex = 0;
		while(_queueManager.get(sender->getAddress()) && waitIndex < 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitIndex++;
		}
		if(!_queueManager.get(sender->getAddress())) sender->serviceMessages->setConfigPending(false);

		pendingQueue.reset(new PacketQueue(receiver->getPhysicalInterface(), PacketQueueType::CONFIG));
		pendingQueue->noSending = true;

		payload.clear();
		payload.push_back(0);
		payload.push_back(sender->getAddress() >> 16);
		payload.push_back((sender->getAddress() >> 8) & 0xFF);
		payload.push_back(sender->getAddress() & 0xFF);
		payload.push_back(receiverChannelIndex);
		configPacket.reset(new MAXPacket(_messageCounter[0], 0x21, 0, _address, receiver->getAddress(), payload, receiver->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio));
		pendingQueue->push(configPacket);
		pendingQueue->push(_messages->find(0x02, 0x02, std::vector<std::pair<uint32_t, int32_t>>()));
		_messageCounter[0]++;

		receiver->pendingQueues->push(pendingQueue);
		receiver->serviceMessages->setConfigPending(true);

		if((receiver->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio) || (receiver->getRXModes() & HomegearDevice::ReceiveModes::always))
		{
			std::shared_ptr<PacketQueue> queue = _queueManager.createQueue(receiver->getPhysicalInterface(), PacketQueueType::CONFIG, receiver->getAddress());
			queue->peer = receiver;
			queue->push(receiver->pendingQueues);
		}

		raiseRPCUpdateDevice(receiver->getID(), receiverChannelIndex, receiverSerialNumber + ":" + std::to_string(receiverChannelIndex), 1);

		waitIndex = 0;
		while(_queueManager.get(receiver->getAddress()) && waitIndex < 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			waitIndex++;
		}
		if(!_queueManager.get(receiver->getAddress())) receiver->serviceMessages->setConfigPending(false);

		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(BaseLib::Exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch(...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Variable::createError(-32500, "Unknown application error.");
}

void MAXCentral::pairingModeTimer(int32_t duration, bool debugOutput)
{
	try
	{
		_pairing = true;
		if(debugOutput) GD::out.printInfo("Info: Pairing mode enabled.");
		_timeLeftInPairingMode = duration;
		int64_t startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		int64_t timePassed = 0;
		while(timePassed < ((int64_t)duration * 1000) && !_stopPairingModeThread)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - startTime;
			_timeLeftInPairingMode = duration - (timePassed / 1000);
		}
		_timeLeftInPairingMode = 0;
		_pairing = false;
		if(debugOutput) GD::out.printInfo("Info: Pairing mode disabled.");
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

std::shared_ptr<Variable> MAXCentral::setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput)
{
	try
	{
		_pairingModeThreadMutex.lock();
		if(_disposing)
		{
			_pairingModeThreadMutex.unlock();
			return Variable::createError(-32500, "Central is disposing.");
		}
		_stopPairingModeThread = true;
		_bl->threadManager.join(_pairingModeThread);
		_stopPairingModeThread = false;
		_timeLeftInPairingMode = 0;
		if(on && duration >= 5)
		{
			_timeLeftInPairingMode = duration; //It's important to set it here, because the thread often doesn't completely initialize before getInstallMode requests _timeLeftInPairingMode
			_bl->threadManager.start(_pairingModeThread, true, &MAXCentral::pairingModeTimer, this, duration, debugOutput);
		}
		_pairingModeThreadMutex.unlock();
		return PVariable(new Variable(VariableType::tVoid));
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    _pairingModeThreadMutex.unlock();
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MAXCentral::setInterface(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, std::string interfaceID)
{
	try
	{
		std::shared_ptr<MAXPeer> peer(getPeer(peerID));
		if(!peer) return Variable::createError(-2, "Unknown device.");
		return peer->setInterface(clientInfo, interfaceID);
	}
	catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Variable::createError(-32500, "Unknown application error.");
}
//End RPC functions
}
