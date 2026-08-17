/*
 * UrlBlocker.cpp - blocks access to URLs on the local (slave) machine
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

#include "UrlBlocker.h"
#include "VeyonCore.h"

#ifdef Q_OS_WIN
#include <windows.h>

#include <string>
#endif


namespace
{

#ifdef Q_OS_WIN
constexpr wchar_t ChromePolicyKey[] = L"SOFTWARE\\Policies\\Google\\Chrome\\URLBlocklist";
constexpr wchar_t EdgePolicyKey[] = L"SOFTWARE\\Policies\\Microsoft\\Edge\\URLBlocklist";


bool removePolicyKey( const wchar_t* policyKey )
{
	const auto result = RegDeleteTreeW( HKEY_LOCAL_MACHINE, policyKey );
	return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}


bool writePolicyKey( const wchar_t* policyKey, const QStringList& urls )
{
	if( removePolicyKey( policyKey ) == false )
	{
		return false;
	}

	HKEY key = nullptr;
	DWORD disposition = 0;
	const auto createResult = RegCreateKeyExW( HKEY_LOCAL_MACHINE,
											 policyKey,
											 0,
											 nullptr,
											 REG_OPTION_NON_VOLATILE,
											 KEY_SET_VALUE,
											 nullptr,
											 &key,
											 &disposition );
	Q_UNUSED( disposition )

	if( createResult != ERROR_SUCCESS )
	{
		return false;
	}

	bool success = true;
	for( qsizetype index = 0; index < urls.size(); ++index )
	{
		const auto valueName = QString::number( index + 1 ).toStdWString();
		const auto value = urls.at( index ).toStdWString();
		const auto valueSize = static_cast<DWORD>( ( value.size() + 1 ) * sizeof( wchar_t ) );

		if( RegSetValueExW( key,
							valueName.c_str(),
							0,
							REG_SZ,
							reinterpret_cast<const BYTE*>( value.c_str() ),
							valueSize ) != ERROR_SUCCESS )
		{
			success = false;
			break;
		}
	}

	RegCloseKey( key );

	if( success == false )
	{
		removePolicyKey( policyKey );
	}

	return success;
}
#endif

}


void UrlBlocker::apply( const QStringList& urls )
{
	QStringList normalizedUrls;
	for( const auto& url : urls )
	{
		const auto normalizedUrl = url.trimmed();
		if( normalizedUrl.isEmpty() == false &&
			normalizedUrls.contains( normalizedUrl, Qt::CaseInsensitive ) == false )
		{
			normalizedUrls.append( normalizedUrl );
		}
	}

	if( normalizedUrls.isEmpty() )
	{
		clear();
		return;
	}

#ifdef Q_OS_WIN
	const bool chromeSuccess = writePolicyKey( ChromePolicyKey, normalizedUrls );
	const bool edgeSuccess = writePolicyKey( EdgePolicyKey, normalizedUrls );

	if( chromeSuccess && edgeSuccess )
	{
		m_blockedUrls = normalizedUrls;
		vCritical() << "UrlBlocker: applied browser policies:" << normalizedUrls;
	}
	else
	{
		removePolicyKey( ChromePolicyKey );
		removePolicyKey( EdgePolicyKey );
		m_blockedUrls.clear();
		vCritical() << "UrlBlocker: failed to write browser policies; administrator privileges are required";
	}
#else
	Q_UNUSED( normalizedUrls )
	vCritical() << "UrlBlocker: URL blocking is only supported on Windows";
#endif
}



void UrlBlocker::clear()
{
#ifdef Q_OS_WIN
	const bool chromeSuccess = removePolicyKey( ChromePolicyKey );
	const bool edgeSuccess = removePolicyKey( EdgePolicyKey );

	if( chromeSuccess && edgeSuccess )
	{
		m_blockedUrls.clear();
		vCritical() << "UrlBlocker: cleared browser policies";
	}
	else
	{
		vCritical() << "UrlBlocker: failed to clear browser policies; administrator privileges are required";
	}
#else
	m_blockedUrls.clear();
#endif
}
