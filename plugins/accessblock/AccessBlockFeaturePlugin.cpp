/*
 * AccessBlockFeaturePlugin.cpp - implementation of AccessBlockFeaturePlugin class
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

#include "AccessBlockFeaturePlugin.h"
#include "AccessBlockConfigurationPage.h"
#include "ComputerControlInterface.h"
#include "VeyonServerInterface.h"


AccessBlockFeaturePlugin::AccessBlockFeaturePlugin( QObject* parent ) :
	QObject( parent ),
	m_configuration( &VeyonCore::config() ),
	m_accessBlockFeature( QStringLiteral( "AccessBlock" ),
						  Feature::Flag::Mode | Feature::Flag::AllComponents,
						  AccessBlockProtocol::legacyFeatureUid(),
						  Feature::Uid(),
						  tr( "Block access" ), tr( "Unblock access" ),
						  tr( "Block access to certain URLs and applications on all computers." ) ),
	m_accessBlockV2Feature( QStringLiteral( "AccessBlockPolicyV2" ),
							Feature::Flag::Meta | Feature::Flag::AllComponents,
							AccessBlockProtocol::policyV2FeatureUid(),
							Feature::Uid(), {}, {}, {} ),
	m_features( { m_accessBlockFeature, m_accessBlockV2Feature } )
{
}



bool AccessBlockFeaturePlugin::controlFeature( Feature::Uid featureUid, Operation operation,
											   const QVariantMap& arguments,
											   const ComputerControlInterfaceList& computerControlInterfaces )
{
	if( featureUid != m_accessBlockFeature.uid() )
	{
		return false;
	}

	if( operation == Operation::Start )
	{
		// prefer explicit arguments (e.g. from veyon-cli or a dialog), otherwise
		// fall back to the configured block lists (managed by the config page)
		const auto urls = arguments.contains( argToString(Argument::BlockedUrls) )
				? arguments.value( argToString(Argument::BlockedUrls) ).toStringList()
				: m_configuration.blockedUrls();
		const auto apps = arguments.contains( argToString(Argument::BlockedApps) )
				? arguments.value( argToString(Argument::BlockedApps) ).toStringList()
				: m_configuration.blockedApps();

		sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}
						   .addArgument(Argument::BlockedUrls, urls)
						   .addArgument(Argument::BlockedApps, apps),
						   computerControlInterfaces);
		const auto query = AccessBlockProtocol::makeMessage(
				AccessBlockProtocol::Command::QueryCapabilities );
		AccessBlockProtocol::Envelope queryEnvelope;
		AccessBlockProtocol::decode( query, &queryEnvelope, nullptr );
		for( const auto& computerControlInterface : computerControlInterfaces )
		{
			computerControlInterface.data()->QObject::setProperty(
					"accessBlockV2PendingCommandId", queryEnvelope.commandId );
		}
		sendFeatureMessage( query, computerControlInterfaces );

		return true;
	}

	if( operation == Operation::Stop )
	{
		sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}
						   .addArgument(Argument::BlockedUrls, QStringList{})
						   .addArgument(Argument::BlockedApps, QStringList{}),
						   computerControlInterfaces);
		const auto query = AccessBlockProtocol::makeMessage(
				AccessBlockProtocol::Command::QueryCapabilities );
		AccessBlockProtocol::Envelope queryEnvelope;
		AccessBlockProtocol::decode( query, &queryEnvelope, nullptr );
		for( const auto& computerControlInterface : computerControlInterfaces )
		{
			computerControlInterface.data()->QObject::setProperty(
					"accessBlockV2PendingCommandId", queryEnvelope.commandId );
		}
		sendFeatureMessage( query, computerControlInterfaces );

		return true;
	}

	return false;
}



bool AccessBlockFeaturePlugin::handleFeatureMessage( VeyonServerInterface& server,
													 const MessageContext& messageContext,
													 const FeatureMessage& message )
{
	if( message.featureUid() == m_accessBlockV2Feature.uid() )
	{
		AccessBlockProtocol::Envelope envelope;
		QString decodeError;
		if( AccessBlockProtocol::decode( message, &envelope, &decodeError ) == false )
		{
			QCborMap payload;
			payload.insert( QStringLiteral("state"), QStringLiteral("Rejected") );
			payload.insert( QStringLiteral("reason"), QStringLiteral("InvalidEnvelope") );
			return server.sendFeatureMessageReply(
					messageContext,
					AccessBlockProtocol::makeMessage( AccessBlockProtocol::Command::Rejected, payload ) );
		}

		switch( message.command<AccessBlockProtocol::Command>() )
		{
		case AccessBlockProtocol::Command::QueryCapabilities:
		{
			QCborMap payload;
			payload.insert( QStringLiteral("state"), QStringLiteral("LegacyCompatibility") );
			payload.insert( QStringLiteral("machineSingletonBroker"), false );
			payload.insert( QStringLiteral("policyMutationV2"), false );
			payload.insert( QStringLiteral("browserPolicyWrittenVerified"), false );
			#ifdef Q_OS_WIN
			payload.insert( QStringLiteral("resolvedDomainNetworkBestEffort"), true );
			payload.insert( QStringLiteral("processContainment"), QStringLiteral("SnapshotWeakLatency") );
			#else
			payload.insert( QStringLiteral("resolvedDomainNetworkBestEffort"), false );
			payload.insert( QStringLiteral("processContainment"), QStringLiteral("Unsupported") );
			#endif
			return server.sendFeatureMessageReply(
					messageContext,
					AccessBlockProtocol::makeMessage(
							AccessBlockProtocol::Command::CapabilitiesResult, payload, &envelope ) );
		}
		case AccessBlockProtocol::Command::QueryStatus:
		{
			QCborMap payload;
			payload.insert( QStringLiteral("state"), QStringLiteral("LegacyUnverified") );
			payload.insert( QStringLiteral("desiredRevision"), 0 );
			payload.insert( QStringLiteral("managedAppliedHash"), QByteArray{} );
			return server.sendFeatureMessageReply(
					messageContext,
					AccessBlockProtocol::makeMessage(
							AccessBlockProtocol::Command::StatusResult, payload, &envelope ) );
		}
		default:
		{
			QCborMap payload;
			payload.insert( QStringLiteral("state"), QStringLiteral("Rejected") );
			payload.insert( QStringLiteral("reason"), QStringLiteral("BrokerNotReady") );
			payload.insert( QStringLiteral("detail"),
							QStringLiteral("V2 mutation is disabled until singleton broker cutover") );
			return server.sendFeatureMessageReply(
					messageContext,
					AccessBlockProtocol::makeMessage(
							AccessBlockProtocol::Command::Rejected, payload, &envelope ) );
		}
		}
	}

	if( message.featureUid() != m_accessBlockFeature.uid() )
	{
		return false;
	}
	if( message.command<FeatureMessage::Command>() != FeatureMessage::Command::Default ||
		message.hasArgument( Argument::BlockedUrls ) == false ||
		message.hasArgument( Argument::BlockedApps ) == false ||
		message.argument( Argument::BlockedUrls ).userType() != QMetaType::QStringList ||
		message.argument( Argument::BlockedApps ).userType() != QMetaType::QStringList )
	{
		vWarning() << "AccessBlock: rejected malformed legacy message";
		return true;
	}

	const auto blockedUrls = message.argument( Argument::BlockedUrls ).toStringList();
	const auto blockedApps = message.argument( Argument::BlockedApps ).toStringList();
	const bool active = blockedUrls.isEmpty() == false || blockedApps.isEmpty() == false;

	// default log level is Warning, so use vCritical() to make this visible
	vCritical() << "SERVER: AccessBlock" << ( active ? "active" : "inactive" )
				<< "URL rule count:" << blockedUrls.size()
				<< "application rule count:" << blockedApps.size();

	if( active )
	{
		m_urlBlocker.apply( blockedUrls );
		m_processBlocker.apply( blockedApps );
	}
	else
	{
		m_urlBlocker.clear();
		m_processBlocker.clear();
	}

	return true;
}



bool AccessBlockFeaturePlugin::handleFeatureMessage(
		ComputerControlInterface::Pointer computerControlInterface,
		const FeatureMessage& message )
{
	if( message.featureUid() != m_accessBlockV2Feature.uid() || computerControlInterface.isNull() )
	{
		return false;
	}

	AccessBlockProtocol::Envelope envelope;
	QString decodeError;
	if( AccessBlockProtocol::decode( message, &envelope, &decodeError ) == false )
	{
		computerControlInterface.data()->QObject::setProperty( "accessBlockV2LastError", decodeError );
		return true;
	}
	const auto command = message.command<AccessBlockProtocol::Command>();
	if( command != AccessBlockProtocol::Command::Accepted &&
		command != AccessBlockProtocol::Command::ApplyResult &&
		command != AccessBlockProtocol::Command::StatusResult &&
		command != AccessBlockProtocol::Command::Rejected &&
		command != AccessBlockProtocol::Command::CapabilitiesResult )
	{
		computerControlInterface.data()->QObject::setProperty(
				"accessBlockV2LastError", QStringLiteral("unexpected request command from server") );
		return true;
	}
	const auto pendingCommandId = computerControlInterface->property(
			"accessBlockV2PendingCommandId" ).toUuid();
	if( pendingCommandId.isNull() || pendingCommandId != envelope.commandId )
	{
		computerControlInterface.data()->QObject::setProperty(
				"accessBlockV2LastError", QStringLiteral("uncorrelated or stale response") );
		return true;
	}

	computerControlInterface.data()->QObject::setProperty( "accessBlockV2LastCommand",
									 static_cast<int>( command ) );
	computerControlInterface.data()->QObject::setProperty( "accessBlockV2LastCommandId", envelope.commandId );
	computerControlInterface.data()->QObject::setProperty( "accessBlockV2LastPayload",
									 QVariant::fromValue( envelope.payload.toVariantMap() ) );
	computerControlInterface.data()->QObject::setProperty( "accessBlockV2LastError", QVariant{} );
	computerControlInterface.data()->QObject::setProperty( "accessBlockV2PendingCommandId", QVariant{} );
	return true;
}



bool AccessBlockFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	Q_UNUSED(worker)
	Q_UNUSED(message)

	return false;
}



ConfigurationPage* AccessBlockFeaturePlugin::createConfigurationPage()
{
	return new AccessBlockConfigurationPage( m_configuration );
}



IMPLEMENT_CONFIG_PROXY(AccessBlockConfiguration)
