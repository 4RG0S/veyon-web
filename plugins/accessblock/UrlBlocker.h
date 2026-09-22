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

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

// OWNER: P1 (web/URL blocking).
// Exact domains are blocked for all applications through Windows Filtering
// Platform. URL patterns containing a scheme or path use browser policy.
class UrlBlocker : public QObject
{
public:
	explicit UrlBlocker( QObject* parent = nullptr );
	~UrlBlocker() override;

	// Apply URL blocking for the given list, replacing any previously applied
	// set. An empty list is equivalent to clear().
	void apply( const QStringList& urls );

	// Remove all URL blocking applied by this class.
	void clear();

private:
	void applyNetworkFilters();
	void clearNetworkFilters();
	void closeNetworkEngine();
	void applyBrowserPolicies( const QStringList& urls );
	void clearBrowserPolicies();

	QStringList m_blockedDomains;
	QStringList m_blockedBrowserUrls;
	QVector<quint64> m_filterIds;
	QTimer m_refreshTimer;
	void* m_filterEngine{};

};
