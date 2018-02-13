cmake_minimum_required(VERSION 3.8)
project(homegear_max)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        src/PhysicalInterfaces/COC.cpp
        src/PhysicalInterfaces/COC.h
        src/PhysicalInterfaces/CUL.cpp
        src/PhysicalInterfaces/CUL.h
        src/PhysicalInterfaces/Cunx.cpp
        src/PhysicalInterfaces/Cunx.h
        src/PhysicalInterfaces/TICC1100.cpp
        src/PhysicalInterfaces/TICC1100.h
        src/delegate.hpp
        src/delegate_list.hpp
        src/delegate_template.hpp
        src/Factory.cpp
        src/Factory.h
        src/GD.cpp
        src/GD.h
        src/Interfaces.cpp
        src/Interfaces.h
        src/MAX.cpp
        src/MAX.h
        src/MAXCentral.cpp
        src/MAXCentral.h
        src/MAXDeviceTypes.h
        src/MAXMessage.cpp
        src/MAXMessage.h
        src/MAXMessages.cpp
        src/MAXMessages.h
        src/MAXPacket.cpp
        src/MAXPacket.h
        src/MAXPeer.cpp
        src/MAXPeer.h
        src/PacketManager.cpp
        src/PacketManager.h
        src/PacketQueue.cpp
        src/PacketQueue.h
        src/PendingQueues.cpp
        src/PendingQueues.h
        src/QueueManager.cpp
        src/QueueManager.h
        config.h src/PhysicalInterfaces/IMaxInterface.cpp src/PhysicalInterfaces/IMaxInterface.h)

add_custom_target(homegear-gateway COMMAND ../makeDebug.sh SOURCES ${SOURCE_FILES})

add_library(homegear_max ${SOURCE_FILES})