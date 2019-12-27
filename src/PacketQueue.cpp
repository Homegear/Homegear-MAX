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

#include "PacketQueue.h"
#include "MAXMessages.h"
#include "PendingQueues.h"
#include <homegear-base/BaseLib.h>
#include "GD.h"

namespace MAX
{
PacketQueue::PacketQueue()
{
	_queueType = PacketQueueType::EMPTY;
	_lastPop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	_physicalInterface = GD::defaultPhysicalInterface;
	_disposing = false;
	_stopResendThread = false;
	_stopPopWaitThread = false;
	_workingOnPendingQueue = false;
	noSending = false;
}

PacketQueue::PacketQueue(std::shared_ptr<BaseLib::Systems::IPhysicalInterface> physicalInterface) : PacketQueue()
{
	if(physicalInterface) _physicalInterface = physicalInterface;
}

PacketQueue::PacketQueue(std::shared_ptr<BaseLib::Systems::IPhysicalInterface> physicalInterface, PacketQueueType queueType) : PacketQueue(physicalInterface)
{
	_queueType = queueType;
}

PacketQueue::~PacketQueue()
{
	try
	{
		dispose();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::serialize(std::vector<uint8_t>& encodedData)
{
	try
	{
		BaseLib::BinaryEncoder encoder(GD::bl);
		_queueMutex.lock();
		if(_queue.size() == 0)
		{
			_queueMutex.unlock();
			return;
		}
		encoder.encodeByte(encodedData, (int32_t)_queueType);
		encoder.encodeInteger(encodedData, _queue.size());
		for(std::list<PacketQueueEntry>::iterator i = _queue.begin(); i != _queue.end(); ++i)
		{
			encoder.encodeByte(encodedData, (uint8_t)i->getType());
			encoder.encodeBoolean(encodedData, i->stealthy);
			encoder.encodeBoolean(encodedData, i->forceResend);
			if(!i->getPacket()) encoder.encodeBoolean(encodedData, false);
			else
			{
				encoder.encodeBoolean(encodedData, true);
				std::vector<uint8_t> packet = i->getPacket()->byteArray();
				encoder.encodeByte(encodedData, packet.size());
				encodedData.insert(encodedData.end(), packet.begin(), packet.end());
				encoder.encodeBoolean(encodedData, i->getPacket()->getBurst());
			}
			std::shared_ptr<MAXMessage> message = i->getMessage();
			if(!message) encoder.encodeBoolean(encodedData, false);
			else
			{
				encoder.encodeBoolean(encodedData, true);
				uint8_t dummy = 0;
				encoder.encodeByte(encodedData, dummy);
				encoder.encodeByte(encodedData, message->getMessageType());
				encoder.encodeByte(encodedData, message->getMessageSubtype());
				std::vector<std::pair<uint32_t, int32_t>>* subtypes = message->getSubtypes();
				encoder.encodeByte(encodedData, subtypes->size());
				for(std::vector<std::pair<uint32_t, int32_t>>::iterator j = subtypes->begin(); j != subtypes->end(); ++j)
				{
					encoder.encodeByte(encodedData, j->first);
					encoder.encodeByte(encodedData, j->second);
				}
			}
			encoder.encodeString(encodedData, parameterName);
			encoder.encodeInteger(encodedData, channel);
			std::string id = _physicalInterface->getID();
			encoder.encodeString(encodedData, id);
		}
	}
	catch(const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	_queueMutex.unlock();
}

void PacketQueue::unserialize(std::shared_ptr<std::vector<char>> serializedData, uint32_t position)
{
	try
	{
		BaseLib::BinaryDecoder decoder(GD::bl);
		_queueMutex.lock();
		_queueType = (PacketQueueType)decoder.decodeByte(*serializedData, position);
		uint32_t queueSize = decoder.decodeInteger(*serializedData, position);
		for(uint32_t i = 0; i < queueSize; i++)
		{
			_queue.push_back(PacketQueueEntry());
			PacketQueueEntry* entry = &_queue.back();
			entry->setType((QueueEntryType)decoder.decodeByte(*serializedData, position));
			entry->stealthy = decoder.decodeBoolean(*serializedData, position);
			entry->forceResend = decoder.decodeBoolean(*serializedData, position);
			int32_t packetExists = decoder.decodeBoolean(*serializedData, position);
			if(packetExists)
			{
				std::vector<uint8_t> packetData;
				uint32_t dataSize = decoder.decodeByte(*serializedData, position);
				if(position + dataSize <= serializedData->size()) packetData.insert(packetData.end(), serializedData->begin() + position, serializedData->begin() + position + dataSize);
				position += dataSize;
				std::shared_ptr<MAXPacket> packet(new MAXPacket(packetData, false));
				packet->setBurst(decoder.decodeBoolean(*serializedData, position));
				entry->setPacket(packet, false);
			}
			int32_t messageExists = decoder.decodeBoolean(*serializedData, position);
			if(messageExists)
			{
				decoder.decodeByte(*serializedData, position);
				int32_t messageType = decoder.decodeByte(*serializedData, position);
				int32_t messageSubtype = decoder.decodeByte(*serializedData, position);
				uint32_t subtypeSize = decoder.decodeByte(*serializedData, position);
				std::vector<std::pair<uint32_t, int32_t>> subtypes;
				for(uint32_t j = 0; j < subtypeSize; j++)
				{
					subtypes.push_back(std::pair<uint32_t, int32_t>(decoder.decodeByte(*serializedData, position), decoder.decodeByte(*serializedData, position)));
				}
				std::shared_ptr<MAXCentral> central(std::dynamic_pointer_cast<MAXCentral>(GD::family->getCentral()));
				if(central) entry->setMessage(central->getMessages()->find(messageType, messageSubtype, subtypes), false);
			}
			parameterName = decoder.decodeString(*serializedData, position);
			channel = decoder.decodeInteger(*serializedData, position);
			std::string physicalInterfaceID = decoder.decodeString(*serializedData, position);
			if(GD::physicalInterfaces.find(physicalInterfaceID) != GD::physicalInterfaces.end()) _physicalInterface = GD::physicalInterfaces.at(physicalInterfaceID);
			else _physicalInterface = GD::defaultPhysicalInterface;
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    	clear();
    	_pendingQueues.reset();
    }
    if(!_physicalInterface) _physicalInterface = GD::defaultPhysicalInterface;
    _queueMutex.unlock();
}

void PacketQueue::dispose()
{
	try
	{
		if(_disposing) return;
		_disposing = true;
		_startResendThreadMutex.lock();
		GD::bl->threadManager.join(_startResendThread);
		_startResendThreadMutex.unlock();
		_pushPendingQueueThreadMutex.lock();
		GD::bl->threadManager.join(_pushPendingQueueThread);
		_pushPendingQueueThreadMutex.unlock();
		_sendThreadMutex.lock();
		GD::bl->threadManager.join(_sendThread);
        _sendThreadMutex.unlock();
		stopResendThread();
		stopPopWaitThread();
		_queueMutex.lock();
		_queue.clear();
		_pendingQueues.reset();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    	_sendThreadMutex.unlock();
    	_pushPendingQueueThreadMutex.unlock();
    	_startResendThreadMutex.unlock();
    }
    _queueMutex.unlock();
}

bool PacketQueue::isEmpty()
{
	return _queue.empty() && (!_pendingQueues || _pendingQueues->empty());
}

bool PacketQueue::pendingQueuesEmpty()
{
	 return (!_pendingQueues || _pendingQueues->empty());
}

void PacketQueue::resend(uint32_t threadId, bool burst)
{
	try
	{
		//Add ~100 milliseconds after popping, otherwise the first resend is 100 ms too early.
		int64_t timeSinceLastPop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - _lastPop;
		int32_t i = 0;
		std::chrono::milliseconds sleepingTime;
		uint32_t responseDelay = _physicalInterface->responseDelay();
		if(timeSinceLastPop < responseDelay)
		{
			sleepingTime = std::chrono::milliseconds((responseDelay - timeSinceLastPop) / 3);
			if(_resendCounter == 0)
			{
				while(!_stopResendThread && i < 3)
				{
					std::this_thread::sleep_for(sleepingTime);
					i++;
				}
			}
		}
		if(_stopResendThread) return;
		if(_resendCounter < 3)
		{
			//Sleep for 200/3000 ms
			i = 0;
			if(burst)
			{
				longKeepAlive();
				sleepingTime = std::chrono::milliseconds(300);
			}
			else
			{
				keepAlive();
				sleepingTime = std::chrono::milliseconds(20);
			}
			while(!_stopResendThread && i < 10)
			{
				std::this_thread::sleep_for(sleepingTime);
				i++;
			}
		}
		else
		{
			//Sleep for 400/4000 ms
			i = 0;
			if(burst)
			{
				longKeepAlive();
				sleepingTime = std::chrono::milliseconds(200);
			}
			else
			{
				keepAlive();
				sleepingTime = std::chrono::milliseconds(20);
			}
			while(!_stopResendThread && i < 20)
			{
				std::this_thread::sleep_for(sleepingTime);
				i++;
			}
		}
		if(_stopResendThread) return;

		_queueMutex.lock();
		if(!_queue.empty() && !_stopResendThread)
		{
			if(_stopResendThread)
			{
				_queueMutex.unlock();
				return;
			}
			bool forceResend = _queue.front().forceResend;
			if(!noSending)
			{
				GD::out.printDebug("Sending from resend thread " + std::to_string(threadId) + " of queue " + std::to_string(id) + ".");
				std::shared_ptr<MAXPacket> packet = _queue.front().getPacket();
				if(!packet) return;
				bool stealthy = _queue.front().stealthy;
				_queueMutex.unlock();
				_sendThreadMutex.lock();
				GD::bl->threadManager.join(_sendThread);
				if(_stopResendThread || _disposing)
				{
					_sendThreadMutex.unlock();
					return;
				}
				if(burst) packet->setBurst(true);
				GD::bl->threadManager.start(_sendThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::send, this, packet, stealthy);
				_sendThreadMutex.unlock();
			}
			else _queueMutex.unlock(); //Has to be unlocked before startResendThread
			if(_stopResendThread) return;
			if(_resendCounter < ((signed)retries - 2)) //This actually means that the message will be sent three times all together if there is no response
			{
				_resendCounter++;
				_startResendThreadMutex.lock();
				if(_disposing)
				{
					_startResendThreadMutex.unlock();
					return;
				}
				GD::bl->threadManager.join(_startResendThread);
				GD::bl->threadManager.start(_startResendThread, true, &PacketQueue::startResendThread, this, forceResend);
				_startResendThreadMutex.unlock();
			}
			else _resendCounter = 0;
		}
		else _queueMutex.unlock();
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
		_sendThreadMutex.unlock();
		_startResendThreadMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::push(std::shared_ptr<MAXPacket> packet, bool stealthy, bool forceResend)
{
	try
	{
		if(_disposing) return;
		PacketQueueEntry entry;
		entry.setPacket(packet, true);
		entry.stealthy = stealthy;
		entry.forceResend = forceResend;
		_queueMutex.lock();
		if(!noSending && (_queue.size() == 0 || (_queue.size() == 1 && _queue.front().getType() == QueueEntryType::MESSAGE)))
		{
			_queue.push_back(entry);
			_queueMutex.unlock();
			_resendCounter = 0;
			if(!noSending)
			{
				_sendThreadMutex.lock();
				if(_disposing)
				{
					_sendThreadMutex.unlock();
					return;
				}
				GD::bl->threadManager.join(_sendThread);
				GD::bl->threadManager.start(_sendThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::send, this, entry.getPacket(), entry.stealthy);
				_sendThreadMutex.unlock();
				startResendThread(forceResend);
			}
		}
		else
		{
			_queue.push_back(entry);
			_queueMutex.unlock();
		}
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
		_sendThreadMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::push(std::shared_ptr<PendingQueues>& pendingQueues)
{
	try
	{
		if(_disposing) return;
		_queueMutex.lock();
		_pendingQueues = pendingQueues;
		if(_queue.empty())
		{
			 _queueMutex.unlock();
			pushPendingQueue();
		}
		else  _queueMutex.unlock();
	}
	catch(const std::exception& ex)
    {
        _queueMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::push(std::shared_ptr<PacketQueue> pendingQueue, bool popImmediately, bool clearPendingQueues)
{
	try
	{
		if(_disposing) return;
		if(!pendingQueue) return;
		_queueMutex.lock();
		if(!_pendingQueues) _pendingQueues.reset(new PendingQueues());
		if(clearPendingQueues) _pendingQueues->clear();
		_pendingQueues->push(pendingQueue);
		_queueMutex.unlock();
		pushPendingQueue();
		_queueMutex.lock();
		if(popImmediately)
		{
			if(!_pendingQueues->empty()) _pendingQueues->pop(pendingQueueID);
			_workingOnPendingQueue = false;
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    _queueMutex.unlock();
}

void PacketQueue::push(std::shared_ptr<MAXMessage> message, bool forceResend)
{
	try
	{
		if(_disposing) return;
		if(!message) return;
		PacketQueueEntry entry;
		entry.setMessage(message, true);
		entry.forceResend = forceResend;
		_queueMutex.lock();
		_queue.push_back(entry);
		_queueMutex.unlock();
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
		_sendThreadMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::pushFront(std::shared_ptr<MAXPacket> packet, bool stealthy, bool popBeforePushing, bool forceResend)
{
	try
	{
		if(_disposing) return;
		keepAlive();
		if(popBeforePushing)
		{
			GD::out.printDebug("Popping from MAX! queue and pushing packet at the front: " + std::to_string(id));
			if(_popWaitThread.joinable()) _stopPopWaitThread = true;
			_resendThreadMutex.lock();
			if(_resendThread.joinable()) _stopResendThread = true;
			_resendThreadMutex.unlock();
			_queueMutex.lock();
			_queue.pop_front();
			_queueMutex.unlock();
		}
		PacketQueueEntry entry;
		entry.setPacket(packet, true);
		entry.stealthy = stealthy;
		entry.forceResend = forceResend;
		if(!noSending)
		{
			_queueMutex.lock();
			_queue.push_front(entry);
			_queueMutex.unlock();
			_resendCounter = 0;
			if(!noSending)
			{
				_sendThreadMutex.lock();
				if(_disposing)
				{
					_sendThreadMutex.unlock();
					return;
				}
				GD::bl->threadManager.join(_sendThread);
				GD::bl->threadManager.start(_sendThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::send, this, entry.getPacket(), entry.stealthy);
				_sendThreadMutex.unlock();
				startResendThread(forceResend);
			}
		}
		else
		{
			_queueMutex.lock();
			_queue.push_front(entry);
			_queueMutex.unlock();
		}
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
		_sendThreadMutex.unlock();
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::stopPopWaitThread()
{
	try
	{
		_stopPopWaitThread = true;
		GD::bl->threadManager.join(_popWaitThread);
		_stopPopWaitThread = false;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::popWait(uint32_t waitingTime)
{
	try
	{
		if(_disposing) return;
		stopResendThread();
		stopPopWaitThread();
		GD::bl->threadManager.start(_popWaitThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::popWaitThread, this, _popWaitThreadId++, waitingTime);
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::popWaitThread(uint32_t threadId, uint32_t waitingTime)
{
	try
	{
		std::chrono::milliseconds sleepingTime(25);
		uint32_t i = 0;
		while(!_stopPopWaitThread && i < waitingTime)
		{
			std::this_thread::sleep_for(sleepingTime);
			i += 25;
		}
		if(!_stopPopWaitThread)
		{
			pop();
		}
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::setWakeOnRadio(bool value)
{
	try
	{
		_queueMutex.lock();
		if(_queue.empty())
		{
			_queueMutex.unlock();
			return;
		}
		if(_queue.front().getPacket())
		{
			_queue.front().getPacket()->setBurst(value);
		}
		_queueMutex.unlock();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::send(std::shared_ptr<MAXPacket> packet, bool stealthy)
{
	try
	{
		if(noSending || _disposing) return;
		if(packet->getBurst()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::shared_ptr<MAXCentral> central(std::dynamic_pointer_cast<MAXCentral>(GD::family->getCentral()));
		if(central) central->sendPacket(_physicalInterface, packet, stealthy);
		else GD::out.printError("Error: Central pointer of queue " + std::to_string(id) + " is null.");
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::stopResendThread()
{
	try
	{
		_resendThreadMutex.lock();
		_stopResendThread = true;
		GD::bl->threadManager.join(_resendThread);
		_stopResendThread = false;
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    _resendThreadMutex.unlock();
}

void PacketQueue::startResendThread(bool force)
{
	try
	{
		if(noSending || _disposing) return;
		_queueMutex.lock();
		if(_queue.empty())
		{
			_queueMutex.unlock();
			return;
		}
		int32_t destinationAddress = 0;
		bool burst = false;
		if(_queue.front().getPacket())
		{
			destinationAddress = _queue.front().getPacket()->destinationAddress();
			burst = _queue.front().getPacket()->getBurst();
		}

		_queueMutex.unlock();
		if(destinationAddress != 0 || force) //Resend when no response?
		{
			if(peer && (peer->getRXModes() & HomegearDevice::ReceiveModes::wakeOnRadio)) burst = true;
			_resendThreadMutex.lock();
			try
			{
				_stopResendThread = true;
				GD::bl->threadManager.join(_resendThread);
				_stopResendThread = false;
				GD::bl->threadManager.start(_resendThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::resend, this, _resendThreadId++, burst);
			}
			catch(const std::exception& ex)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
			_resendThreadMutex.unlock();
		}
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::clear()
{
	try
	{
		stopResendThread();
		_queueMutex.lock();
		if(_pendingQueues) _pendingQueues->clear();
		_queue.clear();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    _queueMutex.unlock();
}

void PacketQueue::sleepAndPushPendingQueue()
{
	try
	{
		if(_disposing) return;
		std::this_thread::sleep_for(std::chrono::milliseconds(_physicalInterface->responseDelay()));
		pushPendingQueue();
	}
	catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::pushPendingQueue()
{
	try
	{
		if(_disposing) return;
		_queueMutex.lock();
		if(_disposing)
		{
			_queueMutex.unlock();
			return;
		}
		if(!_pendingQueues || _pendingQueues->empty())
		{
			_queueMutex.unlock();
			return;
		}
		while(!_pendingQueues->empty() && (!_pendingQueues->front() || _pendingQueues->front()->isEmpty()))
		{
			GD::out.printDebug("Debug: Empty queue was pushed.");
			_pendingQueues->pop();
		}
		if(_pendingQueues->empty())
		{
			_queueMutex.unlock();
			return;
		}
		std::shared_ptr<PacketQueue> queue = _pendingQueues->front();
		_queueMutex.unlock();
		if(!queue) return; //Not really necessary, as the mutex is locked, but I had a segmentation fault in this function, so just to make
		_queueType = queue->getQueueType();
		retries = queue->retries;
		pendingQueueID = queue->pendingQueueID;
		for(std::list<PacketQueueEntry>::iterator i = queue->getQueue()->begin(); i != queue->getQueue()->end(); ++i)
		{
			if(!noSending && i->getType() == QueueEntryType::PACKET && (_queue.size() == 0 || (_queue.size() == 1 && _queue.front().getType() == QueueEntryType::MESSAGE)))
			{
				_queueMutex.lock();
				_queue.push_back(*i);
				_queueMutex.unlock();
				_resendCounter = 0;
				if(!noSending)
				{
					_sendThreadMutex.lock();
					if(_disposing)
					{
						_sendThreadMutex.unlock();
						return;
					}
					GD::bl->threadManager.join(_sendThread);
					_lastPop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					GD::bl->threadManager.start(_sendThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::send, this, i->getPacket(), i->stealthy);
					_sendThreadMutex.unlock();
					startResendThread(i->forceResend);
				}
			}
			else
			{
				_queueMutex.lock();
				_queue.push_back(*i);
				_queueMutex.unlock();
			}
		}
		_workingOnPendingQueue = true;
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
		_sendThreadMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::keepAlive()
{
	if(_disposing) return;
	if(lastAction) *lastAction = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void PacketQueue::longKeepAlive()
{
	if(_disposing) return;
	if(lastAction) *lastAction = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 5000;
}

void PacketQueue::nextQueueEntry()
{
	try
	{
		if(_disposing) return;
		_queueMutex.lock();
		if(_queue.empty()) {
			if(_workingOnPendingQueue && !_pendingQueues->empty()) _pendingQueues->pop(pendingQueueID);
			if(!_pendingQueues || (_pendingQueues && _pendingQueues->empty()))
			{
				_stopResendThread = true;
				GD::out.printInfo("Info: Queue " + std::to_string(id) + " is empty and there are no pending queues.");
				_pendingQueues.reset();
				_workingOnPendingQueue = false;
				_queueMutex.unlock();
				return;
			}
			else
			{
				_queueMutex.unlock();
				GD::out.printDebug("Queue " + std::to_string(id) + " is empty. Pushing pending queue...");
				_pushPendingQueueThreadMutex.lock();
				if(_disposing)
				{
					_pushPendingQueueThreadMutex.unlock();
					return;
				}
				GD::bl->threadManager.join(_pushPendingQueueThread);
				GD::bl->threadManager.start(_pushPendingQueueThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::pushPendingQueue, this);
				_pushPendingQueueThreadMutex.unlock();
				return;
			}
		}
		if(_queue.front().getType() == QueueEntryType::PACKET)
		{
			_resendCounter = 0;
			if(!noSending)
			{
				bool forceResend = _queue.front().forceResend;
				std::shared_ptr<MAXPacket> packet = _queue.front().getPacket();
				bool stealthy = _queue.front().stealthy;
				_queueMutex.unlock();
				_sendThreadMutex.lock();
				if(_disposing)
				{
					_sendThreadMutex.unlock();
					return;
				}
				GD::bl->threadManager.join(_sendThread);
				GD::bl->threadManager.start(_sendThread, true, GD::bl->settings.packetQueueThreadPriority(), GD::bl->settings.packetQueueThreadPolicy(), &PacketQueue::send, this, packet, stealthy);
				_sendThreadMutex.unlock();
				startResendThread(forceResend);
			}
			else _queueMutex.unlock();
		}
		else _queueMutex.unlock();
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
		_sendThreadMutex.unlock();
		_pushPendingQueueThreadMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void PacketQueue::pop()
{
	try
	{
		if(_disposing) return;
		keepAlive();
		GD::out.printDebug("Popping from MAX! queue: " + std::to_string(id));
		if(_popWaitThread.joinable()) _stopPopWaitThread = true;
		_resendThreadMutex.lock();
		if(_resendThread.joinable()) _stopResendThread = true;
		_resendThreadMutex.unlock();
		_lastPop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		_queueMutex.lock();
		if(_queue.empty())
		{
			_queueMutex.unlock();
			return;
		}
		_queue.pop_front();
		if(GD::bl->debugLevel >= 5 && !_queue.empty())
		{
			if(_queue.front().getType() == QueueEntryType::PACKET && _queue.front().getPacket()) GD::out.printDebug("Packet now at front of queue: " + _queue.front().getPacket()->hexString());
			else if(_queue.front().getType() == QueueEntryType::MESSAGE && _queue.front().getMessage()) GD::out.printDebug("Message now at front: Message type: 0x" + BaseLib::HelperFunctions::getHexString(_queue.front().getMessage()->getMessageType()) + " Message subtype: 0x" + BaseLib::HelperFunctions::getHexString(_queue.front().getMessage()->getMessageSubtype()));
		}
		_queueMutex.unlock();
		nextQueueEntry();
	}
	catch(const std::exception& ex)
    {
		_queueMutex.unlock();
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}
}
