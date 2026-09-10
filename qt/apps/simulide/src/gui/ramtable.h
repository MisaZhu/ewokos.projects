/***************************************************************************
 *   Copyright (C) 2012 by santiago González                               *
 *   santigoro@gmail.com                                                   *
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

#ifndef RAMTABLE_H
#define RAMTABLE_H

#include <QtWidgets>

#include "baseprocessor.h"

// Order matters here and upstream has it the other way round.  baseprocessor.h
// declares QString, QObject, QHash, QVector and QStringList members but includes
// no Qt header of its own - it has `//#include <QtGui>` commented out at line 23 -
// so it relies on whoever includes it having pulled Qt in first.  Nine files in
// this tree include it and eight of them do, through their own header.  This one
// did not, and the result was not an error about a missing include: it was
// thirty-odd errors inside baseprocessor.h, `expected class-name before '{'` on
// the QObject base, `'Q_OBJECT' does not name a type`, `'signals' does not name a
// type`, and a spurious `getRamValue(int) cannot be overloaded with
// getRamValue(int)` because the two overloads differ only in a QString that had
// failed to be a type.  A header should include what it uses; putting the Qt
// include first makes this one self-sufficient and costs nothing.

class BaseProcessor;
class BaseDebugger;

class RamTable : public QTableWidget
{
    Q_OBJECT
  public:
    RamTable(BaseProcessor *processor);
    ~RamTable();

    void updateValues();

    void setItemValue(int col, QString value);
    void setItemValue(int col, int32_t value);
    void setItemValue(int col, float value);

    void setDebugger(BaseDebugger *deb);
    void remDebugger(BaseDebugger *deb);

  public slots:

    void clearSelected();
    void clearTable();
    void loadVarSet();
    void saveVarSet();
    void loadVariables();

  private slots:
    void addToWatch(QTableWidgetItem *);
    void slotContextMenu(const QPoint &);

  private:
    BaseProcessor *m_processor;
    BaseDebugger *m_debugger;

    QHash<int, QString> watchList;

    bool m_loadingVars;

    int m_numRegs;
    int m_currentRow;
};

#endif // RAMTABLE_H
