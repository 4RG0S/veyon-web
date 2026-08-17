/*
 * AccessBlockConfigurationPage.h - declaration of AccessBlockConfigurationPage class
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

#include "ConfigurationPage.h"

#include <QStringList>

class AccessBlockConfiguration;
class QLineEdit;
class QListWidget;

class AccessBlockConfigurationPage : public ConfigurationPage
{
	Q_OBJECT
public:
	explicit AccessBlockConfigurationPage( AccessBlockConfiguration& configuration, QWidget* parent = nullptr );

	void resetWidgets() override;
	void connectWidgetsToProperties() override;
	void applyConfiguration() override;

private:
	static QStringList listContents( const QListWidget* listWidget );
	static bool isNetworkDomain( const QString& value );
	void addDomain();
	void addBrowserUrl();
	void addApp();
	void removeSelectedItems( QListWidget* listWidget );

	AccessBlockConfiguration& m_configuration;
	QListWidget* m_domainList;
	QLineEdit* m_domainInput;
	QListWidget* m_browserUrlList;
	QLineEdit* m_browserUrlInput;
	QListWidget* m_appList;
	QLineEdit* m_appInput;
};
