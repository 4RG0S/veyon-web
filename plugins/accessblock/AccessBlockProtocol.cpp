/*
 * AccessBlockProtocol.cpp - AccessBlock v2 wire constants and CBOR codec
 *
 * Copyright (c) 2026 CHECK NODE contributors
 *
 * This file is part of Veyon - https://veyon.io
 */

#include "AccessBlockProtocol.h"

#include <QCborParserError>
#include <QCborValue>
#include <QCryptographicHash>
#include <QDateTime>


namespace
{

const auto ProtocolVersionKey = QStringLiteral("protocolVersion");
const auto SchemaVersionKey = QStringLiteral("schemaVersion");
const auto CommandIdKey = QStringLiteral("commandId");
const auto AssignmentIdKey = QStringLiteral("assignmentId");
const auto RevisionKey = QStringLiteral("revision");
const auto ExpectedPreviousHashKey = QStringLiteral("expectedPreviousHash");
const auto PayloadHashKey = QStringLiteral("payloadHash");
const auto PayloadKey = QStringLiteral("payload");
const auto RequestedAtUtcKey = QStringLiteral("requestedAtUtc");

}


Feature::Uid AccessBlockProtocol::legacyFeatureUid()
{
	return Feature::Uid{QStringLiteral("dbeee12f-b78b-42cd-be9a-aec1fbd7fb2f")};
}


Feature::Uid AccessBlockProtocol::policyV2FeatureUid()
{
	return Feature::Uid{QStringLiteral("c861b60d-d65d-4905-b52a-a33ff6904ece")};
}


QByteArray AccessBlockProtocol::encodePayload( const QCborMap& payload )
{
	return QCborValue( payload ).toCbor( QCborValue::SortKeysInMaps );
}


FeatureMessage AccessBlockProtocol::makeMessage( Command command,
												 const QCborMap& payload,
												 const Envelope* correlation )
{
	const auto payloadBytes = encodePayload( payload );
	const auto commandId = correlation && correlation->commandId.isNull() == false
			? correlation->commandId : QUuid::createUuid();
	const auto assignmentId = correlation ? correlation->assignmentId : QUuid{};
	const auto revision = correlation ? correlation->revision : 0;
	const auto expectedPreviousHash = correlation ? correlation->expectedPreviousHash : QByteArray{};

	QCborMap envelope;
	envelope.insert( ProtocolVersionKey, ProtocolVersion );
	envelope.insert( SchemaVersionKey, SchemaVersion );
	envelope.insert( CommandIdKey, commandId.toString( QUuid::WithoutBraces ) );
	envelope.insert( AssignmentIdKey, assignmentId.toString( QUuid::WithoutBraces ) );
	envelope.insert( RevisionKey, static_cast<qint64>( revision ) );
	envelope.insert( ExpectedPreviousHashKey, expectedPreviousHash );
	envelope.insert( PayloadHashKey, QCryptographicHash::hash( payloadBytes, QCryptographicHash::Sha256 ) );
	envelope.insert( PayloadKey, payloadBytes );
	envelope.insert( RequestedAtUtcKey, QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );

	return FeatureMessage{policyV2FeatureUid(), command}
			.addArgument( EnvelopeArgument,
						  QCborValue( envelope ).toCbor( QCborValue::SortKeysInMaps ) );
}


bool AccessBlockProtocol::decode( const FeatureMessage& message, Envelope* envelope, QString* error )
{
	auto fail = [error]( const QString& reason ) {
		if( error )
		{
			*error = reason;
		}
		return false;
	};

	if( envelope == nullptr )
	{
		return fail( QStringLiteral("missing output envelope") );
	}
	if( message.featureUid() != policyV2FeatureUid() || message.hasArgument( EnvelopeArgument ) == false )
	{
		return fail( QStringLiteral("not an AccessBlock v2 envelope") );
	}
	if( message.argument( EnvelopeArgument ).userType() != QMetaType::QByteArray )
	{
		return fail( QStringLiteral("envelope argument must be a byte array") );
	}

	const auto encodedEnvelope = message.argument( EnvelopeArgument ).toByteArray();
	if( encodedEnvelope.isEmpty() || encodedEnvelope.size() > MaximumEnvelopeSize )
	{
		return fail( QStringLiteral("envelope size is outside the allowed range") );
	}

	QCborParserError parserError;
	const auto value = QCborValue::fromCbor( encodedEnvelope, &parserError );
	if( parserError.error != QCborError::NoError || parserError.offset != encodedEnvelope.size() ||
		value.isMap() == false )
	{
		return fail( QStringLiteral("malformed CBOR envelope") );
	}

	const auto map = value.toMap();
	if( QCborValue( map ).toCbor( QCborValue::SortKeysInMaps ) != encodedEnvelope )
	{
		return fail( QStringLiteral("envelope is not deterministic CBOR") );
	}
	if( map.value( ProtocolVersionKey ).toInteger( -1 ) != ProtocolVersion ||
		map.value( SchemaVersionKey ).toInteger( -1 ) != SchemaVersion )
	{
		return fail( QStringLiteral("unsupported protocol or schema version") );
	}

	const auto commandIdValue = map.value( CommandIdKey );
	const auto assignmentIdValue = map.value( AssignmentIdKey );
	const auto revisionValue = map.value( RevisionKey );
	const auto payloadHashValue = map.value( PayloadHashKey );
	const auto payloadValue = map.value( PayloadKey );
	const auto requestedAtValue = map.value( RequestedAtUtcKey );
	if( commandIdValue.isString() == false || assignmentIdValue.isString() == false ||
		revisionValue.isInteger() == false || payloadHashValue.isByteArray() == false ||
		payloadValue.isByteArray() == false || requestedAtValue.isString() == false )
	{
		return fail( QStringLiteral("missing or invalid required envelope field") );
	}

	envelope->commandId = QUuid{commandIdValue.toString()};
	envelope->assignmentId = QUuid{assignmentIdValue.toString()};
	const auto revision = revisionValue.toInteger( -1 );
	if( envelope->commandId.isNull() || revision < 0 )
	{
		return fail( QStringLiteral("invalid command ID or revision") );
	}
	envelope->revision = static_cast<quint64>( revision );

	const auto expectedHashValue = map.value( ExpectedPreviousHashKey );
	if( expectedHashValue.isUndefined() == false && expectedHashValue.isNull() == false &&
		expectedHashValue.isByteArray() == false )
	{
		return fail( QStringLiteral("invalid expected previous hash") );
	}
	envelope->expectedPreviousHash = expectedHashValue.toByteArray();
	if( envelope->expectedPreviousHash.isEmpty() == false &&
		envelope->expectedPreviousHash.size() != QCryptographicHash::hashLength( QCryptographicHash::Sha256 ) )
	{
		return fail( QStringLiteral("invalid expected previous hash length") );
	}
	envelope->payloadHash = payloadHashValue.toByteArray();
	envelope->payloadBytes = payloadValue.toByteArray();
	envelope->requestedAtUtc = requestedAtValue.toString();
	const auto requestedAt = QDateTime::fromString( envelope->requestedAtUtc, Qt::ISODateWithMs );
	if( requestedAt.isValid() == false || envelope->requestedAtUtc.endsWith( QLatin1Char('Z') ) == false )
	{
		return fail( QStringLiteral("requestedAtUtc must be a valid UTC timestamp") );
	}

	if( envelope->payloadHash.size() != QCryptographicHash::hashLength( QCryptographicHash::Sha256 ) ||
		QCryptographicHash::hash( envelope->payloadBytes, QCryptographicHash::Sha256 ) != envelope->payloadHash )
	{
		return fail( QStringLiteral("payload hash mismatch") );
	}

	QCborParserError payloadParserError;
	const auto payload = QCborValue::fromCbor( envelope->payloadBytes, &payloadParserError );
	if( payloadParserError.error != QCborError::NoError ||
		payloadParserError.offset != envelope->payloadBytes.size() || payload.isMap() == false )
	{
		return fail( QStringLiteral("payload must be a CBOR map") );
	}
	if( payload.toCbor( QCborValue::SortKeysInMaps ) != envelope->payloadBytes )
	{
		return fail( QStringLiteral("payload is not deterministic CBOR") );
	}
	envelope->payload = payload.toMap();

	return true;
}
