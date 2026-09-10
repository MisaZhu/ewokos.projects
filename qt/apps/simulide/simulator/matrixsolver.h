/*
 * SimulIDE, ported to EwokOS - the linear solver.
 *
 * Upstream: src/simulator/matrixsolver.h, whose factorMatrix()/luSolve() are
 * private members of CircMatrix.  Split out here because the two halves have
 * different call frequencies and keeping them apart is what makes that visible:
 * factorisation is O(n^3) and runs only when an admittance changes, while the
 * solve is O(n^2) and runs every step.
 *
 * LU with partial pivoting, Doolittle form: after factor() the lower triangle
 * (excluding a unit diagonal) holds L and the upper triangle holds U, in the
 * same storage, with the row permutation recorded separately so the factored
 * matrix survives as many right-hand sides as are thrown at it.
 *
 * Upstream splits the circuit into electrically disconnected groups and
 * factorises each one on its own, which is a real saving on a sheet with
 * several independent circuits.  This port factorises the whole matrix; the
 * group walk is the first thing to put back if a large sheet gets slow.
 */

#ifndef SIMU_MATRIXSOLVER_H
#define SIMU_MATRIXSOLVER_H

#include <QVector>

typedef QVector<double>           dVector;
typedef QVector<QVector<double> > dMatrix;

class MatrixSolver
{
    public:
        // Decompose a in place, writing the row permutation into pivot.
        // Returns false if a column has no usable pivot, which is the
        // "floating subcircuit, no ground" case the caller has to report.
        static bool factor( dMatrix &a, QVector<int> &pivot );

        // Solve a x = b against the factorisation in a.  b is overwritten
        // with x.  a and pivot are untouched, so the same factorisation serves
        // every step until an admittance changes.
        static void solve( const dMatrix &a, const QVector<int> &pivot, dVector &b );
};

#endif // SIMU_MATRIXSOLVER_H
