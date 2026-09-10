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

#ifndef MCUCOMPONENT_H
#define MCUCOMPONENT_H

#include <QString>
#include <QPointF>

// A stub.  Upstream's McuComponent is the on-canvas face of every
// microcontroller - `class McuComponent : public Chip, public MemData`, with
// AVRComponent and PICComponent underneath it - and it owns the firmware load,
// the eeprom, the clock frequency and the MCU serial ports.  This port carries
// no MCU simulation at all: the rest of this directory, and the avrprocessor
// and picprocessor subtrees under src/simulator/elements/processors/, were
// dropped wholesale, together with gpsim and simavr, the two emulator back ends
// they drove.  gpsim alone is 62 files that want glib, config.h and popt, none
// of which this SDK has.
//
// What is left is that ten files which WERE kept still name McuComponent, and
// every one of them reaches it only through self():
//
//   simulator.cpp:309     if (McuComponent::self() && !m_paused)  runAutoLoad()
//   simulator.cpp:371     if (McuComponent::self())               reset()
//   simulator.cpp:437,439 inside if (BaseProcessor::self())       freq()
//   circuitview.cpp:109   if (McuComponent::self())               device(), freq()
//   ws2812.cpp:85         if (McuComponent::self())               freq()
//   circuit.cpp:385       inside if (type == "SerialPort")        pos()
//   baseprocessor.cpp:120 inside hardReset(), which nothing calls
//
// plus four in src/gui/editorwidget/, the code editor and its assembler
// debuggers, which is upstream's tree restored whole:
//
//   basedebugger.cpp:83,85,86  if (McuComponent::self())  load(), device()
//   codeeditor.cpp:338         if (McuComponent::self())  load()
//   codeeditor.cpp:481         if (!McuComponent::self()) report "No Mcu in
//                              Simulator" and refuse to start the debugger
//   codeeditor.cpp:592         reset(), unguarded
//
// The editor being kept rather than stubbed is what makes this stub worth its
// length: 5157 lines of upstream codeeditor, highlighter, find/replace dialog
// and four assembler debuggers compile against this header without a single
// edit, because the only thing any of them ever asks McuComponent is whether
// one exists.  codeeditor.cpp:481 in particular is not a site that has to be
// neutralised - it is a site that is now correct.  Upstream reached the same
// error message by finding m_pSelf null whenever the circuit had no MCU in it;
// here that is always, so the editor reports "No Mcu in Simulator" and stops,
// which is the true answer.
//
// Upstream's self() returns m_pSelf, a static that only the AVRComponent and
// PICComponent constructors ever assign.  With both of those gone nothing can
// set it, so the honest value is a null pointer that is known at compile time -
// which is what this returns.  That is a statement about the program, not an
// approximation of one: it is the same value upstream's self() would hold at
// every one of those points, only arrived at without a runtime check.
//
// Which makes all of them dead or correct, each for a reason worth writing
// down because none of them is "it is guarded" alone:
//
//   The four behind `if (McuComponent::self())` fold away - GCC sees through
//   the inline self() and removes the branch.
//   simulator.cpp:437 and 439 are behind `if (BaseProcessor::self())`, and
//   BaseProcessor::m_pSelf has exactly the same property for exactly the same
//   reason: baseprocessor.cpp:28 initialises it to 0 and the only assignments
//   were in the two processor subclasses.  This branch is NOT folded - the
//   compiler cannot see through a static data member - so the calls are
//   compiled and are reachable in principle, which is why the bodies below have
//   to be safe rather than merely present.
//   circuit.cpp:385 needs an item whose type is "SerialPort", and no component
//   registers that type any more, so createItem returns null for it and the
//   enclosing `if( item )` takes the else branch.
//   baseprocessor.cpp:120 is inside hardReset(), which has no caller in this
//   tree - grep hardReset and the only two hits are its own declaration and
//   definition.
//   codeeditor.cpp:592 is the one genuinely unguarded call, in
//   CodeEditor::reset(), reachable from the debugger toolbar.  It is safe for
//   the reason given below the member list rather than for any structural one:
//   reset() does not touch this.
//
// Header-only on purpose.  Q_OBJECT here would put the class in the moc set and
// would imply there is something behind it to be a meta-object of; the whole
// point is that there is not.  MAINMODULE_EXPORT is spelled the way upstream
// spells it so this declaration reads like the one it replaces - SimulIDE.pro:75
// defines it empty, and this port's Makefile does the same.

class MAINMODULE_EXPORT McuComponent
{
    public:
        static McuComponent* self() { return nullptr; }

        // Bodies rather than declarations only: simulator.cpp:437 and 439 are
        // compiled, so undefined members there would be a link error, and a
        // link error would be the wrong diagnosis for code that cannot run.
        //
        // None of these reads or writes a member, so calling one through a null
        // this touches no memory it does not own.  The return values are chosen
        // on the same principle - a wrong answer in a dead branch is
        // recoverable, a fault is not - which is why freq() returns 1 and not 0:
        // simulator.cpp:439 divides 1000 by it.
        //
        // load() takes its QString by value the way upstream declares it, so
        // that the four call sites match without a conversion.  The parameter is
        // deliberately unnamed: naming it would invite a reader to look for the
        // place it is used.
        void    runAutoLoad()        {}
        void    reset()              {}
        void    load( QString )      {}
        double  freq()               { return 1; }
        QString device()             { return QString(); }
        QPointF pos() const          { return QPointF(); }
};

#endif
