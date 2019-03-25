#include "singleinstanceguard.h"
#include <QtDebug>
#include <QSharedMemory>
#include <QElapsedTimer>
#include <QCoreApplication>

class SingleInstanceGuardPrivate
{
public:
    SingleInstanceGuardPrivate(const QString &key)
        : m_online(false),
          m_sharedMemory(key),
          m_memKey(key)
    {
    }
    // The count of the entries in the buffer to hold the path of the files to open.
    enum { FilesBufCount = 1024 };

    struct SharedStruct {
        // A magic number to identify if this struct is initialized
        int m_magic;

        // Next empty entry in m_filesBuf.
        int m_filesBufIdx;

        // File paths to be opened.
        // Encoded in this way with 2 bytes for each size part.
        // [size of file1][file1][size of file2][file 2]
        // Unicode representation of QString.
        ushort m_filesBuf[FilesBufCount];

        // Whether other instances ask to show the legal instance.
        bool m_askedToShow;
    };

    // Append @p_file to the shared struct files buffer.
    // Returns true if succeeds or false if there is no enough space.
    bool appendFileToBuffer(SharedStruct *p_str, const QString &p_file);

    bool m_online;

    QSharedMemory m_sharedMemory;

    QString m_memKey;
};

const int c_magic = 19910925;

SingleInstanceGuard::SingleInstanceGuard(const QString &key)
    : m_p(new SingleInstanceGuardPrivate(key))
{
}

bool SingleInstanceGuard::tryRun()
{
    m_p->m_online = false;

    // If we can attach to the sharedmemory, there is another instance running.
    // In Linux, crashes may cause the shared memory segment remains. In this case,
    // this will attach to the old segment, then exit, freeing the old segment.
    if (m_p->m_sharedMemory.attach()) {
        qDebug() << "another instance is running";
        return false;
    }

    // Try to create it.
    bool ret = m_p->m_sharedMemory.create(sizeof(SingleInstanceGuardPrivate::SharedStruct));
    if (ret) {
        // We created it.
        m_p->m_sharedMemory.lock();
        SingleInstanceGuardPrivate::SharedStruct *str = (SingleInstanceGuardPrivate::SharedStruct *)m_p->m_sharedMemory.data();
        str->m_magic = c_magic;
        str->m_filesBufIdx = 0;
        str->m_askedToShow = false;
       m_p-> m_sharedMemory.unlock();

        m_p->m_online = true;
        return true;
    } else {
        qDebug() << "fail to create shared memory segment";
        return false;
    }
}

void SingleInstanceGuard::showInstance()
{
    if (!m_p->m_sharedMemory.isAttached()) {
        if (!m_p->m_sharedMemory.attach()) {
            qDebug() << "fail to attach to the shared memory segment"
                     << (m_p->m_sharedMemory.error() ? m_p->m_sharedMemory.errorString() : "");
            return;
        }
    }

    m_p->m_sharedMemory.lock();
    SingleInstanceGuardPrivate::SharedStruct *str = (SingleInstanceGuardPrivate::SharedStruct *)m_p->m_sharedMemory.data();
    Q_ASSERT(str->m_magic == c_magic);
    str->m_askedToShow = true;
    m_p->m_sharedMemory.unlock();

    qDebug() << "try to request another instance to show up";
}

void SingleInstanceGuard::openExternalFiles(const QStringList &p_files)
{
    if (p_files.isEmpty()) {
        return;
    }

    if (!m_p->m_sharedMemory.isAttached()) {
        if (!m_p->m_sharedMemory.attach()) {
            qDebug() << "fail to attach to the shared memory segment"
                     << (m_p->m_sharedMemory.error() ? m_p->m_sharedMemory.errorString() : "");
            return;
        }
    }

    qDebug() << "try to request another instance to open files" << p_files;

    int idx = 0;
    int tryCount = 100;
    while (tryCount--) {
        qDebug() << "set shared memory one round" << idx << "of" << p_files.size();
        m_p->m_sharedMemory.lock();
        SingleInstanceGuardPrivate::SharedStruct *str = (SingleInstanceGuardPrivate::SharedStruct *)m_p->m_sharedMemory.data();
        Q_ASSERT(str->m_magic == c_magic);
        for (; idx < p_files.size(); ++idx) {
            if (p_files[idx].size() + 1 > SingleInstanceGuardPrivate::FilesBufCount) {
                qDebug() << "skip file since its long name" << p_files[idx];
                // Skip this long long name file.
                continue;
            }

            if (!m_p->appendFileToBuffer(str, p_files[idx])) {
                break;
            }
        }

        m_p->m_sharedMemory.unlock();

        if (idx < p_files.size()) {
//            VUtils::sleepWait(500);
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 500) {
                QCoreApplication::processEvents();
            }
        } else {
            break;
        }
    }
}

QStringList SingleInstanceGuard::fetchFilesToOpen()
{
    QStringList files;

    if (!m_p->m_online) {
        return files;
    }

    Q_ASSERT(m_p->m_sharedMemory.isAttached());
    m_p->m_sharedMemory.lock();
    SingleInstanceGuardPrivate::SharedStruct *str = (SingleInstanceGuardPrivate::SharedStruct *)m_p->m_sharedMemory.data();
    Q_ASSERT(str->m_magic == c_magic);
    Q_ASSERT(str->m_filesBufIdx <= SingleInstanceGuardPrivate::FilesBufCount);
    int idx = 0;
    while (idx < str->m_filesBufIdx) {
        int strSize = str->m_filesBuf[idx++];
        Q_ASSERT(strSize <= str->m_filesBufIdx - idx);
        QString file;
        for (int i = 0; i < strSize; ++i) {
            file.append(QChar(str->m_filesBuf[idx++]));
        }

        files.append(file);
    }

    str->m_filesBufIdx = 0;
    m_p->m_sharedMemory.unlock();

    return files;
}

bool SingleInstanceGuard::fetchAskedToShow()
{
    if (!m_p->m_online) {
        return false;
    }

    Q_ASSERT(m_p->m_sharedMemory.isAttached());
    m_p->m_sharedMemory.lock();
    SingleInstanceGuardPrivate::SharedStruct *str = (SingleInstanceGuardPrivate::SharedStruct *)m_p->m_sharedMemory.data();
    Q_ASSERT(str->m_magic == c_magic);
    bool ret = str->m_askedToShow;
    str->m_askedToShow = false;
    m_p->m_sharedMemory.unlock();

    return ret;
}

void SingleInstanceGuard::exit()
{
    if (!m_p->m_online) {
        return;
    }

    Q_ASSERT(m_p->m_sharedMemory.isAttached());
    m_p->m_sharedMemory.detach();
    m_p->m_online = false;
}

bool SingleInstanceGuardPrivate::appendFileToBuffer(SharedStruct *p_str, const QString &p_file)
{
    if (p_file.isEmpty()) {
        return true;
    }

    int strSize = p_file.size();
    if (strSize + 1 > FilesBufCount - p_str->m_filesBufIdx) {
        qDebug() << "no enough space for" << p_file;
        return false;
    }

    // Put the size first.
    p_str->m_filesBuf[p_str->m_filesBufIdx++] = (ushort)strSize;
    const QChar *data = p_file.constData();
    for (int i = 0; i < strSize; ++i) {
        p_str->m_filesBuf[p_str->m_filesBufIdx++] = data[i].unicode();
    }

    qDebug() << "after appended one file" << p_str->m_filesBufIdx << p_file;
    return true;
}
