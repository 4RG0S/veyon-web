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


ProcessBlocker::ProcessBlocker( QObject* parent ) :
	QObject( parent )
{
}



void ProcessBlocker::apply( const QStringList& apps )
{
	m_blockedApps = apps;

	// default log level is Warning, so use vCritical() to make this visible
	vCritical() << "ProcessBlocker: apply" << apps;

	// TODO(P2): 실제 프로세스 차단
	//  - QTimer 주기 폴링으로 CreateToolhelp32Snapshot 열거
	//  - 매칭되는 프로세스 OpenProcess(PROCESS_TERMINATE) -> TerminateProcess
	//  - (대안) IFEO 레지스트리로 실행 자체 차단
	//  타 세션 프로세스 종료엔 서비스(SYSTEM) 권한 필요.
}



void ProcessBlocker::clear()
{
	vCritical() << "ProcessBlocker: clear";

	m_blockedApps.clear();

	// TODO(P2): 감시 중단 (QTimer 정지) 및 IFEO 등 적용분 원복
}
