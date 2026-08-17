include(BuildVeyonPlugin)

build_veyon_plugin(accessblock
	NAME AccessBlock
	SOURCES
	AccessBlockFeaturePlugin.cpp
	AccessBlockFeaturePlugin.h
	AccessBlockConfiguration.h
	AccessBlockConfigurationPage.cpp
	AccessBlockConfigurationPage.h
	UrlBlocker.cpp
	UrlBlocker.h
	ProcessBlocker.cpp
	ProcessBlocker.h)

if(WIN32)
	target_link_libraries(accessblock PRIVATE -ladvapi32)
endif()
