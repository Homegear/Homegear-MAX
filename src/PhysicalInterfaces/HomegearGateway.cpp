/* Copyright 2013-2019 Homegear GmbH */

#include "../GD.h"
#include "HomegearGateway.h"

namespace MAX
{

HomegearGateway::HomegearGateway(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IMaxInterface(settings)
{
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

HomegearGateway::~HomegearGateway()
{
    stopListening();
    _bl->threadManager.join(_initThread);
}

void HomegearGateway::startListening()
{
    try
    {
        stopListening();

        if(_settings->host.empty() || _settings->port.empty() || _settings->caFile.empty() || _settings->certFile.empty() || _settings->keyFile.empty())
        {
            _out.printError("Error: Configuration of Homegear Gateway is incomplete. Please correct it in \"max.conf\".");
            return;
        }

        _tcpSocket.reset(new BaseLib::TcpSocket(_bl, _settings->host, _settings->port, true, _settings->caFile, true, _settings->certFile, _settings->keyFile));
        _tcpSocket->setConnectionRetries(1);
        _tcpSocket->setReadTimeout(5000000);
        _tcpSocket->setWriteTimeout(5000000);
        if(_settings->useIdForHostnameVerification) _tcpSocket->setVerificationHostname(_settings->id);
        _stopCallbackThread = false;
        if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &HomegearGateway::listen, this);
        else _bl->threadManager.start(_listenThread, true, &HomegearGateway::listen, this);
        IPhysicalInterface::startListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void HomegearGateway::stopListening()
{
    try
    {
        _stopCallbackThread = true;
        if(_tcpSocket) _tcpSocket->close();
        _bl->threadManager.join(_listenThread);
        _stopped = true;
        _tcpSocket.reset();
        IPhysicalInterface::stopListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void HomegearGateway::listen()
{
    try
    {
        try
        {
            _tcpSocket->open();
            if(_tcpSocket->connected())
            {
                _out.printInfo("Info: Successfully connected.");
                _stopped = false;
            }
        }
        catch(const std::exception& ex)
        {
            _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
        }

        std::vector<char> buffer(1024);
        int32_t processedBytes = 0;
        while(!_stopCallbackThread)
        {
            try
            {
                if(_stopped || !_tcpSocket->connected())
                {
                    if(_stopCallbackThread) return;
                    if(_stopped) _out.printWarning("Warning: Connection to device closed. Trying to reconnect...");
                    _tcpSocket->close();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    _tcpSocket->open();
                    if(_tcpSocket->connected())
                    {
                        _out.printInfo("Info: Successfully connected.");
                        _stopped = false;
                    }
                    continue;
                }

                int32_t bytesRead = 0;
                try
                {
                    bytesRead = _tcpSocket->proofread(buffer.data(), buffer.size());
                }
                catch(BaseLib::SocketTimeOutException& ex)
                {
                    continue;
                }
                if(bytesRead <= 0) continue;
                if(bytesRead > 1024) bytesRead = 1024;

                if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: TCP packet received: " + BaseLib::HelperFunctions::getHexString(buffer.data(), bytesRead));

                processedBytes = 0;
                while(processedBytes < bytesRead)
                {
                    try
                    {
                        processedBytes += _binaryRpc->process(buffer.data() + processedBytes, bytesRead - processedBytes);
                        if(_binaryRpc->isFinished())
                        {
                            if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request)
                            {
                                std::string method;
                                BaseLib::PArray parameters = _rpcDecoder->decodeRequest(_binaryRpc->getData(), method);

                                if(method == "packetReceived" && parameters && parameters->size() == 2 && parameters->at(0)->integerValue64 == MAX_FAMILY_ID && !parameters->at(1)->stringValue.empty())
                                {
                                    processPacket(parameters->at(1)->stringValue);
                                }

                                BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();
                                std::vector<char> data;
                                _rpcEncoder->encodeResponse(response, data);
                                _tcpSocket->proofwrite(data);
                            }
                            else if(_binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::response && _waitForResponse)
                            {
                                std::unique_lock<std::mutex> requestLock(_requestMutex);
                                _rpcResponse = _rpcDecoder->decodeResponse(_binaryRpc->getData());
                                requestLock.unlock();
                                _requestConditionVariable.notify_all();
                            }
                            _binaryRpc->reset();
                        }
                    }
                    catch(BaseLib::Rpc::BinaryRpcException& ex)
                    {
                        _binaryRpc->reset();
                        _out.printError("Error processing packet: " + std::string(ex.what()));
                    }
                }
            }
            catch(const std::exception& ex)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void HomegearGateway::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
    try
    {
        std::shared_ptr<MAXPacket> maxPacket(std::dynamic_pointer_cast<MAXPacket>(packet));
        if(!maxPacket || !_tcpSocket) return;

        if(_stopped || !_tcpSocket->connected())
        {
            _out.printInfo("Info: Waiting two seconds, because wre are not connected.");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            if(_stopped || !_tcpSocket->connected())
            {
                _out.printWarning("Warning: !!!Not!!! sending packet " + BaseLib::HelperFunctions::getHexString(maxPacket->byteArray()) + ", because init is not complete.");
                return;
            }
        }

        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->reserve(3);
        parameters->push_back(std::make_shared<BaseLib::Variable>(MAX_FAMILY_ID));
        parameters->push_back(std::make_shared<BaseLib::Variable>(maxPacket->hexString()));
        parameters->push_back(std::make_shared<BaseLib::Variable>(maxPacket->getBurst()));

        if(_bl->debugLevel >= 4) _out.printInfo("Info: Sending: " + parameters->back()->stringValue);

        auto result = invoke("sendPacket", parameters);
        if(result->errorStruct)
        {
            _out.printError("Error sending packet " + maxPacket->hexString() + ": " + result->structValue->at("faultString")->stringValue);
        }

        _lastPacketSent = BaseLib::HelperFunctions::getTime();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

PVariable HomegearGateway::invoke(std::string methodName, PArray& parameters)
{
    try
    {
        std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

        std::unique_lock<std::mutex> requestLock(_requestMutex);
        _rpcResponse.reset();
        _waitForResponse = true;

        std::vector<char> encodedPacket;
        _rpcEncoder->encodeRequest(methodName, parameters, encodedPacket);

        int32_t i = 0;
        for(i = 0; i < 5; i++)
        {
            try
            {
                _tcpSocket->proofwrite(encodedPacket);
                break;
            }
            catch(BaseLib::SocketOperationException& ex)
            {
                _out.printError("Error: " + std::string(ex.what()));
                if(i == 5) return BaseLib::Variable::createError(-32500, ex.what());
                _tcpSocket->open();
            }
        }

        i = 0;
        while(!_requestConditionVariable.wait_for(requestLock, std::chrono::milliseconds(1000), [&]
        {
            i++;
            return _rpcResponse || _stopped || i == 10;
        }));
        _waitForResponse = false;
        if(i == 10 || !_rpcResponse) return BaseLib::Variable::createError(-32500, "No RPC response received.");

        return _rpcResponse;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return BaseLib::Variable::createError(-32500, "Unknown application error. See log for more details.");
}

void HomegearGateway::processPacket(std::string& data)
{
    try
    {
        if(data.size() < 9)
        {
            _out.printError("Error: Too small packet received: " + BaseLib::HelperFunctions::getHexString(data));
            return;
        }

        auto packetBytes = _bl->hf.getUBinary(data);
        std::shared_ptr<MAXPacket> packet = std::make_shared<MAXPacket>(packetBytes, true, BaseLib::HelperFunctions::getTime());
        raisePacketReceived(packet);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

}