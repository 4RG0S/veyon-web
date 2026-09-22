/*
 * AccessBlockFeaturePlugin.h - declaration of AccessBlockFeaturePlugin class
 *
 * Copyright (c) 2026 hsminu
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

#include "AccessBlockConfiguration.h"
#include "AccessBlockProtocol.h"
#include "ConfigurationPagePluginInterface.h"
#include "FeatureProviderInterface.h"
#include "ProcessBlocker.h"
#include "UrlBlocker.h"

class AccessBlockFeaturePlugin : public QObject, PluginInterface, FeatureProviderInterface, ConfigurationPagePluginInterface
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "io.veyon.Veyon.Plugins.AccessBlock")
	Q_INTERFACES(PluginInterface FeatureProviderInterface ConfigurationPagePluginInterface)
public:
	enum class Argument
	{
		BlockedUrls,
		BlockedApps,
	};
	Q_ENUM(Argument)

	explicit AccessBlockFeaturePlugin( QObject* parent = nullptr );
	~AccessBlockFeaturePlugin() override = default;

	Plugin::Uid uid() const override
	{
		return Plugin::Uid{ QStringLiteral("4f8c8f5b-abe0-4d8d-a8f6-e69bcced74dd") };
	}

	QVersionNumber version() const override
	{
		return QVersionNumber( 1, 0 );
	}

	QString name() const override
	{
		return QStringLiteral("AccessBlock");
	}

	QString description() const override
	{
		return tr( "Block access to certain URLs and applications" );
	}

	QString vendor() const override
	{
		return QStringLiteral("Veyon Community");
	}

	QString copyright() const override
	{
		return QStringLiteral("hsminu");
	}

	const FeatureList& featureList() const override
	{
		return m_features;
	}

	bool controlFeature( Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
						const ComputerControlInterfaceList& computerControlInterfaces ) override;

	bool handleFeatureMessage( VeyonServerInterface& server,
							   const MessageContext& messageContext,
							   const FeatureMessage& message ) override;
	bool handleFeatureMessage( ComputerControlInterface::Pointer computerControlInterface,
							   const FeatureMessage& message ) override;

	bool handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message ) override;

	ConfigurationPage* createConfigurationPage() override;

private:
	AccessBlockConfiguration m_configuration;

	const Feature m_accessBlockFeature;
	const Feature m_accessBlockV2Feature;
	const FeatureList m_features;

	// Legacy compatibility engines. V2 mutation remains disabled until the
	// machine-singleton broker owns these backends.
	UrlBlocker m_urlBlocker;
	ProcessBlocker m_processBlocker;

};
