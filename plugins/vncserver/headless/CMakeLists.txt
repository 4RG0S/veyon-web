include(BuildVeyonPlugin)

build_veyon_plugin(headless-vnc-server
	NAME HeadlessVncServer
	SOURCES
	HeadlessVncServer.cpp
	HeadlessVncServer.h
	HeadlessVncConfiguration.h
	ScreenCapture.cpp
	ScreenCapture.h
)

if(WITH_BUNDLED_LIBVNC)
	target_link_libraries(headless-vnc-server PRIVATE vncserver)
else()
	target_link_libraries(headless-vnc-server PRIVATE LibVNC::LibVNCServer)
endif()

# GDI screen capture needs the Win32 GDI/user libraries
target_link_libraries(headless-vnc-server PRIVATE gdi32 user32)

target_compile_options(headless-vnc-server PRIVATE -Wno-parentheses)
