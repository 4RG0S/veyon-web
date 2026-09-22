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

if(WITH_TESTS)
	add_executable(AccessBlockProtocolTest
		AccessBlockProtocolTest.cpp
		AccessBlockProtocol.cpp
		AccessBlockProtocol.h)
	set_default_target_properties(AccessBlockProtocolTest)
	target_link_libraries(AccessBlockProtocolTest PRIVATE
		veyon-core
		Qt${QT_MAJOR_VERSION}::Test)
	add_test(NAME AccessBlockProtocolTest COMMAND AccessBlockProtocolTest)
endif()
