/***************************************************************************
 *   Qt5Multimedia shim for EwokOS SimulIDE port.                          *
 *                                                                         *
 *   Qt5Multimedia is not available in the EwokOS Qt build.  This shim      *
 *   provides just enough of the QAudioOutput/QAudioFormat/QAudioDeviceInfo *
 *   API for AudioOut to compile; all audio operations are no-ops.          *
 ***************************************************************************/

#ifndef QAUDIOOUTPUT_SHIM_H
#define QAUDIOOUTPUT_SHIM_H

#include <QString>
#include <QIODevice>
#include <QStringList>
#include <QList>

namespace QAudio
{
    enum State
    {
        ActiveState,
        SuspendedState,
        StoppedState,
        IdleState,
        InterruptedState
    };

    enum Error
    {
        NoError,
        OpenError,
        IOError,
        UnderrunError,
        FatalError
    };

    enum Mode
    {
        PullMode,
        PushMode
    };
}

class QAudioFormat
{
    public:
        enum ByteOrder { LittleEndian, BigEndian };
        enum SampleType { SignedInt, UnSignedInt, Float };

        QAudioFormat() : m_sampleRate(0), m_channels(0), m_sampleSize(0),
                         m_byteOrder(LittleEndian), m_sampleType(SignedInt) {}

        void setSampleRate( int rate ) { m_sampleRate = rate; }
        int sampleRate() const { return m_sampleRate; }

        void setChannelCount( int ch ) { m_channels = ch; }
        int channelCount() const { return m_channels; }

        void setSampleSize( int size ) { m_sampleSize = size; }
        int sampleSize() const { return m_sampleSize; }

        void setCodec( const QString &codec ) { m_codec = codec; }
        QString codec() const { return m_codec; }

        void setByteOrder( ByteOrder order ) { m_byteOrder = order; }
        ByteOrder byteOrder() const { return m_byteOrder; }

        void setSampleType( SampleType type ) { m_sampleType = type; }
        SampleType sampleType() const { return m_sampleType; }

        bool isValid() const { return m_sampleRate > 0 && m_channels > 0; }

    private:
        int m_sampleRate;
        int m_channels;
        int m_sampleSize;
        QString m_codec;
        ByteOrder m_byteOrder;
        SampleType m_sampleType;
};

class QAudioDeviceInfo
{
    public:
        QAudioDeviceInfo() {}

        QString deviceName() const { return QString( "EwokOS Audio (stub)" ); }
        bool isNull() const { return false; }

        static QAudioDeviceInfo defaultOutputDevice() { return QAudioDeviceInfo(); }

        bool isFormatSupported( const QAudioFormat &format ) const { (void)format; return true; }

        QAudioFormat nearestFormat( const QAudioFormat &format ) const { return format; }

        QStringList supportedCodecs() const { return QStringList() << "audio/pcm"; }
        QList<int> supportedSampleRates() const { return QList<int>() << 44100; }
        QList<int> supportedChannelCounts() const { return QList<int>() << 1 << 2; }
        QList<int> supportedSampleSizes() const { return QList<int>() << 8 << 16; }
};

class QAudioOutput : public QObject
{
    Q_OBJECT
    public:
        QAudioOutput( const QAudioFormat &format, QObject *parent = nullptr )
            : QObject( parent ), m_format( format ), m_state( QAudio::StoppedState ), m_device( nullptr ) {}

        QAudioOutput( const QAudioDeviceInfo &deviceInfo, const QAudioFormat &format, QObject *parent = nullptr )
            : QObject( parent ), m_deviceInfo( deviceInfo ), m_format( format ), m_state( QAudio::StoppedState ), m_device( nullptr ) {}

        ~QAudioOutput() {}

        // Pull mode: returns a QIODevice* for writing audio data
        QIODevice* start()
        {
            m_state = QAudio::ActiveState;
            return m_device;
        }

        // Push mode: starts playback from the given device
        void start( QIODevice *device )
        {
            m_device = device;
            m_state = QAudio::ActiveState;
        }

        void stop() { m_state = QAudio::StoppedState; }
        void reset() { m_state = QAudio::StoppedState; }
        void suspend() { m_state = QAudio::SuspendedState; }
        void resume() { m_state = QAudio::ActiveState; }

        QAudio::State state() const { return m_state; }
        QAudioFormat format() const { return m_format; }

        void setVolume( qreal volume ) { m_volume = volume; }
        qreal volume() const { return m_volume; }

        int bytesFree() const { return 0; }
        int periodSize() const { return 0; }
        int bufferSize() const { return 0; }
        void setBufferSize( int size ) { (void)size; }

        int processedUSecs() const { return 0; }
        int elapsedUSecs() const { return 0; }

    private:
        QAudioDeviceInfo m_deviceInfo;
        QAudioFormat m_format;
        QAudio::State m_state;
        QIODevice *m_device;
        qreal m_volume = 1.0;
};

#endif // QAUDIOOUTPUT_SHIM_H
