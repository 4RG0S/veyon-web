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
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>

#include <string>
#endif


namespace
{

constexpr auto ResolutionTimeout = 5000;

#ifdef Q_OS_WIN
constexpr wchar_t ChromePolicyKey[] = L"SOFTWARE\\Policies\\Google\\Chrome\\URLBlocklist";
constexpr wchar_t EdgePolicyKey[] = L"SOFTWARE\\Policies\\Microsoft\\Edge\\URLBlocklist";
constexpr wchar_t ChromeOwnershipKey[] = L"SOFTWARE\\Veyon\\AccessBlock\\BrowserOwnership\\Chrome";
constexpr wchar_t EdgeOwnershipKey[] = L"SOFTWARE\\Veyon\\AccessBlock\\BrowserOwnership\\Edge";
constexpr qsizetype MaximumBrowserPolicyEntries = 1000;
constexpr UINT32 DynamicWfpSessionFlag = 0x00000001;

const GUID AccessBlockSublayer =
{ 0x9bf09f59, 0x6fe5, 0x4fef, { 0x9e, 0xb6, 0x37, 0xd5, 0x42, 0x64, 0xa1, 0xd2 } };


GUID accessBlockSublayerKey()
{
	auto key = AccessBlockSublayer;
	key.Data1 ^= GetCurrentProcessId();
	return key;
}


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


class ScopedNamedMutex
{
public:
	explicit ScopedNamedMutex( const wchar_t* name ) :
		m_mutex( CreateMutexW( nullptr, FALSE, name ) )
	{
		const auto waitResult = m_mutex ? WaitForSingleObject( m_mutex, 5000 ) : WAIT_FAILED;
		if( waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED )
		{
			m_locked = true;
		}
	}

	~ScopedNamedMutex()
	{
		if( m_locked )
		{
			ReleaseMutex( m_mutex );
		}
		if( m_mutex )
		{
			CloseHandle( m_mutex );
		}
	}

	ScopedNamedMutex( const ScopedNamedMutex& ) = delete;
	ScopedNamedMutex& operator=( const ScopedNamedMutex& ) = delete;

	explicit operator bool() const
	{
		return m_locked;
	}

private:
	HANDLE m_mutex{};
	bool m_locked{};
};


using RegistryValues = QMap<QString, QString>;


struct OwnedPolicyState
{
	QStringList foreignUrls;
	QStringList ownedUrls;
	RegistryValues ownedValues;
};


enum class ConditionalWriteResult
{
	Written,
	PreconditionMismatch,
	WriteFailure,
};


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


bool setRegistryStringValue( HKEY key, const QString& name, const QString& value )
{
	const auto nameData = name.toStdWString();
	const auto valueData = value.toStdWString();
	const auto valueSize = static_cast<DWORD>( ( valueData.size() + 1 ) * sizeof( wchar_t ) );
	if( RegSetValueExW( key, nameData.c_str(), 0, REG_SZ,
						 reinterpret_cast<const BYTE*>( valueData.c_str() ), valueSize ) != ERROR_SUCCESS )
	{
		return false;
	}

	DWORD readType = 0;
	DWORD readSize = 0;
	if( RegQueryValueExW( key, nameData.c_str(), nullptr, &readType, nullptr, &readSize ) != ERROR_SUCCESS ||
		readType != REG_SZ )
	{
		return false;
	}
	std::vector<wchar_t> readValue( readSize / sizeof( wchar_t ) + 1 );
	if( RegQueryValueExW( key, nameData.c_str(), nullptr, &readType,
						 reinterpret_cast<BYTE*>( readValue.data() ), &readSize ) != ERROR_SUCCESS )
	{
		return false;
	}
	return QString::fromWCharArray( readValue.data() ) == value &&
			RegFlushKey( key ) == ERROR_SUCCESS;
}


bool setRegistryString( const wchar_t* path, const QString& name, const QString& value )
{
	HKEY key = nullptr;
	if( openOrCreateRegistryKey( path, KEY_QUERY_VALUE | KEY_SET_VALUE, &key ) == false )
	{
		return false;
	}
	const ScopedRegistryKey scopedKey{key};
	return setRegistryStringValue( scopedKey.get(), name, value );
}


ConditionalWriteResult setRegistryStringIfMatches( const wchar_t* path,
													const QString& name,
													const QString* expectedValue,
													const QString& value )
{
	HKEY key = nullptr;
	if( openOrCreateRegistryKey( path, KEY_QUERY_VALUE | KEY_SET_VALUE, &key ) == false )
	{
		return ConditionalWriteResult::WriteFailure;
	}
	const ScopedRegistryKey scopedKey{key};
	const auto nameData = name.toStdWString();
	DWORD valueType = 0;
	DWORD valueSize = 0;
	const auto sizeResult = RegQueryValueExW(
			scopedKey.get(), nameData.c_str(), nullptr, &valueType, nullptr, &valueSize );
	if( expectedValue == nullptr )
	{
		if( sizeResult != ERROR_FILE_NOT_FOUND )
		{
			return ConditionalWriteResult::PreconditionMismatch;
		}
	}
	else
	{
		if( sizeResult != ERROR_SUCCESS || valueType != REG_SZ || valueSize < sizeof( wchar_t ) )
		{
			return ConditionalWriteResult::PreconditionMismatch;
		}
		std::vector<wchar_t> currentValue( valueSize / sizeof( wchar_t ) + 1 );
		if( RegQueryValueExW( scopedKey.get(), nameData.c_str(), nullptr, &valueType,
							 reinterpret_cast<BYTE*>( currentValue.data() ), &valueSize ) != ERROR_SUCCESS ||
			QString::fromWCharArray( currentValue.data() ) != *expectedValue )
		{
			return ConditionalWriteResult::PreconditionMismatch;
		}
	}

	return setRegistryStringValue( scopedKey.get(), name, value )
			? ConditionalWriteResult::Written : ConditionalWriteResult::WriteFailure;
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
	return result == ERROR_FILE_NOT_FOUND ||
			( result == ERROR_SUCCESS && RegFlushKey( scopedKey.get() ) == ERROR_SUCCESS );
}


bool deleteRegistryValueIfMatches( const wchar_t* path,
								   const QString& name,
								   const QString& expectedValue )
{
	HKEY key = nullptr;
	const auto openResult = RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0,
										KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &key );
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
	DWORD valueType = 0;
	DWORD valueSize = 0;
	const auto sizeResult = RegQueryValueExW(
			scopedKey.get(), nameData.c_str(), nullptr, &valueType, nullptr, &valueSize );
	if( sizeResult == ERROR_FILE_NOT_FOUND )
	{
		return true;
	}
	if( sizeResult != ERROR_SUCCESS || valueType != REG_SZ || valueSize < sizeof( wchar_t ) )
	{
		return false;
	}
	std::vector<wchar_t> value( valueSize / sizeof( wchar_t ) + 1 );
	if( RegQueryValueExW( scopedKey.get(), nameData.c_str(), nullptr, &valueType,
						 reinterpret_cast<BYTE*>( value.data() ), &valueSize ) != ERROR_SUCCESS ||
		QString::fromWCharArray( value.data() ) != expectedValue )
	{
		return false;
	}
	return RegDeleteValueW( scopedKey.get(), nameData.c_str() ) == ERROR_SUCCESS &&
			RegFlushKey( scopedKey.get() ) == ERROR_SUCCESS;
}


QString pendingOwnershipRecord( const QString& oldHash, const QString& newHash )
{
	return QStringLiteral("P:%1:%2").arg( oldHash, newHash );
}


bool recoverPendingOwnership( const wchar_t* policyKey,
								  const wchar_t* ownershipKey,
								  const RegistryValues& currentValues,
								  const QSet<QString>& currentValueNames,
								  const RegistryValues& ownershipValues )
{
	for( auto it = ownershipValues.cbegin(); it != ownershipValues.cend(); ++it )
	{
		if( it.value().startsWith( QStringLiteral("P:") ) == false )
		{
			if( currentValueNames.contains( it.key() ) == false )
			{
				if( deleteRegistryValue( ownershipKey, it.key() ) == false )
				{
					return false;
				}
			}
			else if( currentValues.contains( it.key() ) == false ||
					 policyValueHash( currentValues.value( it.key() ) ) != it.value().toLatin1() )
			{
				return false;
			}
			continue;
		}

		const auto parts = it.value().split( QLatin1Char(':') );
		if( parts.size() != 3 )
		{
			return false;
		}
		const auto& oldHash = parts.at( 1 );
		const auto& newHash = parts.at( 2 );
		if( currentValueNames.contains( it.key() ) == false )
		{
			if( oldHash == QLatin1String("-") || newHash == QLatin1String("-") )
			{
				if( deleteRegistryValue( ownershipKey, it.key() ) == false )
				{
					return false;
				}
				continue;
			}
			return false;
		}
		if( oldHash == QLatin1String("-") )
		{
			// A crashed add cannot be distinguished from an external writer that
			// populated the same slot. Relinquish ownership and preserve the value.
			if( deleteRegistryValue( ownershipKey, it.key() ) == false )
			{
				return false;
			}
			continue;
		}
		if( currentValues.contains( it.key() ) == false )
		{
			return false;
		}

		const auto currentHash = QString::fromLatin1( policyValueHash( currentValues.value( it.key() ) ) );
		if( newHash != QLatin1String("-") && currentHash == newHash )
		{
			if( setRegistryString( ownershipKey, it.key(), newHash ) == false )
			{
				return false;
			}
		}
		else if( newHash == QLatin1String("-") && currentHash == oldHash )
		{
			if( deleteRegistryValueIfMatches( policyKey, it.key(), currentValues.value( it.key() ) ) == false ||
				deleteRegistryValue( ownershipKey, it.key() ) == false )
			{
				return false;
			}
		}
		else if( oldHash != QLatin1String("-") && currentHash == oldHash )
		{
			if( setRegistryString( ownershipKey, it.key(), oldHash ) == false )
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}


bool readOwnedPolicyState( const wchar_t* policyKey,
							  const wchar_t* ownershipKey,
							  OwnedPolicyState* state )
{
	RegistryValues currentValues;
	RegistryValues ownershipValues;
	QSet<QString> currentValueNames;
	QSet<QString> ownershipValueNames;
	if( readRegistryValues( policyKey, &currentValues, &currentValueNames ) == false ||
		readRegistryValues( ownershipKey, &ownershipValues, &ownershipValueNames ) == false ||
		ownershipValueNames.size() != ownershipValues.size() )
	{
		return false;
	}
	if( recoverPendingOwnership( policyKey, ownershipKey, currentValues,
								 currentValueNames, ownershipValues ) == false ||
		readRegistryValues( policyKey, &currentValues, &currentValueNames ) == false ||
		readRegistryValues( ownershipKey, &ownershipValues, &ownershipValueNames ) == false )
	{
		vWarning() << "UrlBlocker: browser policy ownership recovery is required";
		return false;
	}
	if( currentValueNames.size() != currentValues.size() ||
		ownershipValueNames.size() != ownershipValues.size() )
	{
		vWarning() << "UrlBlocker: non-string browser policy values were preserved";
		return false;
	}

	QMap<quint64, QString> indexedValues;
	for( auto it = currentValues.cbegin(); it != currentValues.cend(); ++it )
	{
		bool validIndex = false;
		const auto index = it.key().toULongLong( &validIndex );
		if( validIndex == false || index == 0 || QString::number( index ) != it.key() ||
			indexedValues.contains( index ) )
		{
			vWarning() << "UrlBlocker: non-canonical browser list value names were preserved";
			return false;
		}
		indexedValues.insert( index, it.value() );
	}

	for( auto it = ownershipValues.cbegin(); it != ownershipValues.cend(); ++it )
	{
		if( currentValues.contains( it.key() ) == false ||
			policyValueHash( currentValues.value( it.key() ) ) != it.value().toLatin1() )
		{
			vWarning() << "UrlBlocker: browser ownership manifest does not match policy values";
			return false;
		}
	}

	state->foreignUrls.clear();
	state->ownedUrls.clear();
	state->ownedValues.clear();
	quint64 nextForeignIndex = 1;
	while( indexedValues.contains( nextForeignIndex ) &&
		   ownershipValues.contains( QString::number( nextForeignIndex ) ) == false )
	{
		state->foreignUrls.append( indexedValues.value( nextForeignIndex ) );
		++nextForeignIndex;
	}

	QMap<quint64, QString> indexedOwnedValues;
	for( auto it = indexedValues.cbegin(); it != indexedValues.cend(); ++it )
	{
		const auto name = QString::number( it.key() );
		if( ownershipValues.contains( name ) )
		{
			state->ownedValues.insert( name, it.value() );
			indexedOwnedValues.insert( it.key(), it.value() );
		}
		else if( it.key() >= nextForeignIndex )
		{
			vWarning() << "UrlBlocker: unmanaged browser values outside the contiguous prefix were preserved";
			return false;
		}
	}
	for( const auto& value : std::as_const( indexedOwnedValues ) )
	{
		state->ownedUrls.append( value );
	}
	return true;
}


bool writeOwnedPolicyValues( const wchar_t* policyKey,
								 const wchar_t* ownershipKey,
								 const QStringList& urls )
{
	OwnedPolicyState currentState;
	if( readOwnedPolicyState( policyKey, ownershipKey, &currentState ) == false )
	{
		return false;
	}
	if( currentState.foreignUrls.size() + urls.size() > MaximumBrowserPolicyEntries )
	{
		vWarning() << "UrlBlocker: browser policy capacity would be exceeded";
		return false;
	}

	QSet<QString> desiredOwnedNames;
	for( qsizetype index = 0; index < urls.size(); ++index )
	{
		const auto name = QString::number( currentState.foreignUrls.size() + index + 1 );
		desiredOwnedNames.insert( name );
		const auto& newValue = urls.at( index );
		if( currentState.ownedValues.value( name ) == newValue )
		{
			continue;
		}
		const auto replacesOwnedValue = currentState.ownedValues.contains( name );
		const auto oldHash = replacesOwnedValue
				? QString::fromLatin1( policyValueHash( currentState.ownedValues.value( name ) ) )
				: QStringLiteral("-");
		const auto newHash = QString::fromLatin1( policyValueHash( newValue ) );
		const auto oldValue = currentState.ownedValues.value( name );
		const auto expectedValue = replacesOwnedValue ? &oldValue : nullptr;
		if( replacesOwnedValue &&
			setRegistryString( ownershipKey, name, pendingOwnershipRecord( oldHash, newHash ) ) == false )
		{
			return false;
		}

		const auto writeResult = setRegistryStringIfMatches(
				policyKey, name, expectedValue, newValue );
		if( writeResult != ConditionalWriteResult::Written )
		{
			if( replacesOwnedValue && writeResult == ConditionalWriteResult::PreconditionMismatch )
			{
				if( deleteRegistryValue( ownershipKey, name ) == false )
				{
					vCritical() << "UrlBlocker: failed to relinquish a contested browser policy value";
				}
			}
			return false;
		}
		if( setRegistryString( ownershipKey, name, newHash ) == false )
		{
			if( replacesOwnedValue == false )
			{
				const bool ownershipCleared = deleteRegistryValue( ownershipKey, name );
				const bool policyRolledBack = ownershipCleared &&
						deleteRegistryValueIfMatches( policyKey, name, newValue );
				if( ownershipCleared == false || policyRolledBack == false )
				{
					vCritical() << "UrlBlocker: new browser policy ownership failed and rollback requires recovery";
				}
			}
			return false;
		}
	}

	QMap<quint64, QString> obsoleteOwnedNames;
	for( auto it = currentState.ownedValues.cbegin(); it != currentState.ownedValues.cend(); ++it )
	{
		if( desiredOwnedNames.contains( it.key() ) == false )
		{
			obsoleteOwnedNames.insert( it.key().toULongLong(), it.key() );
		}
	}
	for( auto it = obsoleteOwnedNames.crbegin(); it != obsoleteOwnedNames.crend(); ++it )
	{
		const auto& name = it.value();
		const auto oldValue = currentState.ownedValues.value( name );
		const auto oldHash = QString::fromLatin1( policyValueHash( oldValue ) );
		if( setRegistryString( ownershipKey, name,
							   pendingOwnershipRecord( oldHash, QStringLiteral("-") ) ) == false ||
			deleteRegistryValueIfMatches( policyKey, name, oldValue ) == false ||
			deleteRegistryValue( ownershipKey, name ) == false )
		{
			return false;
		}
	}

	return true;
}


bool clearOwnedPolicyValues( const wchar_t* policyKey, const wchar_t* ownershipKey )
{
	return writeOwnedPolicyValues( policyKey, ownershipKey, {} );
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
	m_resolutionTimer.setSingleShot( true );
	m_resolutionTimer.setInterval( ResolutionTimeout );
	connect( &m_resolutionTimer, &QTimer::timeout, this, [this]() {
		if( m_pendingDomains.isEmpty() == false )
		{
			cancelNetworkResolution();
			vCritical() << "UrlBlocker: DNS resolution timed out; previous WFP generation remains active";
		}
	} );
}


UrlBlocker::~UrlBlocker()
{
	m_refreshTimer.stop();
	cancelNetworkResolution();
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
	const ScopedNamedMutex policyMutex{L"Global\\VeyonAccessBlockBrowserPolicy"};
	if( !policyMutex )
	{
		vCritical() << "UrlBlocker: could not acquire browser policy writer mutex";
		return;
	}
	OwnedPolicyState previousChromeState;
	OwnedPolicyState previousEdgeState;
	if( readOwnedPolicyState( ChromePolicyKey, ChromeOwnershipKey, &previousChromeState ) == false ||
		readOwnedPolicyState( EdgePolicyKey, EdgeOwnershipKey, &previousEdgeState ) == false )
	{
		vCritical() << "UrlBlocker: browser policy preflight failed; unmanaged values were preserved";
		return;
	}

	const bool chromeSuccess = writeOwnedPolicyValues( ChromePolicyKey, ChromeOwnershipKey, urls );
	const bool edgeSuccess = chromeSuccess &&
			writeOwnedPolicyValues( EdgePolicyKey, EdgeOwnershipKey, urls );
	if( chromeSuccess && edgeSuccess )
	{
		vCritical() << "UrlBlocker: applied" << urls.size() << "browser URL policies";
	}
	else
	{
		const bool edgeRollback = writeOwnedPolicyValues(
				EdgePolicyKey, EdgeOwnershipKey, previousEdgeState.ownedUrls );
		const bool chromeRollback = writeOwnedPolicyValues(
				ChromePolicyKey, ChromeOwnershipKey, previousChromeState.ownedUrls );
		vCritical() << "UrlBlocker: browser policy apply failed; rollback"
					<< ( chromeRollback && edgeRollback ? "completed" : "requires recovery" );
	}
#else
	Q_UNUSED( urls )
#endif
}


void UrlBlocker::clearBrowserPolicies()
{
#ifdef Q_OS_WIN
	const ScopedNamedMutex policyMutex{L"Global\\VeyonAccessBlockBrowserPolicy"};
	if( !policyMutex )
	{
		vCritical() << "UrlBlocker: could not acquire browser policy writer mutex";
		return;
	}
	OwnedPolicyState previousChromeState;
	OwnedPolicyState previousEdgeState;
	if( readOwnedPolicyState( ChromePolicyKey, ChromeOwnershipKey, &previousChromeState ) == false ||
		readOwnedPolicyState( EdgePolicyKey, EdgeOwnershipKey, &previousEdgeState ) == false )
	{
		vCritical() << "UrlBlocker: browser policy clear preflight failed; unmanaged values were preserved";
		return;
	}

	const bool chromeSuccess = clearOwnedPolicyValues( ChromePolicyKey, ChromeOwnershipKey );
	const bool edgeSuccess = chromeSuccess &&
			clearOwnedPolicyValues( EdgePolicyKey, EdgeOwnershipKey );
	if( chromeSuccess && edgeSuccess )
	{
		vCritical() << "UrlBlocker: cleared browser policies";
	}
	else
	{
		const bool edgeRollback = writeOwnedPolicyValues(
				EdgePolicyKey, EdgeOwnershipKey, previousEdgeState.ownedUrls );
		const bool chromeRollback = writeOwnedPolicyValues(
				ChromePolicyKey, ChromeOwnershipKey, previousChromeState.ownedUrls );
		vCritical() << "UrlBlocker: browser policy clear failed; rollback"
					<< ( chromeRollback && edgeRollback ? "completed" : "requires recovery" );
	}
#endif
}


void UrlBlocker::applyNetworkFilters()
{
#ifdef Q_OS_WIN
	cancelNetworkResolution();
	const auto generation = m_resolutionGeneration;
	if( m_blockedDomains.isEmpty() )
	{
		if( clearNetworkFilters() == false )
		{
			vWarning() << "UrlBlocker: closing WFP session after transactional clear failure";
			closeNetworkEngine();
		}
		return;
	}

	m_resolutionFailed = false;
	m_resolvedAddresses.clear();
	for( const auto& domain : std::as_const( m_blockedDomains ) )
	{
		m_pendingDomains.insert( domain );
		const auto lookupId = QHostInfo::lookupHost(
				domain, this,
				[this, generation, domain]( const QHostInfo& hostInfo ) {
					if( generation != m_resolutionGeneration ||
						m_pendingDomains.remove( domain ) == 0 )
					{
						return;
					}

					bool domainResolved = false;
					for( const auto& address : hostInfo.addresses() )
					{
						if( address.protocol() == QAbstractSocket::IPv4Protocol ||
							address.protocol() == QAbstractSocket::IPv6Protocol )
						{
							const QPair<QString, QHostAddress> resolvedAddress{domain, address};
							if( m_resolvedAddresses.contains( resolvedAddress ) == false )
							{
								m_resolvedAddresses.append( resolvedAddress );
							}
							domainResolved = true;
						}
					}
					if( domainResolved == false )
					{
						m_resolutionFailed = true;
						vCritical() << "UrlBlocker: failed to resolve one configured domain:"
									<< hostInfo.errorString();
					}
					if( m_pendingDomains.isEmpty() )
					{
						finishNetworkResolution( generation );
					}
				} );
		if( lookupId < 0 )
		{
			m_pendingDomains.remove( domain );
			m_resolutionFailed = true;
		}
		else
		{
			m_lookupIds.append( lookupId );
		}
	}

	if( m_pendingDomains.isEmpty() )
	{
		finishNetworkResolution( generation );
	}
	else
	{
		m_resolutionTimer.start();
	}
#endif
}


void UrlBlocker::cancelNetworkResolution()
{
	++m_resolutionGeneration;
	m_resolutionTimer.stop();
	for( const auto lookupId : std::as_const( m_lookupIds ) )
	{
		QHostInfo::abortHostLookup( lookupId );
	}
	m_lookupIds.clear();
	m_pendingDomains.clear();
	m_resolvedAddresses.clear();
}


void UrlBlocker::finishNetworkResolution( quint64 generation )
{
#ifdef Q_OS_WIN
	if( generation != m_resolutionGeneration )
	{
		return;
	}
	m_resolutionTimer.stop();
	m_lookupIds.clear();
	if( m_resolutionFailed || m_resolvedAddresses.isEmpty() )
	{
		m_resolvedAddresses.clear();
		vCritical() << "UrlBlocker: keeping existing WFP generation because resolution was incomplete";
		return;
	}

	const auto resolvedAddresses = m_resolvedAddresses;
	m_resolvedAddresses.clear();
	replaceNetworkFilters( resolvedAddresses );
#else
	Q_UNUSED( generation )
#endif
}


void UrlBlocker::replaceNetworkFilters(
		const QVector<QPair<QString, QHostAddress>>& resolvedAddresses )
{
#ifdef Q_OS_WIN

	auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine == nullptr )
	{
		FWPM_SESSION0 session{};
		session.flags = DynamicWfpSessionFlag;
		const auto openResult = FwpmEngineOpen0( nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine );
		if( openResult != ERROR_SUCCESS )
		{
			vCritical() << "UrlBlocker: failed to open WFP engine:" << openResult;
			return;
		}
		m_filterEngine = engine;

		const auto sublayerKey = accessBlockSublayerKey();
		FWPM_SUBLAYER0 sublayer{};
		sublayer.subLayerKey = sublayerKey;
		sublayer.displayData.name = const_cast<wchar_t*>( L"CHECK NODE domain blocking" );
		sublayer.weight = 0x100;
		const auto sublayerResult = FwpmSubLayerAdd0( engine, &sublayer, nullptr );
		if( sublayerResult != ERROR_SUCCESS &&
			sublayerResult != static_cast<DWORD>( FWP_E_ALREADY_EXISTS ) )
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
		filter.subLayerKey = accessBlockSublayerKey();
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
			const auto deleteResult = FwpmFilterDeleteById0( engine, filterId );
			if( deleteResult != ERROR_SUCCESS &&
				deleteResult != static_cast<DWORD>( FWP_E_FILTER_NOT_FOUND ) )
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


bool UrlBlocker::clearNetworkFilters()
{
#ifdef Q_OS_WIN
	const auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine != nullptr && m_filterIds.isEmpty() == false )
	{
		if( FwpmTransactionBegin0( engine, 0 ) != ERROR_SUCCESS )
		{
			vCritical() << "UrlBlocker: failed to begin WFP clear transaction";
			return false;
		}
		bool success = true;
		for( const auto filterId : std::as_const( m_filterIds ) )
		{
			const auto deleteResult = FwpmFilterDeleteById0( engine, filterId );
			if( deleteResult != ERROR_SUCCESS &&
				deleteResult != static_cast<DWORD>( FWP_E_FILTER_NOT_FOUND ) )
			{
				success = false;
				break;
			}
		}
		if( success == false || FwpmTransactionCommit0( engine ) != ERROR_SUCCESS )
		{
			FwpmTransactionAbort0( engine );
			vCritical() << "UrlBlocker: WFP clear aborted; existing generation remains active";
			return false;
		}
	}
	m_filterIds.clear();
#endif
	return true;
}


void UrlBlocker::closeNetworkEngine()
{
#ifdef Q_OS_WIN
	const auto engine = static_cast<HANDLE>( m_filterEngine );
	if( engine != nullptr )
	{
		const auto sublayerKey = accessBlockSublayerKey();
		FwpmSubLayerDeleteByKey0( engine, &sublayerKey );
		FwpmEngineClose0( engine );
	}
#endif
	m_filterIds.clear();
	m_filterEngine = nullptr;
}


void UrlBlocker::clear()
{
	m_refreshTimer.stop();
	cancelNetworkResolution();
	if( clearNetworkFilters() == false )
	{
		vWarning() << "UrlBlocker: closing WFP session after transactional clear failure";
		closeNetworkEngine();
	}
	clearBrowserPolicies();
	m_blockedDomains.clear();
	m_blockedBrowserUrls.clear();
}
