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

#include "MAX.h"
#include "Interfaces.h"
#include "MAXCentral.h"
#include "GD.h"

namespace MAX
{

MAX::MAX(BaseLib::SharedObjects* bl, BaseLib::Systems::DeviceFamily::IFamilyEventSink* eventHandler) : BaseLib::Systems::DeviceFamily(bl, eventHandler, MAX_FAMILY_ID, MAX_FAMILY_NAME)
{
	if(!bl || !eventHandler)
	{
		std::cerr << "Critical: bl or eventHandler are nullptr in MAX module contstructor." << std::endl;
		exit(1);
	}
	GD::bl = _bl;
	GD::family = this;
	GD::out.init(bl);
	GD::out.setPrefix("Module MAX: ");
	GD::out.printDebug("Debug: Loading module...");
	_physicalInterfaces.reset(new Interfaces(bl, _settings->getPhysicalInterfaceSettings()));
}

MAX::~MAX()
{

}

void MAX::dispose()
{
	if(_disposed) return;
	DeviceFamily::dispose();

	GD::physicalInterfaces.clear();
	GD::defaultPhysicalInterface.reset();
}

void MAX::createCentral()
{
	try
	{
		if(_central) return;

		int32_t address = (0xfd << 16) + BaseLib::HelperFunctions::getRandomNumber(0, 0xFFFF);
		int32_t seedNumber = BaseLib::HelperFunctions::getRandomNumber(1, 9999999);
		std::ostringstream stringstream;
		stringstream << "VBC" << std::setw(7) << std::setfill('0') << std::dec << seedNumber;
		std::string serialNumber(stringstream.str());

		_central.reset(new MAXCentral(0, serialNumber, address, this));
		GD::out.printMessage("Created MAX central with id " + std::to_string(_central->getId()) + ", address 0x" + BaseLib::HelperFunctions::getHexString(address, 6) + " and serial number " + serialNumber);
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

std::shared_ptr<BaseLib::Systems::ICentral> MAX::initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber)
{
	return std::shared_ptr<MAXCentral>(new MAXCentral(deviceId, serialNumber, address, this));
}

PVariable MAX::getPairingMethods()
{
	try
	{
		if(!_central) return PVariable(new Variable(VariableType::tArray));
		PVariable array(new Variable(VariableType::tArray));
		array->arrayValue->push_back(PVariable(new Variable(std::string("setInstallMode"))));
		return array;
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
}
