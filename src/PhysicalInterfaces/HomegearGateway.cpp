/* Copyright 2013-2019 Homegear GmbH */

#include "../GD.h"
#include "HomegearGateway.h"

namespace MAX {

HomegearGateway::HomegearGateway(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IMaxInterface(settings) {
  _settings = settings;
  _out.init(GD::bl);
  _out.setPrefix(GD::out.getPrefix() + "MAX! Homegear Gateway \"" + settings->id + "\": ");

  signal(SIGPIPE, SIG_IGN);

  _stopped = true;
  _waitForResponse = false;

  _binaryRpc.reset(new BaseLib::Rpc::BinaryRpc(_bl));
  _rpcEncoder.reset(new BaseLib::Rpc::RpcEncoder(_bl, true, true));
  _rpcDecoder.reset(new BaseLib::Rpc::RpcDecoder(_bl, false, false));
}

HomegearGateway::~HomegearGateway() {
  stopListening();
  _bl->threadManager.join(_initThread);
}

void HomegearGateway::startListening() {
  try {
    stopListening();

    if (_settings->host.empty() || _settings->port.empty() || _settings->caFile.empty() || _settings->certFile.empty() || _settings->keyFile.empty()) {
      _out.printError("Error: Configuration of Homegear Gateway is incomplete. Please correct it in \"max.conf\".");
      return;
    }

    C1Net::TcpSocketInfo tcp_socket_info;
    tcp_socket_info.read_timeout = 5000;
    tcp_socket_info.write_timeout = 5000;

    C1Net::TcpSocketHostInfo tcp_socket_host_info{
        .host = _settings->host,
        .port = (uint16_t)BaseLib::Math::getUnsignedNumber(_settings->port),
        .tls = _settings->ssl,
        .verify_certificate = _settings->verifyCertificate,
        .ca_file = _settings->caFile,
        .connection_retries = 1
    };

    if (_settings->useIdForHostnameVerification) {
      tcp_socket_host_info.verify_custom_hostname = true;
      tcp_socket_host_info.custom_hostname = _settings->id;
    }

    _tcpSocket = std::make_unique<C1Net::TcpSocket>(tcp_socket_info, tcp_socket_host_info);

    _stopCallbackThread = false;
    if (_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &HomegearGateway::listen, this);
    else _bl->threadManager.start(_listenThread, true, &HomegearGateway::listen, this);
    IPhysicalInterface::startListening();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void HomegearGateway::stopListening() {
  try {
    _stopCallbackThread = true;
    if (_tcpSocket) _tcpSocket->Shutdown();
    _bl->threadManager.join(_listenThread);
    _stopped = true;
    _tcpSocket.reset();
    IPhysicalInterface::stopListening();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void HomegearGateway::listen() {
  try {
    try {
      _tcpSocket->Open();
      if (_tcpSocket->Connected()) {
        _out.printInfo("Info: Successfully connected.");
        _stopped = false;
      }
    }
    catch (const std::exception &ex) {
      _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }

    std::vector<char> buffer(1024);
    int32_t processedBytes = 0;
    bool more_data = false;
    while (!_stopCallbackThread) {
      try {
        if (_stopped || !_tcpSocket->Connected()) {
          if (_stopCallbackThread) return;
          if (_stopped) _out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
          _tcpSocket->Shutdown();
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          _tcpSocket->Open();
          if (_tcpSocket->Connected()) {
            _out.printInfo("Info: Successfully connected.");
            _stopped = false;
          }
          continue;
        }

        int32_t bytesRead = 0;
        try {
          bytesRead = _tcpSocket->Read((uint8_t *)buffer.data(), buffer.size(), more_data);
        }
        catch (const C1Net::TimeoutException &ex) {
          continue;
        }
        if (bytesRead <= 0) continue;
        if (bytesRead > 1024) bytesRead = 1024;

        if (GD::bl->debugLevel >= 5) _out.printDebug("Debug: TCP packet received: " + BaseLib::HelperFunctions::getHexString(buffer.data(), bytesRead));

        processedBytes = 0;
        while (processedBytes < bytesRead) {
          try {
            processedBytes += _binaryRpc->process(buffer.data() + processedBytes, bytesRead - processedBytes);
            if (_binaryRpc->isFinished()) {
              if (_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request) {
                std::string method;
                BaseLib::PArray parameters = _rpcDecoder->decodeRequest(_binaryRpc->getData(), method);

                if (method == "packetReceived" && parameters && parameters->size() == 2 && parameters->at(0)->integerValue64 == MAX_FAMILY_ID && !parameters->at(1)->stringValue.empty()) {
                  processPacket(parameters->at(1)->stringValue);
                }

                BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();
                std::vector<uint8_t> data;
                _rpcEncoder->encodeResponse(response, data);
                _tcpSocket->Send(data);
              } else if (_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::response && _waitForResponse) {
                std::unique_lock<std::mutex> requestLock(_requestMutex);
                _rpcResponse = _rpcDecoder->decodeResponse(_binaryRpc->getData());
                requestLock.unlock();
                _requestConditionVariable.notify_all();
              }
              _binaryRpc->reset();
            }
          }
          catch (const BaseLib::Rpc::BinaryRpcException &ex) {
            _binaryRpc->reset();
            _out.printError("Error processing packet: " + std::string(ex.what()));
          }
        }
      }
      catch (const std::exception &ex) {
        _stopped = true;
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
      }
    }
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

void HomegearGateway::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet) {
  try {
    std::shared_ptr<MAXPacket> maxPacket(std::dynamic_pointer_cast<MAXPacket>(packet));
    if (!maxPacket || !_tcpSocket) return;

    if (_stopped || !_tcpSocket->Connected()) {
      _out.printInfo("Info: Waiting two seconds, because wre are not connected.");
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      if (_stopped || !_tcpSocket->Connected()) {
        _out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(maxPacket->byteArray()) + ", because init is not complete.");
        return;
      }
    }

    BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
    parameters->reserve(3);
    parameters->push_back(std::make_shared<BaseLib::Variable>(MAX_FAMILY_ID));
    parameters->push_back(std::make_shared<BaseLib::Variable>(maxPacket->hexString()));
    parameters->push_back(std::make_shared<BaseLib::Variable>(maxPacket->getBurst()));

    if (_bl->debugLevel >= 4) _out.printInfo("Info: Sending: " + parameters->at(1)->stringValue);

    auto result = invoke("sendPacket", parameters);
    if (result->errorStruct) {
      _out.printError("Error sending packet " + maxPacket->hexString() + ": " + result->structValue->at("faultString")->stringValue);
    }

    _lastPacketSent = BaseLib::HelperFunctions::getTime();
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

PVariable HomegearGateway::invoke(std::string methodName, PArray &parameters) {
  try {
    std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

    std::unique_lock<std::mutex> requestLock(_requestMutex);
    _rpcResponse.reset();
    _waitForResponse = true;

    std::vector<uint8_t> encodedPacket;
    _rpcEncoder->encodeRequest(methodName, parameters, encodedPacket);

    int32_t i = 0;
    for (i = 0; i < 5; i++) {
      try {
        _tcpSocket->Send(encodedPacket);
        break;
      }
      catch (const C1Net::Exception &ex) {
        _out.printError("Error: " + std::string(ex.what()));
        if (i == 5) return BaseLib::Variable::createError(-32500, ex.what());
        _tcpSocket->Open();
      }
    }

    i = 0;
    while (!_requestConditionVariable.wait_for(requestLock, std::chrono::milliseconds(1000), [&] {
      i++;
      return _rpcResponse || _stopped || i == 10;
    }));
    _waitForResponse = false;
    if (i == 10 || !_rpcResponse) return BaseLib::Variable::createError(-32500, "No RPC response received.");

    return _rpcResponse;
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
  return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void HomegearGateway::processPacket(std::string &data) {
  try {
    if (data.size() < 9) {
      _out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
      return;
    }

    auto packetBytes = _bl->hf.getUBinary(data);
    std::shared_ptr<MAXPacket> packet = std::make_shared<MAXPacket>(packetBytes, true, BaseLib::HelperFunctions::getTime());
    raisePacketReceived(packet);
  }
  catch (const std::exception &ex) {
    _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
  }
}

}
