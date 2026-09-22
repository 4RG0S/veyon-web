/*
 * ProcessBlocker.cpp - blocks execution of programs on the local (slave) machine
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

#include "ProcessBlocker.h"
#include "VeyonCore.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif


namespace
{

constexpr auto PollInterval = 1000;
#ifdef Q_OS_WIN
constexpr DWORD TerminationWaitTimeout = 500;
#endif


QString normalizedExecutableName( const QString& app )
{
	return QFileInfo( app.trimmed() ).fileName().toCaseFolded();
}


bool isProtectedExecutableName( const QString& executableName )
{
	static const QStringList protectedExecutableNames{
		QStringLiteral( "idle" ),
		QStringLiteral( "system" ),
		QStringLiteral( "registry" ),
		QStringLiteral( "smss.exe" ),
		QStringLiteral( "csrss.exe" ),
		QStringLiteral( "wininit.exe" ),
		QStringLiteral( "winlogon.exe" ),
		QStringLiteral( "services.exe" ),
		QStringLiteral( "lsass.exe" ),
		QStringLiteral( "svchost.exe" ),
		QStringLiteral( "fontdrvhost.exe" ),
		QStringLiteral( "dwm.exe" ),
		QStringLiteral( "veyon-cli.exe" ),
		QStringLiteral( "veyon-configurator.exe" ),
		QStringLiteral( "veyon-master.exe" ),
		QStringLiteral( "veyon-server.exe" ),
		QStringLiteral( "veyon-service.exe" ),
		QStringLiteral( "veyon-worker.exe" ),
	};

	if( protectedExecutableNames.contains( executableName ) )
	{
		return true;
	}

	return executableName == normalizedExecutableName( QCoreApplication::applicationFilePath() );
}


#ifdef Q_OS_WIN
struct ProcessIdentity
{
	DWORD processId{};
	DWORD sessionId{};
	FILETIME creationTime{};
	QString executableName;
};


class ScopedHandle
{
public:
	explicit ScopedHandle( HANDLE handle ) :
		m_handle( handle )
	{
	}

	~ScopedHandle()
	{
		if( m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE )
		{
			CloseHandle( m_handle );
		}
	}

	ScopedHandle( const ScopedHandle& ) = delete;
	ScopedHandle& operator=( const ScopedHandle& ) = delete;

	HANDLE get() const
	{
		return m_handle;
	}

	explicit operator bool() const
	{
		return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
	}

private:
	HANDLE m_handle;

};


bool resolveProcessIdentity( HANDLE process,
							 DWORD snapshotProcessId,
							 const QString& snapshotExecutableName,
							 ProcessIdentity* identity )
{
	if( process == nullptr || identity == nullptr || GetProcessId( process ) != snapshotProcessId )
	{
		return false;
	}

	FILETIME exitTime{};
	FILETIME kernelTime{};
	FILETIME userTime{};
	if( GetProcessTimes( process, &identity->creationTime, &exitTime, &kernelTime, &userTime ) == FALSE )
	{
		return false;
	}

	std::wstring imagePath( 32768, L'\0' );
	DWORD imagePathSize = static_cast<DWORD>( imagePath.size() );
	if( QueryFullProcessImageNameW( process, 0, imagePath.data(), &imagePathSize ) == FALSE )
	{
		return false;
	}
	imagePath.resize( imagePathSize );

	identity->processId = snapshotProcessId;
	identity->executableName = normalizedExecutableName( QString::fromStdWString( imagePath ) );
	if( identity->executableName != snapshotExecutableName ||
		ProcessIdToSessionId( snapshotProcessId, &identity->sessionId ) == FALSE )
	{
		return false;
	}

	return true;
}


bool isProtectedProcess( const ProcessIdentity& identity )
{
	return identity.processId == 0 ||
			identity.processId == 4 ||
			identity.processId == GetCurrentProcessId() ||
			identity.sessionId == 0 ||
			isProtectedExecutableName( identity.executableName );
}
#endif

}


ProcessBlocker::ProcessBlocker( QObject* parent ) :
	QObject( parent )
{
	m_pollTimer.setInterval( PollInterval );
	connect( &m_pollTimer, &QTimer::timeout, this, &ProcessBlocker::pollProcesses );
}



void ProcessBlocker::apply( const QStringList& apps )
{
	QStringList normalizedApps;
	for( const auto& app : apps )
	{
		const auto executableName = normalizedExecutableName( app );
		if( executableName.isEmpty() == false &&
			isProtectedExecutableName( executableName ) == false &&
			normalizedApps.contains( executableName ) == false )
		{
			normalizedApps.append( executableName );
		}
	}

	if( normalizedApps.isEmpty() )
	{
		clear();
		return;
	}

	m_blockedApps = normalizedApps;
	m_pollTimer.start();
	pollProcesses();
	vCritical() << "ProcessBlocker: monitoring" << m_blockedApps.size() << "application rules";
}



void ProcessBlocker::clear()
{
	m_pollTimer.stop();
	m_blockedApps.clear();
	vCritical() << "ProcessBlocker: monitoring stopped";
}



void ProcessBlocker::pollProcesses()
{
#ifdef Q_OS_WIN
	const ScopedHandle snapshot{CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 )};
	if( !snapshot )
	{
		vCritical() << "ProcessBlocker: failed to enumerate processes:" << GetLastError();
		return;
	}

	PROCESSENTRY32W processEntry{};
	processEntry.dwSize = sizeof( processEntry );
	if( Process32FirstW( snapshot.get(), &processEntry ) )
	{
		do
		{
			const auto executableName = normalizedExecutableName( QString::fromWCharArray( processEntry.szExeFile ) );
			if( m_blockedApps.contains( executableName ) &&
				processEntry.th32ProcessID != 0 &&
				processEntry.th32ProcessID != 4 &&
				processEntry.th32ProcessID != GetCurrentProcessId() )
			{
				const ScopedHandle process{OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION |
													PROCESS_TERMINATE | SYNCHRONIZE,
												FALSE, processEntry.th32ProcessID )};
				if( process )
				{
					ProcessIdentity identity;
					if( resolveProcessIdentity( process.get(), processEntry.th32ProcessID,
										   executableName, &identity ) == false )
					{
						vWarning() << "ProcessBlocker: identity changed or could not be verified for PID"
								   << processEntry.th32ProcessID;
						continue;
					}

					if( isProtectedProcess( identity ) )
					{
						vWarning() << "ProcessBlocker: refusing to terminate protected process PID"
								   << identity.processId;
						continue;
					}

					if( TerminateProcess( process.get(), 0 ) )
					{
						const auto waitResult = WaitForSingleObject( process.get(), TerminationWaitTimeout );
						if( waitResult == WAIT_OBJECT_0 )
						{
							vCritical() << "ProcessBlocker: terminated" << executableName
										<< "PID" << identity.processId;
						}
						else
						{
							vCritical() << "ProcessBlocker: termination was not confirmed for PID"
										<< identity.processId << "wait result" << waitResult
										<< "error" << ( waitResult == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT );
						}
					}
					else
					{
						vCritical() << "ProcessBlocker: failed to terminate" << executableName
									<< "PID" << processEntry.th32ProcessID << "error" << GetLastError();
					}
				}
				else
				{
					vCritical() << "ProcessBlocker: cannot open" << executableName
								<< "PID" << processEntry.th32ProcessID << "error" << GetLastError();
				}
			}
		}
		while( Process32NextW( snapshot.get(), &processEntry ) );
	}
#endif

}
