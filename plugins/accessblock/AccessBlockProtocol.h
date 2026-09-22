/*
 * AccessBlockProtocol.h - AccessBlock v2 wire constants and CBOR codec
 *
 * Copyright (c) 2026 CHECK NODE contributors
 *
 * This file is part of Veyon - https://veyon.io
 */

#pragma once

#include <QCborMap>
#include <QString>

#include "FeatureMessage.h"

class AccessBlockProtocol
{
public:
	enum class Command : qint32
	{
		QueryCapabilities = 1,
		SetDesiredPolicy = 2,
		ClearAssignment = 3,
		RenewLease = 4,
		QueryStatus = 5,
		Reconcile = 6,
		ForceClearAll = 7,
		Accepted = 100,
		ApplyResult = 101,
		StatusResult = 102,
		Rejected = 103,
		CapabilitiesResult = 104,
	};

	struct Envelope
	{
		QUuid commandId;
		QUuid assignmentId;
		quint64 revision{};
		QByteArray expectedPreviousHash;
		QByteArray payloadHash;
		QByteArray payloadBytes;
		QCborMap payload;
		QString requestedAtUtc;
	};

	static constexpr int EnvelopeArgument = 0;
	static constexpr qsizetype MaximumEnvelopeSize = 256 * 1024;
	static constexpr quint16 ProtocolVersion = 2;
	static constexpr quint16 SchemaVersion = 1;

	static Feature::Uid legacyFeatureUid();
	static Feature::Uid policyV2FeatureUid();

	static FeatureMessage makeMessage( Command command,
								   const QCborMap& payload = {},
								   const Envelope* correlation = nullptr );

	static bool decode( const FeatureMessage& message, Envelope* envelope, QString* error );

private:
	static QByteArray encodePayload( const QCborMap& payload );
};
