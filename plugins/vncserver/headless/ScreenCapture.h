/*
 * ScreenCapture.h - GDI-based desktop screen capture (Windows)
 *
 * Copyright (c) 2026 CHECK NODE contributors
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

#pragma once

#include <QSize>

// GDI (BitBlt) based capture of the primary desktop. Win32 handles are kept as
// opaque void* so <windows.h> stays out of the rfb/winsock translation unit.
//
// This is the base implementation for the "monitoring" feature. Follow-up work:
//   - damage tracking (only mark changed rectangles) instead of full frame
//   - multi-monitor / virtual screen (SM_CXVIRTUALSCREEN)
//   - optional DXGI Desktop Duplication backend for lower CPU usage
class ScreenCapture
{
public:
	ScreenCapture();
	~ScreenCapture();

	ScreenCapture( const ScreenCapture& ) = delete;
	ScreenCapture& operator=( const ScreenCapture& ) = delete;

	// Current primary screen size in pixels.
	QSize screenSize() const;

	// Capture the primary screen into dst as 32-bit BGRX (matching
	// QImage::Format_RGB32). dst must hold width*height*4 bytes.
	// Returns false if capture failed.
	bool grab( unsigned char* dst, int width, int height );

private:
	void ensureResources( int width, int height );
	void releaseResources();

	void* m_screenDc = nullptr;   // HDC
	void* m_memDc = nullptr;      // HDC
	void* m_bitmap = nullptr;     // HBITMAP
	int m_width = 0;
	int m_height = 0;

};
