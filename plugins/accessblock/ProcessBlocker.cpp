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

#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif


ProcessBlocker::ProcessBlocker( QObject* parent ) :
	QObject( parent )
{
	m_pollTimer.setInterval( 1000 );
	connect( &m_pollTimer, &QTimer::timeout, this, &ProcessBlocker::pollProcesses );
}



void ProcessBlocker::apply( const QStringList& apps )
{
	QStringList normalizedApps;
	for( const auto& app : apps )
	{
		const auto executableName = QFileInfo( app.trimmed() ).fileName();
		if( executableName.isEmpty() == false &&
			normalizedApps.contains( executableName, Qt::CaseInsensitive ) == false )
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
	const auto snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if( snapshot == INVALID_HANDLE_VALUE )
	{
		vCritical() << "ProcessBlocker: failed to enumerate processes:" << GetLastError();
		return;
	}

	PROCESSENTRY32W processEntry{};
	processEntry.dwSize = sizeof( processEntry );
	if( Process32FirstW( snapshot, &processEntry ) )
	{
		do
		{
			const auto executableName = QString::fromWCharArray( processEntry.szExeFile );
			if( m_blockedApps.contains( executableName, Qt::CaseInsensitive ) )
			{
				const auto process = OpenProcess( PROCESS_TERMINATE, FALSE, processEntry.th32ProcessID );
				if( process != nullptr )
				{
					if( TerminateProcess( process, 1 ) )
					{
						vCritical() << "ProcessBlocker: terminated" << executableName
									<< "PID" << processEntry.th32ProcessID;
					}
					else
					{
						vCritical() << "ProcessBlocker: failed to terminate" << executableName
									<< "PID" << processEntry.th32ProcessID << "error" << GetLastError();
					}
					CloseHandle( process );
				}
				else
				{
					vCritical() << "ProcessBlocker: cannot open" << executableName
								<< "PID" << processEntry.th32ProcessID << "error" << GetLastError();
				}
			}
		}
		while( Process32NextW( snapshot, &processEntry ) );
	}

	CloseHandle( snapshot );
#endif

}
