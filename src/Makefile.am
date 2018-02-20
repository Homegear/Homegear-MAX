AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++11 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_max.la
mod_max_la_SOURCES = Makefile.am MAXMessages.cpp MAXPacket.cpp PendingQueues.cpp Factory.cpp GD.h MAXPeer.h MAXMessage.cpp MAXPeer.cpp PacketQueue.cpp QueueManager.h delegate.hpp GD.cpp MAX.cpp delegate_template.hpp Factory.h MAXPacket.h MAXMessage.h delegate_list.hpp PhysicalInterfaces/CUL.h PhysicalInterfaces/CUL.cpp PhysicalInterfaces/Cunx.h PhysicalInterfaces/Cunx.cpp PhysicalInterfaces/IMaxInterface.cpp PhysicalInterfaces/TICC1100.h PhysicalInterfaces/COC.h PhysicalInterfaces/TICC1100.cpp PhysicalInterfaces/COC.cpp MAXCentral.cpp MAXCentral.h PacketQueue.h PendingQueues.h PacketManager.h PacketManager.cpp QueueManager.cpp MAXMessages.h MAX.h Interfaces.cpp Interfaces.h
mod_max_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_max.la
