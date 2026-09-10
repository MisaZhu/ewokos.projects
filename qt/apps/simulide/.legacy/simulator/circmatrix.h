/*
 * SimulIDE, ported to EwokOS - the circuit matrix.
 *
 * Upstream: src/simulator/circmatrix.h.  Modified nodal analysis over the
 * eNode list: one row per node that has something to solve, the admittances on
 * the diagonal and between rows, the injected currents in the coefficient
 * vector.
 *
 * The one structural change from upstream is that the disconnected-group split
 * is gone (see matrixsolver.h) - a single matrix, factorised once and solved
 * per step.  What is kept is the factoring split itself, and it matters more
 * here than upstream: m_lu persists between solves so a linear circuit, whose
 * admittances do not move, pays the O(n^3) factorisation only when a component
 * value is edited or the topology changes, and O(n^2) per step otherwise.
 *
 * Node numbering is dense from 0 and assigned in createMatrix(), after every
 * node has classified itself single or not.  A node that is single, or that has
 * no pins at all, gets -1 and no row; eNode::stampMatrix() and every
 * setOtherNode() call consult that, which is how a purely logical circuit ends
 * up with a zero-sized matrix and still runs.
 */

#ifndef SIMU_CIRCMATRIX_H
#define SIMU_CIRCMATRIX_H

#include <QList>
#include <QString>

#include "matrixsolver.h"

class eNode;
class eElement;

class CircMatrix
{
    public:
        CircMatrix();
        ~CircMatrix();

 static CircMatrix* self() { return m_pSelf; }

        // Classify the nodes, number them, size the matrix.  Must run before
        // any element stamps: initialize() drops the previous step's stamps.
        void createMatrix( QList<eNode*> &eNodeList, QList<eElement*> &elementList );

        void stampMatrix( int row, int col, double value );
        void stampCoef( int row, double value );

        // Both stamp* accumulate, so the matrix is cleared before every
        // stamping pass - which is each step, and each pass of the non-linear
        // iteration within a step.
        void zeroMatrix();

        // Factor if the admittances moved, then solve.  The solved coefficient
        // vector holds the node voltages on return.  False means singular -
        // a floating subcircuit with no path to ground.
        bool solveMatrix();

        // Write the solved voltages back onto the nodes, and give the single
        // nodes theirs straight from their driver.
        void updateNodes();

        // Something changed the topology: node numbers are stale.
        void setCircChanged() { m_circChanged = true; }
        bool circChanged() const { return m_circChanged; }

        // Whether the next solveMatrix() has to factor again rather than only
        // re-solve the LU it already holds.  Fed by the Simulator out of the
        // bool each eNode::stampMatrix() returns, which is the only place that
        // knows whether the admittances moved: a linear circuit reports no
        // change and keeps its factorisation for the life of the run, while a
        // diode re-linearising against the voltage just solved reports one on
        // every Newton pass until it converges.  A rebuild is not reported
        // here: createMatrix() sets the flag itself, because a fresh numbering
        // makes the old factorisation meaningless whatever the stamps say.
        void setAdmitChanged( bool changed ) { m_admitChanged = changed; }

        int size() const { return m_numEnodes; }
        const dVector &coefVect() const { return m_coefVect; }

        QString error() const { return m_error; }

    private:
 static CircMatrix* m_pSelf;

        void factorMatrix();

        int m_numEnodes;
        QList<eNode*> *m_eNodeList;
        QList<eElement*> m_elementList;

        dMatrix m_circMatrix;      // what the nodes stamped, un-factorised
        dVector m_coefVect;        // right-hand side, then the solution

        dMatrix m_lu;              // persistent factorisation of the above
        QVector<int> m_ipvt;
        bool m_factored;

        bool m_admitChanged;
        bool m_circChanged;

        QString m_error;
};

#endif // SIMU_CIRCMATRIX_H
