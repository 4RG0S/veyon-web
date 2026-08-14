/*
 * ScreenCapture.cpp - GDI-based desktop screen capture (Windows)
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

#include <windows.h>

#include "ScreenCapture.h"


ScreenCapture::ScreenCapture()
{
	m_screenDc = GetDC( nullptr );
}



ScreenCapture::~ScreenCapture()
{
	releaseResources();

	if( m_screenDc )
	{
		ReleaseDC( nullptr, static_cast<HDC>( m_screenDc ) );
		m_screenDc = nullptr;
	}
}



QSize ScreenCapture::screenSize() const
{
	return QSize( GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) );
}



void ScreenCapture::ensureResources( int width, int height )
{
	if( m_memDc != nullptr && m_bitmap != nullptr && m_width == width && m_height == height )
	{
		return;
	}

	releaseResources();

	auto screenDc = static_cast<HDC>( m_screenDc );
	m_memDc = CreateCompatibleDC( screenDc );
	m_bitmap = CreateCompatibleBitmap( screenDc, width, height );
	SelectObject( static_cast<HDC>( m_memDc ), static_cast<HBITMAP>( m_bitmap ) );

	m_width = width;
	m_height = height;
}



void ScreenCapture::releaseResources()
{
	if( m_bitmap )
	{
		DeleteObject( static_cast<HBITMAP>( m_bitmap ) );
		m_bitmap = nullptr;
	}
	if( m_memDc )
	{
		DeleteDC( static_cast<HDC>( m_memDc ) );
		m_memDc = nullptr;
	}
	m_width = 0;
	m_height = 0;
}



bool ScreenCapture::grab( unsigned char* dst, int width, int height )
{
	if( m_screenDc == nullptr || dst == nullptr || width <= 0 || height <= 0 )
	{
		return false;
	}

	ensureResources( width, height );

	auto screenDc = static_cast<HDC>( m_screenDc );
	auto memDc = static_cast<HDC>( m_memDc );

	// copy the live desktop into our compatible bitmap
	if( BitBlt( memDc, 0, 0, width, height, screenDc, 0, 0, SRCCOPY ) == FALSE )
	{
		return false;
	}

	// extract as a top-down 32-bit BGRX image straight into the framebuffer
	BITMAPINFO bmi;
	ZeroMemory( &bmi, sizeof( bmi ) );
	bmi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height; // negative = top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	return GetDIBits( memDc, static_cast<HBITMAP>( m_bitmap ), 0,
					  static_cast<UINT>( height ), dst, &bmi, DIB_RGB_COLORS ) != 0;
}
