include(BuildVeyonPlugin)

build_veyon_plugin(accessblock
	NAME AccessBlock
	SOURCES
	AccessBlockFeaturePlugin.cpp
	AccessBlockFeaturePlugin.h
	AccessBlockConfiguration.h
	UrlBlocker.cpp
	UrlBlocker.h
	ProcessBlocker.cpp
	ProcessBlocker.h)
