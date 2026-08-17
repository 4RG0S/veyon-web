/*
 * HeadlessVncServer.cpp - implementation of HeadlessVncServer class
 *
 * Copyright (c) 2020-2026 Tobias Junghans <tobydox@veyon.io>
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

extern "C" {
#include "rfb/rfb.h"
}

#include <array>
#include <cstdio>
#include <cstring>

#include <QImage>

#include "HeadlessVncServer.h"
#include "ScreenCapture.h"
#include "VeyonConfiguration.h"


struct HeadlessVncScreen
{
	~HeadlessVncScreen()
	{
		delete[] passwords[0];
	}

	rfbScreenInfoPtr rfbScreen{nullptr};
	std::array<char *, 2> passwords{};
	QImage framebuffer;
	QImage previousFrame;   // last frame sent, for damage tracking
	ScreenCapture capture;

};


HeadlessVncServer::HeadlessVncServer( QObject* parent ) :
	QObject( parent ),
	m_configuration( &VeyonCore::config() )
{
}



void HeadlessVncServer::prepareServer()
{
}



bool HeadlessVncServer::runServer( int serverPort, const Password& password )
{
	if (VeyonCore::isDebugging())
	{
		rfbLog = rfbLogDebug;
		rfbErr = rfbLogDebug;
	}
	else
	{
		rfbLog = rfbLogNone;
		rfbErr = rfbLogNone;
	}

	HeadlessVncScreen screen;

	if( initScreen( &screen ) == false ||
		initVncServer( serverPort, password, &screen ) == false )
	{
		return false;
	}

	while( true )
	{
		QThread::msleep( DefaultSleepTime );

		handleScreenChanges( &screen );

		rfbProcessEvents( screen.rfbScreen, 0 );
	}

	rfbShutdownServer( screen.rfbScreen, true );
	rfbScreenCleanup( screen.rfbScreen );

	return true;
}



bool HeadlessVncServer::initScreen( HeadlessVncScreen* screen )
{
	// size the framebuffer to the real screen so we can mirror it
	const auto screenSize = screen->capture.screenSize();
	const int width = screenSize.width() > 0 ? screenSize.width() : DefaultFramebufferWidth;
	const int height = screenSize.height() > 0 ? screenSize.height() : DefaultFramebufferHeight;

	screen->framebuffer = QImage( width, height, QImage::Format_RGB32 );
	screen->framebuffer.fill( m_configuration.backgroundColor() );

	return true;
}



bool HeadlessVncServer::handleScreenChanges( HeadlessVncScreen* screen )
{
	const int width = screen->framebuffer.width();
	const int height = screen->framebuffer.height();

	// grab the live desktop straight into the framebuffer memory
	if( screen->capture.grab( screen->framebuffer.bits(), width, height ) == false )
	{
		return false;
	}

	const auto& current = screen->framebuffer;
	auto& previous = screen->previousFrame;

	// first frame (or a resolution change): send the whole screen once
	if( previous.size() != current.size() )
	{
		previous = current.copy();
		rfbMarkRectAsModified( screen->rfbScreen, 0, 0, width, height );
		return true;
	}

	// damage tracking: only mark runs of rows that actually changed, so a
	// mostly-static desktop re-encodes just the moving parts (real-time feel)
	const size_t bytesPerLine = static_cast<size_t>( width ) * 4;
	int runStart = -1;
	bool anyChange = false;

	for( int y = 0; y < height; ++y )
	{
		const bool rowChanged =
			memcmp( current.constScanLine( y ), previous.constScanLine( y ), bytesPerLine ) != 0;

		if( rowChanged && runStart < 0 )
		{
			runStart = y;
		}
		else if( rowChanged == false && runStart >= 0 )
		{
			rfbMarkRectAsModified( screen->rfbScreen, 0, runStart, width, y );
			runStart = -1;
			anyChange = true;
		}
	}
	if( runStart >= 0 )
	{
		rfbMarkRectAsModified( screen->rfbScreen, 0, runStart, width, height );
		anyChange = true;
	}

	if( anyChange )
	{
		previous = current.copy();
	}

	return anyChange;
}



bool HeadlessVncServer::initVncServer( int serverPort, const VncServerPluginInterface::Password& password,
									  HeadlessVncScreen* screen )
{
	auto rfbScreen = rfbGetScreen( nullptr, nullptr,
								   screen->framebuffer.width(), screen->framebuffer.height(),
								   8, 3, 4 );

	if( rfbScreen == nullptr )
	{
		return false;
	}

	Q_UNUSED(password)

	rfbScreen->desktopName = "VeyonVNC";
	rfbScreen->frameBuffer = reinterpret_cast<char *>( screen->framebuffer.bits() );
	rfbScreen->port = serverPort;
	rfbScreen->ipv6port = serverPort;

	// only the local VncProxyConnection ever connects to this internal server, so
	// bind to loopback and offer the "None" security type. The bundled
	// libvncserver's VNC-auth (DES) is incompatible with veyon-core's client-side
	// implementation in this build, which made the proxy fail "password check".
	// The real authentication gate is the Veyon key/logon auth on the proxy port.
	rfbScreen->listenInterface = htonl( INADDR_LOOPBACK );

	rfbScreen->serverFormat.redShift = 16;
	rfbScreen->serverFormat.greenShift = 8;
	rfbScreen->serverFormat.blueShift = 0;

	rfbScreen->serverFormat.redMax = 255;
	rfbScreen->serverFormat.greenMax = 255;
	rfbScreen->serverFormat.blueMax = 255;

	rfbScreen->serverFormat.trueColour = true;
	rfbScreen->serverFormat.bitsPerPixel = 32;

	rfbScreen->alwaysShared = true;
	rfbScreen->handleEventsEagerly = true;
	rfbScreen->deferUpdateTime = 5;

	rfbScreen->screenData = screen;

	rfbScreen->cursor = nullptr;

	rfbInitServer( rfbScreen );

	rfbMarkRectAsModified( rfbScreen, 0, 0, rfbScreen->width, rfbScreen->height );

	screen->rfbScreen = rfbScreen;

	return true;
}



void HeadlessVncServer::rfbLogDebug(const char* format, ...)
{
	va_list args;
	va_start(args, format);

	static constexpr int MaxMessageLength = 256;
	std::array<char, MaxMessageLength> message;

	std::vsnprintf(message.data(), message.size(), format, args); // Flawfinder: ignore

	va_end(args);

	vDebug() << message.data();
}



void HeadlessVncServer::rfbLogNone(const char* format, ...)
{
	Q_UNUSED(format);
}


IMPLEMENT_CONFIG_PROXY(HeadlessVncConfiguration)
