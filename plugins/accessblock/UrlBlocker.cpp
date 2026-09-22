/*
 * UrlBlocker.cpp - blocks access to URLs on the local (slave) machine
 *
 * Copyright (c) 2026 CHECK NODE contributors
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "UrlBlocker.h"
#include "VeyonCore.h"

#include <QHostAddress>
#include <QHostInfo>

#include <algorithm>
#include <iterator>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>

#include <string>
#endif


namespace
{

#ifdef Q_OS_WIN
constexpr wchar_t ChromePolicyKey[] = L"SOFTWARE\\Policies\\Google\\Chrome\\URLBlocklist";
constexpr wchar_t EdgePolicyKey[] = L"SOFTWARE\\Policies\\Microsoft\\Edge\\URLBlocklist";

const GUID AccessBlockSublayer =
{ 0x9bf09f59, 0x6fe5, 0x4fef, { 0x9e, 0xb6, 0x37, 0xd5, 0x42, 0x64, 0xa1, 0xd2 } };


class ScopedRegistryKey
{
public:
	explicit ScopedRegistryKey( HKEY key ) :
		m_key( key )
	{
	}

	~ScopedRegistryKey()
	{
		if( m_key != nullptr )
		{
			RegCloseKey( m_key );
		}
	}

	ScopedRegistryKey( const ScopedRegistryKey& ) = delete;
	ScopedRegistryKey& operator=( const ScopedRegistryKey& ) = delete;

	HKEY get() const
	{
		return m_key;
	}

private:
	HKEY m_key;

};


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
	const auto createResult = RegCreateKeyExW( HKEY_LOCAL_MACHINE, policyKey, 0, nullptr,
											 REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, &disposition );
	Q_UNUSED( disposition )

	if( createResult != ERROR_SUCCESS )
	{
		return false;
	}

	bool success = true;
	{
		const ScopedRegistryKey scopedKey{key};
		for( qsizetype index = 0; index < urls.size(); ++index )
		{
			const auto valueName = QString::number( index + 1 ).toStdWString();
			const auto value = urls.at( index ).toStdWString();
			const auto valueSize = static_cast<DWORD>( ( value.size() + 1 ) * sizeof( wchar_t ) );

			if( RegSetValueExW( scopedKey.get(),
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
	}

	if( success == false )
	{
		removePolicyKey( policyKey );
	}

	return success;
}
#endif


bool isNetworkDomain( const QString& value )
{
	return value.contains( QStringLiteral( "://" ) ) == false &&
		value.contains( QLatin1Char( '/' ) ) == false;
}

}


UrlBlocker::UrlBlocker( QObject* parent ) :
	QObject( parent )
{
	m_refreshTimer.setInterval( 60000 );
	connect( &m_refreshTimer, &QTimer::timeout, this, &UrlBlocker::applyNetworkFilters );
}


UrlBlocker::~UrlBlocker()
{
	m_refreshTimer.stop();
	clearNetworkFilters();
}


void UrlBlocker::apply( const QStringList& urls )
{
	QStringList domains;
	QStringList browserUrls;
	for( const auto& url : urls )
	{
		const auto value = url.trimmed();
		if( value.isEmpty() )
		{
			continue;
		}

		auto& destination = isNetworkDomain( value ) ? domains : browserUrls;
		if( destination.contains( value, Qt::CaseInsensitive ) == false )
		{
			destination.append( value );
		}
	}

	if( domains.isEmpty() && browserUrls.isEmpty() )
	{
		clear();
		return;
	}

	m_blockedDomains = domains;
	m_blockedBrowserUrls = browserUrls;
	applyNetworkFilters();
	applyBrowserPolicies( browserUrls );

	if( domains.isEmpty() )
	{
		m_refreshTimer.stop();
	}
	else
	{
		m_refreshTimer.start();
	}
}


void UrlBlocker::applyBrowserPolicies( const QStringList& urls )
{
#ifdef Q_OS_WIN
	if( urls.isEmpty() )
	{
		clearBrowserPolicies();
		return;
	}

	const bool chromeSuccess = writePolicyKey( ChromePolicyKey, urls );
	const bool edgeSuccess = writePolicyKey( EdgePolicyKey, urls );
	if( chromeSuccess && edgeSuccess )
	{
		vCritical() << "UrlBlocker: applied browser URL policies:" << urls;
	}
	else
	{
		removePolicyKey( ChromePolicyKey );
		removePolicyKey( EdgePolicyKey );
		vCritical() << "UrlBlocker: failed to write browser policies; administrator privileges are required";
	}
#else
	Q_UNUSED( urls )
#endif
}


void UrlBlocker::clearBrowserPolicies()
{
#ifdef Q_OS_WIN
	const bool chromeSuccess = removePolicyKey( ChromePolicyKey );
	const bool edgeSuccess = removePolicyKey( EdgePolicyKey );
	if( chromeSuccess && edgeSuccess )
	{
		vCritical() << "UrlBlocker: cleared browser policies";
	}
	else
	{
		vCritical() << "UrlBlocker: failed to clear browser policies; administrator privileges are required";
	}
#endif
}


void UrlBlocker::applyNetworkFilters()
{
#ifdef Q_OS_WIN
	if( m_blockedDomains.isEmpty() )
	{
		clearNetworkFilters();
		return;
	}

	QVector<QPair<QString, QHostAddress>> resolvedAddresses;
	for( const auto& domain : std::as_const( m_blockedDomains ) )
	{
		const auto hostInfo = QHostInfo::fromName( domain );
		if( hostInfo.error() != QHostInfo::NoError )
		{
			vCritical() << "UrlBlocker: failed to resolve domain:" << domain << hostInfo.errorString();
			continue;
		}

		for( const auto& address : hostInfo.addresses() )
		{
			if( address.protocol() == QAbstractSocket::IPv4Protocol ||
				address.protocol() == QAbstractSocket::IPv6Protocol )
			{
				resolvedAddresses.append( { domain, address } );
			}
		}
	}

	if( resolvedAddresses.isEmpty() )
	{
		vCritical() << "UrlBlocker: keeping existing WFP filters because no domain resolved";
		return;
	}

	clearNetworkFilters();

	FWPM_SESSION0 session{};
	session.flags = FWPM_SESSION_FLAG_DYNAMIC;
	HANDLE engine = nullptr;
	const auto openResult = FwpmEngineOpen0( nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine );
	if( openResult != ERROR_SUCCESS )
	{
		vCritical() << "UrlBlocker: failed to open WFP engine:" << openResult;
		return;
	}
	m_filterEngine = engine;

	FWPM_SUBLAYER0 sublayer{};
	sublayer.subLayerKey = AccessBlockSublayer;
	sublayer.displayData.name = const_cast<wchar_t*>( L"CHECK NODE domain blocking" );
	sublayer.weight = 0x100;
	const auto sublayerResult = FwpmSubLayerAdd0( engine, &sublayer, nullptr );
	if( sublayerResult != ERROR_SUCCESS && sublayerResult != FWP_E_ALREADY_EXISTS )
	{
		vCritical() << "UrlBlocker: failed to create WFP sublayer:" << sublayerResult;
		clearNetworkFilters();
		return;
	}

	for( const auto& resolvedAddress : std::as_const( resolvedAddresses ) )
	{
		const auto& domain = resolvedAddress.first;
		const auto& address = resolvedAddress.second;
		FWPM_FILTER_CONDITION0 condition{};
		condition.fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
		condition.matchType = FWP_MATCH_EQUAL;

		FWP_BYTE_ARRAY16 ipv6Address{};
		if( address.protocol() == QAbstractSocket::IPv4Protocol )
		{
			condition.conditionValue.type = FWP_UINT32;
			condition.conditionValue.uint32 = address.toIPv4Address();
		}
		else if( address.protocol() == QAbstractSocket::IPv6Protocol )
		{
			const auto bytes = address.toIPv6Address();
			std::copy( bytes.cbegin(), bytes.cend(), std::begin( ipv6Address.byteArray16 ) );
			condition.conditionValue.type = FWP_BYTE_ARRAY16_TYPE;
			condition.conditionValue.byteArray16 = &ipv6Address;
		}
		else
		{
			continue;
		}

		FWPM_FILTER0 filter{};
		const auto filterName = QStringLiteral( "CHECK NODE block %1" ).arg( domain ).toStdWString();
		filter.displayData.name = const_cast<wchar_t*>( filterName.c_str() );
		filter.subLayerKey = AccessBlockSublayer;
		filter.layerKey = address.protocol() == QAbstractSocket::IPv4Protocol
				? FWPM_LAYER_ALE_AUTH_CONNECT_V4 : FWPM_LAYER_ALE_AUTH_CONNECT_V6;
		filter.action.type = FWP_ACTION_BLOCK;
		filter.weight.type = FWP_EMPTY;
		filter.numFilterConditions = 1;
		filter.filterCondition = &condition;

		UINT64 filterId = 0;
		const auto addResult = FwpmFilterAdd0( engine, &filter, nullptr, &filterId );
		if( addResult == ERROR_SUCCESS )
		{
			m_filterIds.append( filterId );
		}
		else
		{
			vCritical() << "UrlBlocker: failed to add WFP filter for" << domain << address << addResult;
		}
	}

	vCritical() << "UrlBlocker: applied" << m_filterIds.size()
			<< "WFP address filters for domains:" << m_blockedDomains;
#endif
}


void UrlBlocker::clearNetworkFilters()
{
#ifdef Q_OS_WIN
	const auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine != nullptr )
	{
		for( const auto filterId : std::as_const( m_filterIds ) )
		{
			FwpmFilterDeleteById0( engine, filterId );
		}
		FwpmSubLayerDeleteByKey0( engine, &AccessBlockSublayer );
		FwpmEngineClose0( engine );
	}
#endif
	m_filterIds.clear();
	m_filterEngine = nullptr;
}


void UrlBlocker::clear()
{
	m_refreshTimer.stop();
	clearNetworkFilters();
	clearBrowserPolicies();
	m_blockedDomains.clear();
	m_blockedBrowserUrls.clear();
}
