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

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif


namespace
{

constexpr auto PollInterval = 1000;


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


bool isProtectedProcess( DWORD processId, const QString& executableName )
{
	return processId == 0 ||
			processId == 4 ||
			processId == GetCurrentProcessId() ||
			isProtectedExecutableName( executableName );
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
	vCritical() << "ProcessBlocker: monitoring applications:" << m_blockedApps;
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
				isProtectedProcess( processEntry.th32ProcessID, executableName ) == false )
			{
				const ScopedHandle process{OpenProcess( PROCESS_TERMINATE, FALSE, processEntry.th32ProcessID )};
				if( process )
				{
					if( TerminateProcess( process.get(), 0 ) )
					{
						vCritical() << "ProcessBlocker: terminated" << executableName
									<< "PID" << processEntry.th32ProcessID;
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
