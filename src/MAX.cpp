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

#include "MAX.h"
#include "Interfaces.h"
#include "MAXCentral.h"
#include "GD.h"

namespace MAX
{

MAX::MAX(BaseLib::SharedObjects* bl, BaseLib::Systems::IFamilyEventSink* eventHandler) : BaseLib::Systems::DeviceFamily(bl, eventHandler, MAX_FAMILY_ID, MAX_FAMILY_NAME)
{
	if(!bl || !eventHandler)
	{
		std::cerr << "Critical: bl or eventHandler are nullptr in MAX module contstructor." << std::endl;
		exit(1);
	}
	GD::bl = _bl;
	GD::family = this;
    GD::settings = _settings;
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
	int32_t addressFromSettings = 0;
	std::string addressHex = GD::settings->getString("centraladdress");
	if(!addressHex.empty()) addressFromSettings = BaseLib::Math::getNumber(addressHex);
	if(addressFromSettings != 0)
	{
		std::shared_ptr<MAXCentral> central(new MAXCentral(deviceId, serialNumber, addressFromSettings, this));
		if(address != addressFromSettings) central->save(true);
		GD::out.printInfo("Info: Central address set to 0x" + BaseLib::HelperFunctions::getHexString(addressFromSettings, 6) + ".");
		return central;
	}
	if(address == 0)
	{
		address = (0xfd << 16) + BaseLib::HelperFunctions::getRandomNumber(0, 0xFFFF);
		std::shared_ptr<MAXCentral> central(new MAXCentral(deviceId, serialNumber, address, this));
		central->save(true);
		GD::out.printInfo("Info: Central address set to 0x" + BaseLib::HelperFunctions::getHexString(address, 6) + ".");
		return central;
	}
	GD::out.printInfo("Info: Central address set to 0x" + BaseLib::HelperFunctions::getHexString(address, 6) + ".");
	return std::shared_ptr<MAXCentral>(new MAXCentral(deviceId, serialNumber, address, this));
}

PVariable MAX::getPairingInfo()
{
	try
	{
		if(!_central) return std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		PVariable info = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		//{{{ General
		info->structValue->emplace("searchInterfaces", std::make_shared<BaseLib::Variable>(false));
		//}}}

		//{{{ Family settings
		PVariable familySettings = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		info->structValue->emplace("familySettings", familySettings);
		//}}}

		//{{{ Pairing methods
		PVariable pairingMethods = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		//{{{ setInstallMode
		PVariable setInstallModeMetadata = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		PVariable setInstallModeMetadataInfo = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		setInstallModeMetadataInfo->structValue->emplace("interfaceSelector", std::make_shared<BaseLib::Variable>(false));
		setInstallModeMetadata->structValue->emplace("metadataInfo", setInstallModeMetadataInfo);

		pairingMethods->structValue->emplace("setInstallMode", setInstallModeMetadata);
		//}}}

		info->structValue->emplace("pairingMethods", pairingMethods);
		//}}}

		//{{{ interfaces
		PVariable interfaces = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

		//{{{ Gateway
		auto interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("Homegear Gateway")));
		interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(true));

		auto field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.pairingInfo.id")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("id", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.pairingInfo.hostname")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("host", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.pairingInfo.password")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("password", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("2017")));
		interface->structValue->emplace("port", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("/etc/homegear/ca/cacert.pem")));
		interface->structValue->emplace("caFile", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("/etc/homegear/ca/certs/gateway-client.crt")));
		interface->structValue->emplace("certFile", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("/etc/homegear/ca/private/gateway-client.key")));
		interface->structValue->emplace("keyFile", field);

		interfaces->structValue->emplace("homegeargateway", interface);
		//}}}

		//{{{ CUL
		interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("CUL")));
		interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(false));

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.id")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("id", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.device")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("device", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.default")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("boolean")));
		interface->structValue->emplace("default", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(40));
		interface->structValue->emplace("responseDelay", field);

		interfaces->structValue->emplace("cul", interface);
		//}}}

		//{{{ COC, SCC, CSM, CCD
		interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("COC, SCC, CSM, CCD")));
		interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(false));

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.id")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("id", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.device")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("device", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.gpio1")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(17));
		interface->structValue->emplace("gpio1", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(3));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.gpio2")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(18));
		interface->structValue->emplace("gpio2", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(4));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.stackposition")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(0));
		interface->structValue->emplace("stackPosition", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(5));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.default")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("boolean")));
		interface->structValue->emplace("default", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(40));
		interface->structValue->emplace("responseDelay", field);

		interfaces->structValue->emplace("coc", interface);
		//}}}

		//{{{ TI CC1101
		interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("TI CC1101")));
		interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(false));

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.id")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("id", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.device")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(std::string("/dev/spidev0.0")));
		interface->structValue->emplace("device", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.interruptpin")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(2));
		interface->structValue->emplace("interruptPin", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(3));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.gpio1")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(24));
		interface->structValue->emplace("gpio1", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(4));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.gpio2")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("default", std::make_shared<BaseLib::Variable>(5));
		interface->structValue->emplace("gpio2", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(5));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.default")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("boolean")));
		interface->structValue->emplace("default", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(45));
		interface->structValue->emplace("responseDelay", field);

		interfaces->structValue->emplace("cc1100", interface);
		//}}}

		//{{{ CUNX
		interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("CUNX")));
		interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(true));

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.id")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("id", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.hostname")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		interface->structValue->emplace("host", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
		field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.default")));
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("boolean")));
		interface->structValue->emplace("default", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("2323")));
		interface->structValue->emplace("port", field);

		field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("integer")));
		field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(93));
		interface->structValue->emplace("responseDelay", field);

		interfaces->structValue->emplace("cunx", interface);
		//}}}

		info->structValue->emplace("interfaces", interfaces);
		//}}}

		return info;
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
