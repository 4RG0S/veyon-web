/*
 * UrlBlocker.h - blocks access to URLs on the local (slave) machine
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

#pragma once

#include <QStringList>

// OWNER: P1 (web/URL blocking).
// Implement apply()/clear() using the browser URLBlocklist policy registry
// (and/or the hosts file). See docs/team/P1-web-blocking.CLAUDE.md.
class UrlBlocker
{
public:
	// Apply URL blocking for the given list, replacing any previously applied
	// set. An empty list is equivalent to clear().
	void apply( const QStringList& urls );

	// Remove all URL blocking applied by this class.
	void clear();

private:
	QStringList m_blockedUrls;

};
