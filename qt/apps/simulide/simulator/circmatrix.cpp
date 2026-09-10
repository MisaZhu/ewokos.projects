/*
 * SimulIDE, ported to EwokOS - see circmatrix.h.
 */

#include "circmatrix.h"

#include "e-node.h"
#include "e-pin.h"

CircMatrix* CircMatrix::m_pSelf = 0l;

CircMatrix::CircMatrix()
            : m_numEnodes( 0 )
            , m_eNodeList( 0l )
            , m_factored( false )
            , m_admitChanged( true )
            , m_circChanged( true )
{
    m_pSelf = this;
}

CircMatrix::~CircMatrix()
{
    if( m_pSelf == this ) m_pSelf = 0l;
}

void CircMatrix::createMatrix( QList<eNode*> &eNodeList, QList<eElement*> &elementList )
{
    m_eNodeList   = &eNodeList;
    m_elementList = elementList;

    // Classify before numbering: whether a node is single depends on what is
    // attached to it, and initialize() is also what drops the previous step's
    // stamps so the pass below starts from nothing.
    for( eNode *node : eNodeList ) node->initialize();

    int num = 0;
    for( eNode *node : eNodeList )
    {
        // Two reasons a node gets no equation.  It is single, so its driver
        // defines it outright.  Or it has one pin, which means it is a
        // dangling lead: an open terminal passes no current, so there is
        // nothing to solve for, and the off-diagonal stamp its element makes
        // towards it is dropped - which is the correct answer.
        if( node->isSingle() || node->numPins() < 2 )
        {
            node->setNodeNumber( -1 );
            continue;
        }
        node->setNodeNumber( num++ );
    }
    m_numEnodes = num;

    m_circMatrix.clear();
    m_circMatrix.resize( num );
    for( int i=0; i<num; ++i )
    {
        m_circMatrix[i].clear();
        m_circMatrix[i].resize( num );
        for( int j=0; j<num; ++j ) m_circMatrix[i][j] = 0;
    }
    m_coefVect.clear();
    m_coefVect.resize( num );
    for( int i=0; i<num; ++i ) m_coefVect[i] = 0;

    m_lu = m_circMatrix;
    m_ipvt.clear();

    m_factored     = false;
    m_admitChanged = true;      // fresh numbering: the old LU means nothing
    m_circChanged  = false;
    m_error.clear();
}

void CircMatrix::zeroMatrix()
{
    for( int i=0; i<m_numEnodes; ++i )
    {
        for( int j=0; j<m_numEnodes; ++j ) m_circMatrix[i][j] = 0;
        m_coefVect[i] = 0;
    }
}

void CircMatrix::stampMatrix( int row, int col, double value )
{
    if( row < 0 || row >= m_numEnodes ) return;
    if( col < 0 || col >= m_numEnodes ) return;

    m_circMatrix[row][col] += value;
}

void CircMatrix::stampCoef( int row, double value )
{
    if( row < 0 || row >= m_numEnodes ) return;

    m_coefVect[row] += value;
}

void CircMatrix::factorMatrix()
{
    m_lu = m_circMatrix;

    m_factored = MatrixSolver::factor( m_lu, m_ipvt );

    if( !m_factored )
        m_error = "Circuit is not solvable: a part of it has no path to Ground.";

    m_admitChanged = false;
}

bool CircMatrix::solveMatrix()
{
    if( m_numEnodes == 0 )
    {
        m_error.clear();
        return true;            // a purely logical circuit: nothing to solve
    }
    if( m_admitChanged || !m_factored ) factorMatrix();

    if( !m_factored ) return false;

    MatrixSolver::solve( m_lu, m_ipvt, m_coefVect );

    return true;
}

void CircMatrix::updateNodes()
{
    if( !m_eNodeList ) return;

    for( eNode *node : *m_eNodeList )
    {
        if( node->isSingle() )
        {
            // Never in the matrix: its driver defines it outright.
            node->solveSingle();
            continue;
        }
        const int num = node->getNodeNumber();
        if( num < 0 ) continue;

        node->setVolt( m_coefVect[num] );
    }
}
