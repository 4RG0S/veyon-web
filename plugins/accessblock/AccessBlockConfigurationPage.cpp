/*
 * AccessBlockConfigurationPage.cpp - implementation of AccessBlockConfigurationPage class
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

#include "AccessBlockConfiguration.h"
#include "AccessBlockConfigurationPage.h"

#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>


AccessBlockConfigurationPage::AccessBlockConfigurationPage( AccessBlockConfiguration& configuration, QWidget* parent ) :
	ConfigurationPage( parent ),
	m_configuration( configuration ),
	m_domainList( new QListWidget( this ) ),
	m_domainInput( new QLineEdit( this ) ),
	m_browserUrlList( new QListWidget( this ) ),
	m_browserUrlInput( new QLineEdit( this ) ),
	m_appList( new QListWidget( this ) ),
	m_appInput( new QLineEdit( this ) )
{
	auto pageLayout = new QVBoxLayout( this );

	auto domainGroup = new QGroupBox( tr( "Blocked domains (all applications)" ), this );
	auto domainLayout = new QVBoxLayout( domainGroup );
	domainLayout->addWidget( new QLabel( tr( "Enter an exact domain such as example.com. Windows network filtering blocks its resolved IP addresses." ), domainGroup ) );
	domainLayout->addWidget( m_domainList );
	auto domainInputLayout = new QHBoxLayout;
	m_domainInput->setPlaceholderText( tr( "Domain name" ) );
	auto addDomainButton = new QPushButton( tr( "Add" ), domainGroup );
	auto removeDomainButton = new QPushButton( tr( "Remove selected" ), domainGroup );
	domainInputLayout->addWidget( m_domainInput );
	domainInputLayout->addWidget( addDomainButton );
	domainInputLayout->addWidget( removeDomainButton );
	domainLayout->addLayout( domainInputLayout );
	pageLayout->addWidget( domainGroup );

	auto browserUrlGroup = new QGroupBox( tr( "Blocked browser URL patterns" ), this );
	auto browserUrlLayout = new QVBoxLayout( browserUrlGroup );
	browserUrlLayout->addWidget( new QLabel( tr( "Enter a URL with a scheme or path, such as https://example.com/private. This uses Chrome and Edge policies." ), browserUrlGroup ) );
	browserUrlLayout->addWidget( m_browserUrlList );
	auto browserUrlInputLayout = new QHBoxLayout;
	m_browserUrlInput->setPlaceholderText( tr( "URL pattern" ) );
	auto addBrowserUrlButton = new QPushButton( tr( "Add" ), browserUrlGroup );
	auto removeBrowserUrlButton = new QPushButton( tr( "Remove selected" ), browserUrlGroup );
	browserUrlInputLayout->addWidget( m_browserUrlInput );
	browserUrlInputLayout->addWidget( addBrowserUrlButton );
	browserUrlInputLayout->addWidget( removeBrowserUrlButton );
	browserUrlLayout->addLayout( browserUrlInputLayout );
	pageLayout->addWidget( browserUrlGroup );

	auto appGroup = new QGroupBox( tr( "Blocked applications" ), this );
	auto appLayout = new QVBoxLayout( appGroup );
	appLayout->addWidget( new QLabel( tr( "Enter an executable file name, for example notepad.exe." ), appGroup ) );
	appLayout->addWidget( m_appList );
	auto appInputLayout = new QHBoxLayout;
	m_appInput->setPlaceholderText( tr( "Executable file name" ) );
	auto addAppButton = new QPushButton( tr( "Add" ), appGroup );
	auto removeAppButton = new QPushButton( tr( "Remove selected" ), appGroup );
	appInputLayout->addWidget( m_appInput );
	appInputLayout->addWidget( addAppButton );
	appInputLayout->addWidget( removeAppButton );
	appLayout->addLayout( appInputLayout );
	pageLayout->addWidget( appGroup );

	m_domainList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_domainList->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
	m_browserUrlList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_browserUrlList->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
	m_appList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_appList->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );

	connect( addDomainButton, &QPushButton::clicked, this, &AccessBlockConfigurationPage::addDomain );
	connect( m_domainInput, &QLineEdit::returnPressed, this, &AccessBlockConfigurationPage::addDomain );
	connect( removeDomainButton, &QPushButton::clicked, this, [this]() { removeSelectedItems( m_domainList ); } );
	connect( addBrowserUrlButton, &QPushButton::clicked, this, &AccessBlockConfigurationPage::addBrowserUrl );
	connect( m_browserUrlInput, &QLineEdit::returnPressed, this, &AccessBlockConfigurationPage::addBrowserUrl );
	connect( removeBrowserUrlButton, &QPushButton::clicked, this, [this]() { removeSelectedItems( m_browserUrlList ); } );
	connect( addAppButton, &QPushButton::clicked, this, &AccessBlockConfigurationPage::addApp );
	connect( m_appInput, &QLineEdit::returnPressed, this, &AccessBlockConfigurationPage::addApp );
	connect( removeAppButton, &QPushButton::clicked, this, [this]() { removeSelectedItems( m_appList ); } );
	connect( m_domainList, &QListWidget::itemChanged, this, &AccessBlockConfigurationPage::applyConfiguration );
	connect( m_browserUrlList, &QListWidget::itemChanged, this, &AccessBlockConfigurationPage::applyConfiguration );
	connect( m_appList, &QListWidget::itemChanged, this, &AccessBlockConfigurationPage::applyConfiguration );
}



void AccessBlockConfigurationPage::resetWidgets()
{
	// block signals while populating so loading the page does not mark the
	// configuration dirty / enable Apply before the user changes anything
	const QSignalBlocker domainBlocker( m_domainList );
	const QSignalBlocker browserUrlBlocker( m_browserUrlList );
	const QSignalBlocker appBlocker( m_appList );

	m_domainList->clear();
	m_browserUrlList->clear();
	for( const auto& value : m_configuration.blockedUrls() )
	{
		( isNetworkDomain( value ) ? m_domainList : m_browserUrlList )->addItem( value );
	}
	m_appList->clear();
	m_appList->addItems( m_configuration.blockedApps() );

	for( auto list : { m_domainList, m_browserUrlList } )
	{
		for( int row = 0; row < list->count(); ++row )
		{
			list->item( row )->setFlags( list->item( row )->flags() | Qt::ItemIsEditable );
		}
	}
	for( int row = 0; row < m_appList->count(); ++row )
	{
		m_appList->item( row )->setFlags( m_appList->item( row )->flags() | Qt::ItemIsEditable );
	}
}



void AccessBlockConfigurationPage::connectWidgetsToProperties()
{
}



void AccessBlockConfigurationPage::applyConfiguration()
{
	auto blockedUrls = listContents( m_domainList );
	blockedUrls.append( listContents( m_browserUrlList ) );
	m_configuration.setBlockedUrls( blockedUrls );
	m_configuration.setBlockedApps( listContents( m_appList ) );
}



QStringList AccessBlockConfigurationPage::listContents( const QListWidget* listWidget )
{
	QStringList contents;
	for( int row = 0; row < listWidget->count(); ++row )
	{
		const auto value = listWidget->item( row )->text().trimmed();
		if( value.isEmpty() == false && contents.contains( value, Qt::CaseInsensitive ) == false )
		{
			contents.append( value );
		}
	}
	return contents;
}



bool AccessBlockConfigurationPage::isNetworkDomain( const QString& value )
{
	return value.contains( QStringLiteral( "://" ) ) == false &&
		value.contains( QLatin1Char( '/' ) ) == false;
}


void AccessBlockConfigurationPage::addDomain()
{
	const auto domain = m_domainInput->text().trimmed();
	if( domain.isEmpty() == false && isNetworkDomain( domain ) &&
		listContents( m_domainList ).contains( domain, Qt::CaseInsensitive ) == false )
	{
		auto item = new QListWidgetItem( domain, m_domainList );
		item->setFlags( item->flags() | Qt::ItemIsEditable );
		m_domainInput->clear();
		applyConfiguration();
	}
}


void AccessBlockConfigurationPage::addBrowserUrl()
{
	const auto url = m_browserUrlInput->text().trimmed();
	if( url.isEmpty() == false && isNetworkDomain( url ) == false &&
		listContents( m_browserUrlList ).contains( url, Qt::CaseInsensitive ) == false )
	{
		auto item = new QListWidgetItem( url, m_browserUrlList );
		item->setFlags( item->flags() | Qt::ItemIsEditable );
		m_browserUrlInput->clear();
		applyConfiguration();
	}
}



void AccessBlockConfigurationPage::addApp()
{
	const auto app = m_appInput->text().trimmed();
	if( app.isEmpty() == false && listContents( m_appList ).contains( app, Qt::CaseInsensitive ) == false )
	{
		auto item = new QListWidgetItem( app, m_appList );
		item->setFlags( item->flags() | Qt::ItemIsEditable );
		m_appInput->clear();
		applyConfiguration();
	}
}



void AccessBlockConfigurationPage::removeSelectedItems( QListWidget* listWidget )
{
	const auto selectedItems = listWidget->selectedItems();
	for( auto item : selectedItems )
	{
		delete listWidget->takeItem( listWidget->row( item ) );
	}

	if( selectedItems.isEmpty() == false )
	{
		applyConfiguration();
	}
}
