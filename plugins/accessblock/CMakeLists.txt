include(BuildVeyonPlugin)

build_veyon_plugin(accessblock
	NAME AccessBlock
	SOURCES
	AccessBlockFeaturePlugin.cpp
	AccessBlockFeaturePlugin.h
	AccessBlockProtocol.cpp
	AccessBlockProtocol.h
	AccessBlockConfiguration.h
	AccessBlockConfigurationPage.cpp
	AccessBlockConfigurationPage.h
	UrlBlocker.cpp
	UrlBlocker.h
	ProcessBlocker.cpp
	ProcessBlocker.h)

if(WIN32)
	target_link_libraries(accessblock PRIVATE -ladvapi32 -lfwpuclnt -lrpcrt4)
endif()

test_veyon_plugin(accessblock AccessBlockProtocolTest)
if(WITH_TESTS)
	target_link_libraries(AccessBlockProtocolTest PRIVATE Qt${QT_MAJOR_VERSION}::Test)
endif()
