/*
 * SimulIDE, ported to EwokOS - see matrixsolver.h.
 */

#include <QtGlobal>

#include "matrixsolver.h"

// A pivot below this is a zero column.  Admittances in a circuit span a wide
// range - a closed switch is 1e3 S and an open one 1e-9 S - so the test is
// against an absolute floor rather than a relative one; a legitimately tiny
// pivot still gets chosen by the partial pivoting below.
#define PIVOTMIN 1e-30

bool MatrixSolver::factor( dMatrix &a, QVector<int> &pivot )
{
    const int n = a.size();

    pivot.clear();
    pivot.resize( n );
    for( int i=0; i<n; ++i ) pivot[i] = i;

    for( int k=0; k<n; ++k )
    {
        // Partial pivoting: the largest magnitude in the column moves onto the
        // diagonal, which is what keeps the multipliers below 1.
        double maxVal = qAbs( a[k][k] );
        int    maxRow = k;

        for( int i=k+1; i<n; ++i )
        {
            const double v = qAbs( a[i][k] );
            if( v > maxVal ) { maxVal = v; maxRow = i; }
        }
        if( maxVal < PIVOTMIN ) return false;

        if( maxRow != k )
        {
            qSwap( a[k], a[maxRow] );

            const int t        = pivot[k];
            pivot[k]           = pivot[maxRow];
            pivot[maxRow]      = t;
        }

        const double inv = 1/a[k][k];

        for( int i=k+1; i<n; ++i )
        {
            const double f = a[i][k]*inv;
            a[i][k] = f;                       // store the multiplier as L

            if( f == 0 ) continue;

            for( int j=k+1; j<n; ++j ) a[i][j] -= f*a[k][j];
        }
    }
    return true;
}

void MatrixSolver::solve( const dMatrix &a, const QVector<int> &pivot, dVector &b )
{
    const int n = a.size();
    if( n == 0 ) return;

    dVector x;
    x.resize( n );

    // Permute the right-hand side the same way the rows were permuted.
    for( int i=0; i<n; ++i ) x[i] = b[pivot[i]];

    // Forward substitution through L, whose diagonal is implicitly 1.
    for( int i=1; i<n; ++i )
    {
        double sum = x[i];
        for( int j=0; j<i; ++j ) sum -= a[i][j]*x[j];
        x[i] = sum;
    }
    // Back substitution through U.
    for( int i=n-1; i>=0; --i )
    {
        double sum = x[i];
        for( int j=i+1; j<n; ++j ) sum -= a[i][j]*x[j];

        x[i] = ( a[i][i] == 0 ) ? 0 : sum/a[i][i];
    }
    b = x;
}
