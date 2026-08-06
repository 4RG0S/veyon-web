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
#include "ComputerControlInterface.h"
#include "VeyonServerInterface.h"


AccessBlockFeaturePlugin::AccessBlockFeaturePlugin( QObject* parent ) :
	QObject( parent ),
	m_accessBlockFeature( QStringLiteral( "AccessBlock" ),
						  Feature::Flag::Mode | Feature::Flag::AllComponents,
						  Feature::Uid( "dbeee12f-b78b-42cd-be9a-aec1fbd7fb2f" ),
						  Feature::Uid(),
						  tr( "Block access" ), tr( "Unblock access" ),
						  tr( "Block access to certain URLs and applications on all computers." ) ),
	m_features( { m_accessBlockFeature } )
{
}



bool AccessBlockFeaturePlugin::controlFeature( Feature::Uid featureUid, Operation operation,
											   const QVariantMap& arguments,
											   const ComputerControlInterfaceList& computerControlInterfaces )
{
	Q_UNUSED(arguments)

	if( hasFeature( featureUid ) == false )
	{
		return false;
	}

	if( operation == Operation::Start )
	{
		// dummy payload just to verify the master->server message plumbing
		sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}
						   .addArgument(Argument::BlockedUrls, QStringList{ QStringLiteral("example.com") }),
						   computerControlInterfaces);

		return true;
	}

	if( operation == Operation::Stop )
	{
		sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}
						   .addArgument(Argument::BlockedUrls, QStringList{}),
						   computerControlInterfaces);

		return true;
	}

	return false;
}



bool AccessBlockFeaturePlugin::handleFeatureMessage( VeyonServerInterface& server,
													 const MessageContext& messageContext,
													 const FeatureMessage& message )
{
	Q_UNUSED(server)
	Q_UNUSED(messageContext)

	if( message.featureUid() != m_accessBlockFeature.uid() )
	{
		return false;
	}

	const auto blockedUrls = message.argument( Argument::BlockedUrls ).toStringList();
	const bool active = blockedUrls.isEmpty() == false;

	// default log level is Warning, so use vCritical() to make this visible
	vCritical() << "SERVER: AccessBlock" << ( active ? "active" : "inactive" )
				<< "blocked URLs:" << blockedUrls;

	// TODO: 실제 차단 로직

	return true;
}



bool AccessBlockFeaturePlugin::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message )
{
	Q_UNUSED(worker)
	Q_UNUSED(message)

	return false;
}
