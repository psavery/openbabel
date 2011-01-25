/**********************************************************************
  QueueManager - Generic queue manager to track running structures

  Copyright (C) 2010 by David C. Lonie

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include <globalsearch/tracker.h>

#include <QtCore/QMutex>
#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <QtCore/QStringList>
#include <QtCore/QReadWriteLock>

namespace GlobalSearch {
  class OptBase;
  class Structure;

  /**
   * @class QueueManager queuemanager.h <globalsearch/queuemanager.h>
   *
   * @brief The QueueManager monitors the running jobs and updates
   * Structure status.
   *
   * @author David C. Lonie
   *
   * The QueueManager creates a local queue and monitoring system
   * control the submission of Structures to an optimization
   * engine or queue.
   *
   * For basic usage, connect the needNewStructure() signal to slot
   * that will generate a new Structure and submit it the following
   * way:
@verbatim
// lockForNaming() returns a list of all structures that the main
// tracker is aware of. It also locks a naming mutex to prevent
// simultaneous naming of Structures, avoiding duplicate
// Structure indices, ID numbers, etc.
QList<Structure*> allStructures = m_queue->lockForNaming();
// Check the Structures in allStructures to determine the next
// available name. The follow code uses a generation and ID number
// for an evolutionary/genetic algorithm. Other methods may only set
// the ID number. (note that the generation number is set already
// in the following example).
Structure *structure;
uint id = 1;
for (int j = 0; j < allStructures.size(); j++) {
  structure = allStructures.at(j);
  structure->lock()->lockForRead();
  if (structure->getGeneration() == generation &&
      structure->getIDNumber() >= id) {
    id = structure->getIDNumber() + 1;
  }
  structure->lock()->unlock();
}

// Assign data to structure (created elsewhere)
QWriteLocker locker (newStructure->lock());
newStructure->setIDNumber(id);
newStructure->setGeneration(generation);
newStructure->setParents(parents);
// Determine, create, and assign paths
QString id_s, gen_s, locpath_s, rempath_s;
id_s.sprintf("%05d",structure->getIDNumber());
gen_s.sprintf("%05d",structure->getGeneration());
locpath_s = filePath + "/" + gen_s + "x" + id_s + "/";
rempath_s = rempath + "/" + gen_s + "x" + id_s + "/";
QDir dir (locpath_s);
if (!dir.exists()) {
  if (!dir.mkpath(locpath_s)) {
    // Output error
  }
}
newStructure->setFileName(locpath_s);
newStructure->setRempath(rempath_s);
newStructure->setCurrentOptStep(1);
newStructure->findSpaceGroup();
// unlockForNaming(Structure*) unlocks the naming mutex and
// begins processing the structure that is passed.
m_queue->unlockForNaming(newStructure);
@endverbatim
   *
   * Most functions in this class do not need to be called as they are
   * automatically called when needed. Check the source if in doubt.
   */
  class QueueManager : public QThread
  {
    Q_OBJECT

  public:
    /**
     * Constructor.
     *
     * @param opt The OptBase class the QueueManager uses
     */
    explicit QueueManager(OptBase *opt);

    /**
     * Destructor.
     */
    virtual ~QueueManager();


  signals:
    /**
     * Emitted when a Structure is accepted into the queuemanager
     * (e.g. after submission through unlockForNaming(Structure*)
     * @param s The Structure that has been accepted
     * @sa lockForNaming
     * @sa unlockForNaming
     */
    void structureStarted(GlobalSearch::Structure *s);

    /**
     *  Emitted when a Structure is submitted for optimization
     * @param s The Structure that has been submitted
     */
    void structureSubmitted(GlobalSearch::Structure *s);

    /**
     * Emitted when a Structure has been killed through
     * killStructure(Structure*)
     * @param s The Structure that has been killed
     * @sa killStructure
     */
    void structureKilled(GlobalSearch::Structure *s);

    /**
     * Emitted when a Structure has changed status. Useful for
     * updating progress tables, plots, etc
     * @param s The Structure that has been updated
     */
    void structureUpdated(GlobalSearch::Structure *s);

    /**
     * Emitted when a Structure has completed all optimization steps.
     *
     * @param s The Structure that has been updated
     */
    void structureFinished(GlobalSearch::Structure *s);

    /**
     * Emitted when the number of unoptimized Structures drops below
     * OptBase::contStructs. This is connected to
     * OptBase::generateNewStructure() by default.
     */
    void needNewStructure();

    /**
     * Emitted when checkPopulation() is called to provide a short
     * summary of the queuemanager's status.
     *
     * @param optimized Number of optimized structures
     * @param running Number of running structures (e.g. submitted
     * for optimization)
     * @param failing Number of structures with a getFailCount() > 0
     */
    void newStatusOverview(int optimized, int running, int failing);

   public slots:
    /**
     * Reset all trackers in trackerList
     */
    void reset();

    void resetRequestedStructures(int i = 0) {m_requestedStructures = i;};

    /**
     * Check all Structures in the main Tracker and assign them to
     * other trackers as needed (runningTracker, etc.).
     *
     * If more structures are needed, they are requested in this
     * function by emitting needNewStructure().
     *
     * This function also submits new structures to the optimization
     * engine if needed.
     *
     * Also emits newStatusOverview for a summary of the queue's
     * status.
     * @sa newStatusOverview
     * @sa needNewStructure
     */
    void checkPopulation();

    /**
     * Monitors the Structures in getAllRunningStructures() and
     * updates their statuses if they've changed.
     */
    void checkRunning();

    /**
     * Update the data from the remote PBS queue.
     */
    void updateQueue();

    /**
     * Called on Structures that are Structure::StepOptimized, this
     * function will update the Structure with the results of the
     * optimization and pass it along to
     * prepareStructureForNextOptStep.
     *
     * @param s The step optimized structure
     * @sa prepareStructureForNextOptStep
     */
    void updateStructure(Structure *s);

    /**
     * Stops any running optimization processes associated with a
     * structure and sets its status to Structure::Killed.
     *
     * The structureKilled signal is emitted as well.
     *
     * @param s The Structure to kill.
     * @sa structureKilled
     */
    void killStructure(Structure *s);

    /**
     * Writes the input files for the optimization process and queues
     * the Structure to be submitted for optimization.
     *
     * @param s The Structure to be submitted
     * @param optStep Optional optimization step. If omitted, the
     * Structure's currentOptStep() is used.
     */
    void submitStructure(Structure *s, int optStep = 0);

    /**
     * Submits the first Structure in m_jobStartTracker for
     * submission. This should not be called directly, instead call
     * prepareStructureForSubmission(Structure*).
     *
     * @param structure The structure to submit
     *
     * @sa prepareStructureForSubmission
     */
    void startJob(Structure *structure);

    /**
     * Kills the optimization process for the indicated Structure.
     *
     * @param s The Structure to stop optimizing.
     */
    void stopJob(Structure *s);

    /**
     * Appends a Structure to m_jobStartTracker. This should not be
     * used unless resuming a session, and then only for structures
     * that are marked Structure::WaitingForOptimization.
     *
     * All other cases should use
     * prepareStructureForSubmission(Structure*)
     *
     * @param s The Structure to be appended
     * @sa prepareStructureForSubmission
     */
    void appendToJobStartTracker(Structure *s);

    /**
     * Return the cached queue data from a remote PBS server.
     *
     * @return A QStringList containing the output of OptBase::qstat
     * on the remote server.
     * @sa updateQueue
     */
    QStringList getRemoteQueueData() {QMutexLocker l (&m_queueDataMutex);
      return m_queueData;};

    /**
     * @return All Structures in m_runningTracker
     */
    QList<Structure*> getAllRunningStructures();

    /**
     * @return All Structures in m_tracker with status
     * Structure::Optimized.
     */
    QList<Structure*> getAllOptimizedStructures();

    /**
     * @return All Structures in m_tracker with status
     * Structure::Duplicate.
     */
    QList<Structure*> getAllDuplicateStructures();

    /**
     * @return All Structures in m_tracker and m_startPendingTracker
     */
    QList<Structure*> getAllStructures();

    /**
     * Locks the m_tracker and m_newStructure for
     * reading.
     *
     * @return All Structures in getAllStructures()
     */
    QList<Structure*> lockForNaming();

    /**
     * Unlocks both the main and startPending trackers.
     *
     * @param s Optional new stucture to be added to the queuemanager
     * and tracker.
     */
    void unlockForNaming(Structure *s = 0);

    void run();

   protected slots:
    void handleOptimizedStructure(Structure *s);
    void handleStepOptimizedStructure(Structure *s);
    void handleWaitingForOptimizationStructure(Structure *s);
    void handleInProcessStructure(Structure *s);
    void handleEmptyStructure(Structure *s);
    void handleUpdatingStructure(Structure *s);
    void handleErrorStructure(Structure *s);
    void handleSubmittedStructure(Structure *s);
    void handleKilledStructure(Structure *s);
    void handleRemovedStructure(Structure *s);
    void handleDuplicateStructure(Structure *s);
    void handleRestartStructure(Structure *s);

   protected:
    OptBase *m_opt;
    Tracker *m_tracker;

    /// Tracks which structures are currently running
    Tracker m_runningTracker;
    /// Tracks which structures are queued to be submitted
    Tracker m_jobStartTracker;
    /// Tracks structures that have been returned from m_opt but have
    /// not yet been accepted into m_tracker.
    Tracker m_newStructureTracker;

    int m_requestedStructures;

    QStringList m_queueData;
    QMutex m_queueDataMutex;

    void submitStructure_(Structure *s);
    void startJob_(Structure *s);
    void unlockForNaming_();
 };

} // end namespace GlobalSearch

#endif
