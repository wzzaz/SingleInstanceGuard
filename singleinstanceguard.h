#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H

#include <QStringList>
#include <QSharedMemory>

class SingleInstanceGuardPrivate;
class SingleInstanceGuard
{
public:
    // Initialize progress memory key
    SingleInstanceGuard(const QString &key);

    // Return true if this is the only instance of progress
    bool tryRun();

    // Ask another instance to show itself
    void showInstance();

    // There is already another instance running.
    // Call this to ask that instance to open external files passed in
    // via command line arguments.
    void openExternalFiles(const QStringList &p_files);


    // Fetch files from shared memory to open.
    // Will clear the shared memory.
    QStringList fetchFilesToOpen();

    // Whether this instance is asked to show itself.
    bool fetchAskedToShow();

    // A running instance requests to exit.
    void exit();

private:
    SingleInstanceGuardPrivate *m_p;
};

#endif // SINGLEINSTANCEGUARD_H
