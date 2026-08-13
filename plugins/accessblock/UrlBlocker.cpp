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


void UrlBlocker::apply( const QStringList& urls )
{
	m_blockedUrls = urls;

	// default log level is Warning, so use vCritical() to make this visible
	vCritical() << "UrlBlocker: apply" << urls;

	// TODO(P1): 실제 URL 차단
	//  - Chrome:  HKLM\SOFTWARE\Policies\Google\Chrome\URLBlocklist\1..N
	//  - Edge:    HKLM\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist\1..N
	//  - (보조) hosts 파일 매핑
	//  관리자 권한 필요. 기존 항목 교체 후 새 목록 적용.
}



void UrlBlocker::clear()
{
	vCritical() << "UrlBlocker: clear";

	m_blockedUrls.clear();

	// TODO(P1): 적용했던 차단 전부 해제 (레지스트리 키/ hosts 항목 삭제)
}
