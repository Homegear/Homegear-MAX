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

#ifndef MAXPACKET_H_
#define MAXPACKET_H_

#include <homegear-base/BaseLib.h>

#include <map>

namespace MAX
{
class MAXPacket : public BaseLib::Systems::Packet
{
public:
    //Properties
    MAXPacket();
    MAXPacket(std::vector<uint8_t>&, bool rssiByte, int64_t timeReceived = 0);
    MAXPacket(std::string packet, int64_t timeReceived = 0);
    MAXPacket(uint8_t messageCounter, uint8_t messageType, uint8_t messageSubtype, int32_t senderAddress, int32_t destinationAddress, std::vector<uint8_t> payload, bool burst);
    virtual ~MAXPacket();

    void import(std::string& packet, bool removeFirstCharacter = true);
    void import(std::vector<uint8_t>& packet, bool rssiByte);

    uint8_t length() { return _length; }
    int32_t senderAddress() { return _senderAddress; }
    int32_t destinationAddress() { return _destinationAddress; }
    void setBurst(bool value) { _burst = value; }
    bool getBurst() { return _burst; }
    uint8_t messageCounter() { return _messageCounter; }
    void setMessageCounter(uint8_t counter) { _messageCounter = counter; }
    uint8_t messageType() { return _messageType; }
    void setMessageType(uint8_t type) { _messageType = type; }
    uint8_t messageSubtype() { return _messageSubtype; }
    uint8_t rssiDevice() { return _rssiDevice; }
    std::vector<uint8_t>& payload() { return _payload; }
    std::string hexString();
    std::vector<uint8_t> byteArray();
    std::vector<uint8_t> getPosition(double index, double size, int32_t mask);
    void setPosition(double index, double size, std::vector<uint8_t>& value);

    bool equals(std::shared_ptr<MAXPacket>& rhs);
protected:
    static const std::array<uint8_t, 9> _bitmask;

    uint8_t _length = 0;
    int32_t _senderAddress = 0;
    int32_t _destinationAddress = 0;
    bool _burst = false;
    uint8_t _messageCounter = 0;
    uint8_t _messageType = 0;
    uint8_t _messageSubtype = 0;
    uint8_t _rssiDevice = 0;
    std::vector<uint8_t> _payload;

    virtual uint8_t getByte(std::string);
    int32_t getInt(std::string);
};

}
#endif
