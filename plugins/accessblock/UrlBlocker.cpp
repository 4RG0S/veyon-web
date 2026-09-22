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
#include <QCryptographicHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <vector>

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
constexpr wchar_t ChromeOwnershipKey[] = L"SOFTWARE\\Veyon\\AccessBlock\\BrowserOwnership\\Chrome";
constexpr wchar_t EdgeOwnershipKey[] = L"SOFTWARE\\Veyon\\AccessBlock\\BrowserOwnership\\Edge";
constexpr qsizetype MaximumBrowserPolicyEntries = 1000;

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


using RegistryValues = QMap<QString, QString>;


QByteArray policyValueHash( const QString& value )
{
	return QCryptographicHash::hash( value.toUtf8(), QCryptographicHash::Sha256 ).toHex();
}


bool openOrCreateRegistryKey( const wchar_t* path, REGSAM access, HKEY* key )
{
	DWORD disposition = 0;
	return RegCreateKeyExW( HKEY_LOCAL_MACHINE, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
						 access | KEY_WOW64_64KEY, nullptr, key, &disposition ) == ERROR_SUCCESS;
}


bool readRegistryValues( const wchar_t* path,
						 RegistryValues* values,
						 QSet<QString>* valueNames = nullptr )
{
	HKEY key = nullptr;
	const auto openResult = RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0,
										KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key );
	if( openResult == ERROR_FILE_NOT_FOUND )
	{
		values->clear();
		if( valueNames )
		{
			valueNames->clear();
		}
		return true;
	}
	if( openResult != ERROR_SUCCESS )
	{
		return false;
	}

	const ScopedRegistryKey scopedKey{key};
	DWORD valueCount = 0;
	DWORD maximumValueNameLength = 0;
	DWORD maximumValueDataLength = 0;
	if( RegQueryInfoKeyW( scopedKey.get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
						&valueCount, &maximumValueNameLength, &maximumValueDataLength,
						nullptr, nullptr ) != ERROR_SUCCESS )
	{
		return false;
	}

	values->clear();
	if( valueNames )
	{
		valueNames->clear();
	}
	std::vector<wchar_t> valueName( maximumValueNameLength + 2 );
	std::vector<wchar_t> valueData( maximumValueDataLength / sizeof( wchar_t ) + 2 );
	for( DWORD index = 0; index < valueCount; ++index )
	{
		DWORD valueNameLength = static_cast<DWORD>( valueName.size() );
		DWORD valueDataLength = static_cast<DWORD>( valueData.size() * sizeof( wchar_t ) );
		DWORD valueType = 0;
		if( RegEnumValueW( scopedKey.get(), index, valueName.data(), &valueNameLength, nullptr,
						   &valueType, reinterpret_cast<BYTE*>( valueData.data() ), &valueDataLength ) != ERROR_SUCCESS )
		{
			return false;
		}
		const auto name = QString::fromWCharArray( valueName.data(), valueNameLength );
		if( valueNames )
		{
			valueNames->insert( name );
		}
		if( valueType != REG_SZ || valueDataLength < sizeof( wchar_t ) )
		{
			continue;
		}

		const auto characterCount = valueDataLength / sizeof( wchar_t );
		const auto stringData = valueData.data();
		const auto valueLength = characterCount > 0 && stringData[characterCount - 1] == L'\0'
				? characterCount - 1 : characterCount;
		values->insert( name, QString::fromWCharArray( stringData, valueLength ) );
	}
	return true;
}


bool setRegistryString( const wchar_t* path, const QString& name, const QString& value )
{
	HKEY key = nullptr;
	if( openOrCreateRegistryKey( path, KEY_QUERY_VALUE | KEY_SET_VALUE, &key ) == false )
	{
		return false;
	}
	const ScopedRegistryKey scopedKey{key};
	const auto nameData = name.toStdWString();
	const auto valueData = value.toStdWString();
	const auto valueSize = static_cast<DWORD>( ( valueData.size() + 1 ) * sizeof( wchar_t ) );
	if( RegSetValueExW( scopedKey.get(), nameData.c_str(), 0, REG_SZ,
						 reinterpret_cast<const BYTE*>( valueData.c_str() ), valueSize ) != ERROR_SUCCESS )
	{
		return false;
	}

	DWORD readType = 0;
	DWORD readSize = 0;
	if( RegQueryValueExW( scopedKey.get(), nameData.c_str(), nullptr, &readType, nullptr, &readSize ) != ERROR_SUCCESS ||
		readType != REG_SZ )
	{
		return false;
	}
	std::vector<wchar_t> readValue( readSize / sizeof( wchar_t ) + 1 );
	if( RegQueryValueExW( scopedKey.get(), nameData.c_str(), nullptr, &readType,
						 reinterpret_cast<BYTE*>( readValue.data() ), &readSize ) != ERROR_SUCCESS )
	{
		return false;
	}
	return QString::fromWCharArray( readValue.data() ) == value;
}


bool deleteRegistryValue( const wchar_t* path, const QString& name )
{
	HKEY key = nullptr;
	const auto openResult = RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0,
										KEY_SET_VALUE | KEY_WOW64_64KEY, &key );
	if( openResult == ERROR_FILE_NOT_FOUND )
	{
		return true;
	}
	if( openResult != ERROR_SUCCESS )
	{
		return false;
	}
	const ScopedRegistryKey scopedKey{key};
	const auto nameData = name.toStdWString();
	const auto result = RegDeleteValueW( scopedKey.get(), nameData.c_str() );
	return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}


bool deleteRegistryValueIfMatches( const wchar_t* path,
								   const QString& name,
								   const QString& expectedValue )
{
	RegistryValues values;
	QSet<QString> valueNames;
	if( readRegistryValues( path, &values, &valueNames ) == false )
	{
		return false;
	}
	if( valueNames.contains( name ) == false )
	{
		return true;
	}
	if( values.contains( name ) == false )
	{
		return false;
	}
	if( values.value( name ) != expectedValue )
	{
		return false;
	}
	return deleteRegistryValue( path, name );
}


bool clearOwnedPolicyValues( const wchar_t* policyKey, const wchar_t* ownershipKey )
{
	RegistryValues currentValues;
	RegistryValues ownershipValues;
	if( readRegistryValues( policyKey, &currentValues ) == false ||
		readRegistryValues( ownershipKey, &ownershipValues ) == false )
	{
		return false;
	}

	bool success = true;
	for( auto it = ownershipValues.cbegin(); it != ownershipValues.cend(); ++it )
	{
		const auto currentValue = currentValues.value( it.key() );
		if( currentValues.contains( it.key() ) && policyValueHash( currentValue ) == it.value().toLatin1() )
		{
			if( deleteRegistryValueIfMatches( policyKey, it.key(), currentValue ) )
			{
				success = deleteRegistryValue( ownershipKey, it.key() ) && success;
			}
			else
			{
				success = false;
			}
		}
		else if( currentValues.contains( it.key() ) )
		{
			vWarning() << "UrlBlocker: preserving externally changed browser policy value" << it.key();
			success = false;
		}
		else
		{
			success = deleteRegistryValue( ownershipKey, it.key() ) && success;
		}
	}
	return success;
}


bool writeOwnedPolicyValues( const wchar_t* policyKey,
								 const wchar_t* ownershipKey,
								 const QStringList& urls )
{
	RegistryValues currentValues;
	RegistryValues ownershipValues;
	QSet<QString> currentValueNames;
	if( readRegistryValues( policyKey, &currentValues, &currentValueNames ) == false ||
		readRegistryValues( ownershipKey, &ownershipValues ) == false )
	{
		return false;
	}
	qsizetype additions = 0;
	for( const auto& url : urls )
	{
		if( currentValues.values().contains( url ) == false )
		{
			++additions;
		}
	}
	if( currentValueNames.size() + additions > MaximumBrowserPolicyEntries )
	{
		vWarning() << "UrlBlocker: browser policy capacity would be exceeded";
		return false;
	}

	for( auto it = ownershipValues.begin(); it != ownershipValues.end(); )
	{
		if( currentValueNames.contains( it.key() ) == false )
		{
			deleteRegistryValue( ownershipKey, it.key() );
			it = ownershipValues.erase( it );
		}
		else if( currentValues.contains( it.key() ) == false ||
				 policyValueHash( currentValues.value( it.key() ) ) != it.value().toLatin1() )
		{
			vWarning() << "UrlBlocker: browser ownership conflict for value" << it.key();
			return false;
		}
		else
		{
			++it;
		}
	}

	QMap<QString, QString> desiredOwnedValues;
	QVector<QPair<QString, QString>> newlyAddedValues;
	quint64 candidateName = 1000000;
	for( const auto& url : urls )
	{
		QString existingOwnedName;
		for( auto it = ownershipValues.cbegin(); it != ownershipValues.cend(); ++it )
		{
			if( currentValues.value( it.key() ) == url )
			{
				existingOwnedName = it.key();
				break;
			}
		}
		if( existingOwnedName.isEmpty() == false )
		{
			desiredOwnedValues.insert( existingOwnedName, url );
			continue;
		}
		if( currentValues.values().contains( url ) )
		{
			continue;
		}

		QString valueName;
		do
		{
			valueName = QString::number( candidateName++ );
		}
		while( currentValueNames.contains( valueName ) );

		const auto hash = QString::fromLatin1( policyValueHash( url ) );
		if( setRegistryString( ownershipKey, valueName, hash ) == false ||
			setRegistryString( policyKey, valueName, url ) == false )
		{
			for( const auto& added : std::as_const( newlyAddedValues ) )
			{
				if( deleteRegistryValueIfMatches( policyKey, added.first, added.second ) )
				{
					deleteRegistryValue( ownershipKey, added.first );
				}
			}
			if( deleteRegistryValueIfMatches( policyKey, valueName, url ) )
			{
				deleteRegistryValue( ownershipKey, valueName );
			}
			return false;
		}
		newlyAddedValues.append( { valueName, url } );
		currentValues.insert( valueName, url );
		currentValueNames.insert( valueName );
		desiredOwnedValues.insert( valueName, url );
	}

	bool success = true;
	for( auto it = ownershipValues.cbegin(); it != ownershipValues.cend(); ++it )
	{
		if( desiredOwnedValues.contains( it.key() ) == false )
		{
			if( policyValueHash( currentValues.value( it.key() ) ) == it.value().toLatin1() )
			{
				if( deleteRegistryValueIfMatches( policyKey, it.key(), currentValues.value( it.key() ) ) )
				{
					success = deleteRegistryValue( ownershipKey, it.key() ) && success;
				}
				else
				{
					success = false;
				}
			}
		}
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
	closeNetworkEngine();
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

	const bool chromeSuccess = writeOwnedPolicyValues( ChromePolicyKey, ChromeOwnershipKey, urls );
	const bool edgeSuccess = writeOwnedPolicyValues( EdgePolicyKey, EdgeOwnershipKey, urls );
	if( chromeSuccess && edgeSuccess )
	{
		vCritical() << "UrlBlocker: applied" << urls.size() << "browser URL policies";
	}
	else
	{
		vCritical() << "UrlBlocker: failed to write browser policies; administrator privileges are required";
	}
#else
	Q_UNUSED( urls )
#endif
}


void UrlBlocker::clearBrowserPolicies()
{
#ifdef Q_OS_WIN
	const bool chromeSuccess = clearOwnedPolicyValues( ChromePolicyKey, ChromeOwnershipKey );
	const bool edgeSuccess = clearOwnedPolicyValues( EdgePolicyKey, EdgeOwnershipKey );
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
	bool allDomainsResolved = true;
	for( const auto& domain : std::as_const( m_blockedDomains ) )
	{
		const auto hostInfo = QHostInfo::fromName( domain );
		if( hostInfo.error() != QHostInfo::NoError )
		{
			allDomainsResolved = false;
			vCritical() << "UrlBlocker: failed to resolve one configured domain:" << hostInfo.errorString();
			continue;
		}

		const auto previousAddressCount = resolvedAddresses.size();
		for( const auto& address : hostInfo.addresses() )
		{
			if( address.protocol() == QAbstractSocket::IPv4Protocol ||
				address.protocol() == QAbstractSocket::IPv6Protocol )
			{
				resolvedAddresses.append( { domain, address } );
			}
		}
		if( resolvedAddresses.size() == previousAddressCount )
		{
			allDomainsResolved = false;
		}
	}

	if( resolvedAddresses.isEmpty() || allDomainsResolved == false )
	{
		vCritical() << "UrlBlocker: keeping existing WFP generation because resolution was incomplete";
		return;
	}

	auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine == nullptr )
	{
		FWPM_SESSION0 session{};
		session.flags = FWPM_SESSION_FLAG_DYNAMIC;
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
			FwpmEngineClose0( engine );
			m_filterEngine = nullptr;
			return;
		}
	}

	const auto transactionResult = FwpmTransactionBegin0( engine, 0 );
	if( transactionResult != ERROR_SUCCESS )
	{
		vCritical() << "UrlBlocker: failed to begin WFP transaction:" << transactionResult;
		return;
	}

	QVector<quint64> newFilterIds;
	bool success = true;
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
			newFilterIds.append( filterId );
		}
		else
		{
			Q_UNUSED( domain )
			Q_UNUSED( address )
			vCritical() << "UrlBlocker: failed to add a WFP address filter:" << addResult;
			success = false;
			break;
		}
	}

	if( success )
	{
		for( const auto filterId : std::as_const( m_filterIds ) )
		{
			if( FwpmFilterDeleteById0( engine, filterId ) != ERROR_SUCCESS )
			{
				success = false;
				break;
			}
		}
	}

	if( success == false )
	{
		FwpmTransactionAbort0( engine );
		vCritical() << "UrlBlocker: WFP replacement aborted; previous generation remains active";
		return;
	}

	const auto commitResult = FwpmTransactionCommit0( engine );
	if( commitResult != ERROR_SUCCESS )
	{
		FwpmTransactionAbort0( engine );
		vCritical() << "UrlBlocker: failed to commit WFP replacement:" << commitResult;
		return;
	}

	m_filterIds = newFilterIds;
	vCritical() << "UrlBlocker: applied" << m_filterIds.size()
			<< "WFP address filters for" << m_blockedDomains.size() << "domains";
#endif
}


void UrlBlocker::clearNetworkFilters()
{
#ifdef Q_OS_WIN
	const auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine != nullptr && m_filterIds.isEmpty() == false )
	{
		if( FwpmTransactionBegin0( engine, 0 ) != ERROR_SUCCESS )
		{
			vCritical() << "UrlBlocker: failed to begin WFP clear transaction";
			return;
		}
		bool success = true;
		for( const auto filterId : std::as_const( m_filterIds ) )
		{
			if( FwpmFilterDeleteById0( engine, filterId ) != ERROR_SUCCESS )
			{
				success = false;
				break;
			}
		}
		if( success == false || FwpmTransactionCommit0( engine ) != ERROR_SUCCESS )
		{
			FwpmTransactionAbort0( engine );
			vCritical() << "UrlBlocker: WFP clear aborted; existing generation remains active";
			return;
		}
	}
	m_filterIds.clear();
#endif
}


void UrlBlocker::closeNetworkEngine()
{
#ifdef Q_OS_WIN
	const auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine != nullptr )
	{
		FwpmSubLayerDeleteByKey0( engine, &AccessBlockSublayer );
		FwpmEngineClose0( engine );
	}
#endif
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
