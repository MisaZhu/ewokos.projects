/***************************************************************************
 *   QScriptEngine shim for EwokOS SimulIDE port.                          *
 *                                                                         *
 *   Qt5Script is not available in the EwokOS Qt build.  This shim provides *
 *   just enough of the QScriptEngine/QScriptValue API for eFunction's      *
 *   Boolean expression evaluation: variables (i0, i1, o0, vi0, vo0, ...),  *
 *   operators (! && || == != < > <= >= + - * /), parentheses, and the      *
 *   literals true/false/0/1.                                               *
 *                                                                         *
 *   The evaluator is a simple recursive-descent parser over a QString.     *
 *   It is not a full JavaScript engine - it covers exactly what eFunction  *
 *   needs and nothing more.                                                *
 ***************************************************************************/

#ifndef QSCRIPTENGINE_SHIM_H
#define QSCRIPTENGINE_SHIM_H

#include <QString>
#include <QHash>
#include <QVariant>

class QScriptValue
{
    public:
        QScriptValue() : m_type(Invalid) {}
        QScriptValue( bool b ) : m_type(Boolean), m_bool(b) {}
        QScriptValue( int i ) : m_type(Number), m_num(i) {}
        QScriptValue( double d ) : m_type(Number), m_num(d) {}
        QScriptValue( const QString &s ) : m_type(String), m_str(s) {}

        bool isBool() const { return m_type == Boolean; }
        bool isNumber() const { return m_type == Number; }
        bool isString() const { return m_type == String; }
        bool isValid() const { return m_type != Invalid; }

        bool toBool() const
        {
            switch( m_type )
            {
                case Boolean: return m_bool;
                case Number:  return m_num != 0;
                case String:  return !m_str.isEmpty() && m_str != "0" && m_str.toLower() != "false";
                default:      return false;
            }
        }

        double toNumber() const
        {
            switch( m_type )
            {
                case Boolean: return m_bool ? 1 : 0;
                case Number:  return m_num;
                case String:  return m_str.toDouble();
                default:      return 0;
            }
        }

        QString toString() const
        {
            switch( m_type )
            {
                case Boolean: return m_bool ? "true" : "false";
                case Number:  return QString::number( m_num );
                case String:  return m_str;
                default:      return QString();
            }
        }

        QVariant toVariant() const
        {
            switch( m_type )
            {
                case Boolean: return QVariant( m_bool );
                case Number:  return QVariant( m_num );
                case String:  return QVariant( m_str );
                default:      return QVariant();
            }
        }

        // For globalObject().setProperty()
        void setProperty( const QString &name, const QScriptValue &value )
        {
            m_props[name] = value;
        }

        QScriptValue property( const QString &name ) const
        {
            return m_props.value( name );
        }

    private:
        enum Type { Invalid, Boolean, Number, String };
        Type m_type;
        bool m_bool;
        double m_num;
        QString m_str;
        QHash<QString, QScriptValue> m_props;
};

class QScriptEngine
{
    public:
        QScriptEngine() {}
        ~QScriptEngine() {}

        QScriptValue globalObject() { return m_global; }

        QScriptValue evaluate( const QString &program )
        {
            m_pos = 0;
            m_src = program;
            skipWs();
            QScriptValue result = parseExpr();
            return result;
        }

    private:
        QScriptValue m_global;
        QString m_src;
        int m_pos;

        void skipWs()
        {
            while( m_pos < m_src.length() && m_src[m_pos].isSpace() )
                m_pos++;
        }

        bool match( const QString &tok )
        {
            skipWs();
            if( m_src.mid( m_pos, tok.length() ) == tok )
            {
                m_pos += tok.length();
                return true;
            }
            return false;
        }

        bool peek( const QString &tok )
        {
            skipWs();
            return m_src.mid( m_pos, tok.length() ) == tok;
        }

        QScriptValue parseExpr()
        {
            return parseOr();
        }

        QScriptValue parseOr()
        {
            QScriptValue left = parseAnd();
            while( match( "||" ) )
            {
                QScriptValue right = parseAnd();
                left = QScriptValue( left.toBool() || right.toBool() );
            }
            return left;
        }

        QScriptValue parseAnd()
        {
            QScriptValue left = parseEquality();
            while( match( "&&" ) )
            {
                QScriptValue right = parseEquality();
                left = QScriptValue( left.toBool() && right.toBool() );
            }
            return left;
        }

        QScriptValue parseEquality()
        {
            QScriptValue left = parseRelational();
            while( true )
            {
                if( match( "==" ) || match( "===" ) )
                {
                    QScriptValue right = parseRelational();
                    left = QScriptValue( left.toNumber() == right.toNumber() );
                }
                else if( match( "!=" ) || match( "!==" ) )
                {
                    QScriptValue right = parseRelational();
                    left = QScriptValue( left.toNumber() != right.toNumber() );
                }
                else break;
            }
            return left;
        }

        QScriptValue parseRelational()
        {
            QScriptValue left = parseAdditive();
            while( true )
            {
                if( match( "<=" ) )
                {
                    QScriptValue right = parseAdditive();
                    left = QScriptValue( left.toNumber() <= right.toNumber() );
                }
                else if( match( ">=" ) )
                {
                    QScriptValue right = parseAdditive();
                    left = QScriptValue( left.toNumber() >= right.toNumber() );
                }
                else if( match( "<" ) )
                {
                    QScriptValue right = parseAdditive();
                    left = QScriptValue( left.toNumber() < right.toNumber() );
                }
                else if( match( ">" ) )
                {
                    QScriptValue right = parseAdditive();
                    left = QScriptValue( left.toNumber() > right.toNumber() );
                }
                else break;
            }
            return left;
        }

        QScriptValue parseAdditive()
        {
            QScriptValue left = parseMultiplicative();
            while( true )
            {
                if( match( "+" ) )
                {
                    QScriptValue right = parseMultiplicative();
                    left = QScriptValue( left.toNumber() + right.toNumber() );
                }
                else if( match( "-" ) )
                {
                    QScriptValue right = parseMultiplicative();
                    left = QScriptValue( left.toNumber() - right.toNumber() );
                }
                else break;
            }
            return left;
        }

        QScriptValue parseMultiplicative()
        {
            QScriptValue left = parseUnary();
            while( true )
            {
                if( match( "*" ) )
                {
                    QScriptValue right = parseUnary();
                    left = QScriptValue( left.toNumber() * right.toNumber() );
                }
                else if( match( "/" ) )
                {
                    QScriptValue right = parseUnary();
                    double d = right.toNumber();
                    left = QScriptValue( d != 0 ? left.toNumber() / d : 0 );
                }
                else if( match( "%" ) )
                {
                    QScriptValue right = parseUnary();
                    double d = right.toNumber();
                    left = QScriptValue( d != 0 ? (int)left.toNumber() % (int)d : 0 );
                }
                else break;
            }
            return left;
        }

        QScriptValue parseUnary()
        {
            skipWs();
            if( match( "!" ) )
            {
                QScriptValue val = parseUnary();
                return QScriptValue( !val.toBool() );
            }
            if( match( "-" ) )
            {
                QScriptValue val = parseUnary();
                return QScriptValue( -val.toNumber() );
            }
            if( match( "+" ) )
            {
                return parseUnary();
            }
            return parsePrimary();
        }

        QScriptValue parsePrimary()
        {
            skipWs();
            if( m_pos >= m_src.length() )
                return QScriptValue( false );

            // Parenthesized expression
            if( match( "(" ) )
            {
                QScriptValue val = parseExpr();
                match( ")" );
                return val;
            }

            // Literals
            if( match( "true" ) )  return QScriptValue( true );
            if( match( "false" ) ) return QScriptValue( false );

            // Number
            QChar c = m_src[m_pos];
            if( c.isDigit() || c == '.' )
            {
                int start = m_pos;
                while( m_pos < m_src.length() && ( m_src[m_pos].isDigit() || m_src[m_pos] == '.' ) )
                    m_pos++;
                return QScriptValue( m_src.mid( start, m_pos - start ).toDouble() );
            }

            // Identifier (variable name)
            if( c.isLetter() || c == '_' )
            {
                int start = m_pos;
                while( m_pos < m_src.length() && ( m_src[m_pos].isLetterOrNumber() || m_src[m_pos] == '_' ) )
                    m_pos++;
                QString name = m_src.mid( start, m_pos - start );
                return m_global.property( name );
            }

            // Unknown - skip and return false
            m_pos++;
            return QScriptValue( false );
        }
};

#endif // QSCRIPTENGINE_SHIM_H
