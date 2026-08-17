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
	m_urlList( new QListWidget( this ) ),
	m_urlInput( new QLineEdit( this ) ),
	m_appList( new QListWidget( this ) ),
	m_appInput( new QLineEdit( this ) )
{
	auto pageLayout = new QVBoxLayout( this );

	auto urlGroup = new QGroupBox( tr( "Blocked URLs" ), this );
	auto urlLayout = new QVBoxLayout( urlGroup );
	urlLayout->addWidget( new QLabel( tr( "Enter URL patterns or domains, for example example.com." ), urlGroup ) );
	urlLayout->addWidget( m_urlList );
	auto urlInputLayout = new QHBoxLayout;
	m_urlInput->setPlaceholderText( tr( "URL or domain" ) );
	auto addUrlButton = new QPushButton( tr( "Add" ), urlGroup );
	auto removeUrlButton = new QPushButton( tr( "Remove selected" ), urlGroup );
	urlInputLayout->addWidget( m_urlInput );
	urlInputLayout->addWidget( addUrlButton );
	urlInputLayout->addWidget( removeUrlButton );
	urlLayout->addLayout( urlInputLayout );
	pageLayout->addWidget( urlGroup );

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

	m_urlList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_urlList->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );
	m_appList->setSelectionMode( QAbstractItemView::ExtendedSelection );
	m_appList->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed );

	connect( addUrlButton, &QPushButton::clicked, this, &AccessBlockConfigurationPage::addUrl );
	connect( m_urlInput, &QLineEdit::returnPressed, this, &AccessBlockConfigurationPage::addUrl );
	connect( removeUrlButton, &QPushButton::clicked, this, [this]() { removeSelectedItems( m_urlList ); } );
	connect( addAppButton, &QPushButton::clicked, this, &AccessBlockConfigurationPage::addApp );
	connect( m_appInput, &QLineEdit::returnPressed, this, &AccessBlockConfigurationPage::addApp );
	connect( removeAppButton, &QPushButton::clicked, this, [this]() { removeSelectedItems( m_appList ); } );
	connect( m_urlList, &QListWidget::itemChanged, this, &AccessBlockConfigurationPage::applyConfiguration );
	connect( m_appList, &QListWidget::itemChanged, this, &AccessBlockConfigurationPage::applyConfiguration );
}



void AccessBlockConfigurationPage::resetWidgets()
{
	// block signals while populating so loading the page does not mark the
	// configuration dirty / enable Apply before the user changes anything
	const QSignalBlocker urlBlocker( m_urlList );
	const QSignalBlocker appBlocker( m_appList );

	m_urlList->clear();
	m_urlList->addItems( m_configuration.blockedUrls() );
	m_appList->clear();
	m_appList->addItems( m_configuration.blockedApps() );

	for( int row = 0; row < m_urlList->count(); ++row )
	{
		m_urlList->item( row )->setFlags( m_urlList->item( row )->flags() | Qt::ItemIsEditable );
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
	m_configuration.setBlockedUrls( listContents( m_urlList ) );
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



void AccessBlockConfigurationPage::addUrl()
{
	const auto url = m_urlInput->text().trimmed();
	if( url.isEmpty() == false && listContents( m_urlList ).contains( url, Qt::CaseInsensitive ) == false )
	{
		auto item = new QListWidgetItem( url, m_urlList );
		item->setFlags( item->flags() | Qt::ItemIsEditable );
		m_urlInput->clear();
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
