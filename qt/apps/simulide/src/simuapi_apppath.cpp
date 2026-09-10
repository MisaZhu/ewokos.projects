/***************************************************************************
 *   Copyright (C) 2017 by Popov Alexey                                    *
 *   hovercraft@yandex.ru                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.  *
 *                                                                         *
 ***************************************************************************/

#include <qapplication.h>
#include <QStandardPaths>
#include "simuapi_apppath.h"

SIMUAPI_AppPath* SIMUAPI_AppPath::m_pSelf = 0l;

SIMUAPI_AppPath *SIMUAPI_AppPath::self()
{
    if (!m_pSelf) {
        m_pSelf = new SIMUAPI_AppPath();
    }
    return m_pSelf;
}

SIMUAPI_AppPath::SIMUAPI_AppPath()
               : m_ROExamFolder( qApp->applicationDirPath() )
               , m_RODataFolder( qApp->applicationDirPath() )
               , m_RWDataFolder( QStandardPaths::writableLocation( QStandardPaths::DataLocation ))
{
    // Upstream derives the read-only data/examples folders from the executable
    // location as <appdir>/../share/simulide/{data,examples}.  EwokOS installs
    // the app as /apps/simulide/ with its resources underneath, so that layout
    // does not exist and QDir::cd() would fail, leaving the folders null and
    // every data lookup (chip packages, help, examples) empty.  The Makefile
    // bakes the real locations in as SIMULIDE_DATA_DIR / SIMULIDE_EXAMPLES_DIR;
    // use them when present and fall back to the upstream derivation otherwise.
#ifdef SIMULIDE_EXAMPLES_DIR
    m_ROExamFolder = QDir( QString(SIMULIDE_EXAMPLES_DIR) );
#else
    m_ROExamFolder.cd( "../share/simulide/examples" );
#endif
#ifdef SIMULIDE_DATA_DIR
    m_RODataFolder = QDir( QString(SIMULIDE_DATA_DIR) );
#else
    m_RODataFolder.cd( "../share/simulide/data" );
#endif
    m_RWDataFolder.cd( "data" );
}

QDir SIMUAPI_AppPath::RWDataFolder() const
{
    return m_RWDataFolder;
}

void SIMUAPI_AppPath::setRWDataFolder( const QDir &RWDataFolder )
{
    m_RWDataFolder = RWDataFolder;
}

QDir SIMUAPI_AppPath::ROExamFolder() const
{
    return m_ROExamFolder;
}

void SIMUAPI_AppPath::setROExamFolder( const QDir &ROExamFolder )
{
    m_ROExamFolder = ROExamFolder;
}

QDir SIMUAPI_AppPath::RODataFolder() const
{
    return m_RODataFolder;
}

void SIMUAPI_AppPath::setRODataFolder( const QDir &RODataFolder )
{
    m_RODataFolder = RODataFolder;
}

QString SIMUAPI_AppPath::availableDataFilePath( QString fileRelPath )
{
    QString filePath = m_RWDataFolder.absoluteFilePath( fileRelPath );
    
    if( QFile::exists( filePath )) return filePath;
    
    filePath = m_RODataFolder.absoluteFilePath( fileRelPath );
    
    if( QFile::exists( filePath )) return filePath;
    
    return "";
}

QString SIMUAPI_AppPath::availableDataDirPath( QString dirRelPath )
{
    if( m_RWDataFolder.exists( dirRelPath ))
        return m_RWDataFolder.absoluteFilePath(dirRelPath);
        
    if( m_RODataFolder.exists( dirRelPath ))
        return m_RODataFolder.absoluteFilePath(dirRelPath);
        
    return "";
}



