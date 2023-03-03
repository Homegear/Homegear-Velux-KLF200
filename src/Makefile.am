AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++17 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_velux_klf200.la
mod_velux_klf200_la_SOURCES = Velux.cpp Factory.cpp VeluxPacket.cpp GD.cpp VeluxPeer.cpp PhysicalInterfaces/Klf200.cpp VeluxCentral.cpp Interfaces.cpp
mod_velux_klf200_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_velux_klf200.la
