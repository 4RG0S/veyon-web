/*
 * AccessBlockProtocolTest.cpp - tests for the AccessBlock v2 wire codec
 *
 * Copyright (c) 2026 CHECK NODE contributors
 *
 * This file is part of Veyon - https://veyon.io
 */

#include <QtTest>

#include "AccessBlockProtocol.h"


class AccessBlockProtocolTest : public QObject
{
	Q_OBJECT

private slots:
	void fixedWireIdentifiers();
	void roundTripAndCorrelate();
	void rejectOversizedEnvelope();
	void rejectPayloadHashMismatch();
	void rejectWrongArgumentType();
	void rejectTrailingEnvelopeData();
};


void AccessBlockProtocolTest::fixedWireIdentifiers()
{
	QCOMPARE( AccessBlockProtocol::legacyFeatureUid(),
			  Feature::Uid{QStringLiteral("dbeee12f-b78b-42cd-be9a-aec1fbd7fb2f")} );
	QCOMPARE( AccessBlockProtocol::policyV2FeatureUid(),
			  Feature::Uid{QStringLiteral("c861b60d-d65d-4905-b52a-a33ff6904ece")} );
	QCOMPARE( static_cast<qint32>( AccessBlockProtocol::Command::QueryCapabilities ), 1 );
	QCOMPARE( static_cast<qint32>( AccessBlockProtocol::Command::CapabilitiesResult ), 104 );
}


void AccessBlockProtocolTest::roundTripAndCorrelate()
{
	QCborMap payload;
	payload.insert( QStringLiteral("probe"), true );
	const auto request = AccessBlockProtocol::makeMessage(
			AccessBlockProtocol::Command::QueryCapabilities, payload );

	AccessBlockProtocol::Envelope requestEnvelope;
	QString error;
	QVERIFY2( AccessBlockProtocol::decode( request, &requestEnvelope, &error ), qPrintable( error ) );
	QCOMPARE( requestEnvelope.payload.value( QStringLiteral("probe") ).toBool(), true );

	QCborMap responsePayload;
	responsePayload.insert( QStringLiteral("policyMutationV2"), false );
	const auto response = AccessBlockProtocol::makeMessage(
			AccessBlockProtocol::Command::CapabilitiesResult, responsePayload, &requestEnvelope );
	AccessBlockProtocol::Envelope responseEnvelope;
	QVERIFY2( AccessBlockProtocol::decode( response, &responseEnvelope, &error ), qPrintable( error ) );
	QCOMPARE( responseEnvelope.commandId, requestEnvelope.commandId );
}


void AccessBlockProtocolTest::rejectOversizedEnvelope()
{
	FeatureMessage message{AccessBlockProtocol::policyV2FeatureUid(),
						   AccessBlockProtocol::Command::QueryCapabilities};
	message.addArgument( AccessBlockProtocol::EnvelopeArgument,
					 QByteArray( AccessBlockProtocol::MaximumEnvelopeSize + 1, 'x' ) );
	AccessBlockProtocol::Envelope envelope;
	QString error;
	QVERIFY( AccessBlockProtocol::decode( message, &envelope, &error ) == false );
}


void AccessBlockProtocolTest::rejectPayloadHashMismatch()
{
	const auto validMessage = AccessBlockProtocol::makeMessage(
			AccessBlockProtocol::Command::QueryCapabilities );
	QCborParserError parserError;
	auto map = QCborValue::fromCbor(
			validMessage.argument( AccessBlockProtocol::EnvelopeArgument ).toByteArray(),
			&parserError ).toMap();
	QCOMPARE( parserError.error, QCborError::NoError );
	map.insert( QStringLiteral("payload"), QByteArray("tampered") );

	FeatureMessage tamperedMessage{AccessBlockProtocol::policyV2FeatureUid(),
								   AccessBlockProtocol::Command::QueryCapabilities};
	tamperedMessage.addArgument( AccessBlockProtocol::EnvelopeArgument,
							 QCborValue( map ).toCbor( QCborValue::SortKeysInMaps ) );
	AccessBlockProtocol::Envelope envelope;
	QString error;
	QVERIFY( AccessBlockProtocol::decode( tamperedMessage, &envelope, &error ) == false );
}


void AccessBlockProtocolTest::rejectWrongArgumentType()
{
	FeatureMessage message{AccessBlockProtocol::policyV2FeatureUid(),
						   AccessBlockProtocol::Command::QueryCapabilities};
	message.addArgument( AccessBlockProtocol::EnvelopeArgument, QStringLiteral("not bytes") );
	AccessBlockProtocol::Envelope envelope;
	QString error;
	QVERIFY( AccessBlockProtocol::decode( message, &envelope, &error ) == false );
}


void AccessBlockProtocolTest::rejectTrailingEnvelopeData()
{
	const auto validMessage = AccessBlockProtocol::makeMessage(
			AccessBlockProtocol::Command::QueryCapabilities );
	auto bytes = validMessage.argument( AccessBlockProtocol::EnvelopeArgument ).toByteArray();
	bytes.append( 'x' );
	FeatureMessage message{AccessBlockProtocol::policyV2FeatureUid(),
						   AccessBlockProtocol::Command::QueryCapabilities};
	message.addArgument( AccessBlockProtocol::EnvelopeArgument, bytes );
	AccessBlockProtocol::Envelope envelope;
	QString error;
	QVERIFY( AccessBlockProtocol::decode( message, &envelope, &error ) == false );
}


QTEST_APPLESS_MAIN(AccessBlockProtocolTest)

#include "AccessBlockProtocolTest.moc"
