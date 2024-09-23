/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_MAX_HOMEGEARGATEWAY_H
#define HOMEGEAR_MAX_HOMEGEARGATEWAY_H

#include "../MAXPacket.h"
#include "IMaxInterface.h"
#include <homegear-base/BaseLib.h>

namespace MAX
{

class HomegearGateway : public IMaxInterface
{
public:
    HomegearGateway(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
    virtual ~HomegearGateway();

    virtual void startListening();
    virtual void stopListening();

    virtual bool isOpen() { return !_stopped; }

    virtual void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet);
protected:
    std::unique_ptr<C1Net::TcpSocket> _tcpSocket;
    std::unique_ptr<BaseLib::Rpc::BinaryRpc> _binaryRpc;
    std::unique_ptr<BaseLib::Rpc::RpcEncoder> _rpcEncoder;
    std::unique_ptr<BaseLib::Rpc::RpcDecoder> _rpcDecoder;

    std::thread _initThread;
    std::mutex _invokeMutex;
    std::mutex _requestMutex;
    std::atomic_bool _waitForResponse;
    std::condition_variable _requestConditionVariable;
    BaseLib::PVariable _rpcResponse;

    void listen();
    PVariable invoke(std::string methodName, PArray& parameters);
    void processPacket(std::string& data);
};

}

#endif //HOMEGEAR_ENOCEAN_HOMEGEARGATEWAY_H
