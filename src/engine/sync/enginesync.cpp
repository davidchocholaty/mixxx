/***************************************************************************
                          enginesync.cpp  -  master sync control for
                          maintaining beatmatching amongst n decks
                             -------------------
    begin                : Mon Mar 12 2012
    copyright            : (C) 2012 by Owen Williams
    email                : owilliams@mixxx.org
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "engine/sync/enginesync.h"

#include <QStringList>

#include "engine/sync/internalclock.h"

EngineSync::EngineSync(ConfigObject<ConfigValue>* pConfig)
        : BaseSyncableListener(pConfig),
          m_pScratchingPreviousMaster(NULL),
          m_bExplicitMasterSelected(false) {
}

EngineSync::~EngineSync() {
}

void EngineSync::requestSyncMode(Syncable* pSyncable, SyncMode mode) {
    qDebug() << "EngineSync::requestSyncMode" << pSyncable->getGroup() << mode;
    // Based on the call hierarchy I don't think this is possible. (Famous last words.)
    Q_ASSERT(pSyncable);

    const bool channelIsMaster = m_pMasterSyncable == pSyncable;

    if (mode == SYNC_MASTER) {
        // Syncable is explicitly requesting master, so we'll honor that.
        m_bExplicitMasterSelected = true;
        activateMaster(pSyncable);
    } else if (mode == SYNC_FOLLOWER) {
        if (pSyncable == m_pInternalClock && m_pMasterSyncable == m_pInternalClock &&
            syncDeckExists()) {
           // Internal cannot be set to follower if there are other decks with sync on.
           return;
        }
        // Was this deck master before?  If so do a handoff.
        if (channelIsMaster) {
            m_pMasterSyncable = NULL;
            activateFollower(pSyncable);
            // Choose a new master, but don't pick the current one.
            findNewMaster(pSyncable);
        } else if (m_pMasterSyncable == NULL) {
            // If no master active, activate the internal clock.
            activateMaster(m_pInternalClock);
        }
        activateFollower(pSyncable);
    } else {
        if (pSyncable == m_pInternalClock && m_pMasterSyncable == m_pInternalClock &&
           syncDeckExists()) {
           // Internal cannot be set to follower if there are other decks with sync on.
           return;
        }
        pSyncable->notifySyncModeChanged(SYNC_NONE);
        // if we were the master, choose a new one.
        if (channelIsMaster) {
            m_pMasterSyncable = NULL;
            findNewMaster(NULL);
        }
    }
}

void EngineSync::requestEnableSync(Syncable* pSyncable, bool bEnabled) {
    qDebug() << "EngineSync::requestEnableSync" << pSyncable->getGroup() << bEnabled;

    SyncMode syncMode = pSyncable->getSyncMode();
    bool syncEnabled = syncMode != SYNC_NONE;
    // Already enabled.
    if (syncEnabled == bEnabled) {
        return;
    }

    if (bEnabled) {
        if (m_pMasterSyncable == NULL) {
            // There is no sync source.  If any other deck is playing we will
            // match the first available bpm -- sync won't be enabled on these decks,
            // otherwise there would have been a sync source.

            bool foundTargetBpm = false;
            double targetBpm = 0.0;
            double targetBeatDistance = 0.0;

            foreach (Syncable* other_deck, m_syncables) {
                if (other_deck == pSyncable) {
                    continue;
                }

                if (other_deck->isPlaying()) {
                    foundTargetBpm = true;
                    targetBpm = other_deck->getBpm();
                    targetBeatDistance = other_deck->getBeatDistance();
                    break;
                }
            }

            activateMaster(m_pInternalClock);

            if (foundTargetBpm) {
                setMasterBpm(NULL, targetBpm);
                setMasterBeatDistance(NULL, targetBeatDistance);
            }
        } else if (m_pMasterSyncable == m_pInternalClock && playingSyncDeckCount() <= 1) {
            // If there is only one follower, reset the internal clock beat distance.
            setMasterBeatDistance(pSyncable, pSyncable->getBeatDistance());
        }
        activateFollower(pSyncable);
    } else {
        pSyncable->notifySyncModeChanged(SYNC_NONE);
        // It was the master.
        if (syncMode == SYNC_MASTER) {
            m_pMasterSyncable = NULL;
            findNewMaster(pSyncable);
        } else if (!syncDeckExists()) {
            // If this was the last sync deck, turn off the Internal Clock master.
            deactivateSync(m_pInternalClock);
        }
    }
}

void EngineSync::notifyPlaying(Syncable* pSyncable, bool playing) {
    qDebug() << "EngineSync::notifyPlaying" << pSyncable->getGroup() << playing;
    // For now we don't care if the deck is now playing or stopping.
    if (pSyncable->getSyncMode() != SYNC_NONE) {
        if (m_pMasterSyncable == m_pInternalClock) {
            if (!syncDeckExists()) {
                deactivateSync(m_pInternalClock);
            } else {
                // If there is only one deck playing, set internal clock beat distance to match it.
                Syncable* uniqueSyncable = NULL;
                int playing_sync_decks = 0;
                foreach (Syncable* pOtherSyncable, m_syncables) {
                    if (pOtherSyncable->isPlaying()) {
                        uniqueSyncable = pOtherSyncable;
                        ++playing_sync_decks;
                    }
                }
                if (playing_sync_decks == 1) {
                    m_pInternalClock->setBeatDistance(uniqueSyncable->getBeatDistance());
                }
            }
        }
    }
}

void EngineSync::notifyScratching(Syncable* pSyncable, bool scratching) {
    SyncMode mode = pSyncable->getSyncMode();

    // Ignore decks that aren't part of the sync group.
    if (mode == SYNC_NONE) {
        return;
    }

    // Syncable started scratching.
    if (scratching) {
        // If there is no explicit master.
        if (!m_bExplicitMasterSelected) {
            // If the syncable is not the master, become the master.
            if (mode != SYNC_MASTER) {
                // TODO(rryan): This is kind of janky when multiple decks
                // scratch at once. For now, the last "previous master" always
                // wins.
                m_pScratchingPreviousMaster = m_pMasterSyncable;
                activateMaster(pSyncable);
            }
        } else {
            // The master is explicitly not us. Don't do anything.
        }
    } else {
        // Syncable stopped scratching.
        if (!m_bExplicitMasterSelected) {
            // There was a previous master.
            if (m_pScratchingPreviousMaster) {
                activateMaster(m_pScratchingPreviousMaster);
                m_pScratchingPreviousMaster = NULL;
            }
        } else {
            // The master is explicitly set. Don't do anything.
        }
    }
}

void EngineSync::notifyBpmChanged(Syncable* pSyncable, double bpm, bool fileChanged) {
    qDebug() << "EngineSync::notifyBpmChanged" << pSyncable->getGroup() << bpm;

    SyncMode syncMode = pSyncable->getSyncMode();
    if (syncMode == SYNC_NONE) {
        return;
    }

    // EngineSyncTest.SlaveRateChange dictates this must not happen in general,
    // but it is required when the file BPM changes because it's not a true BPM
    // change, so we set the follower back to the master BPM.
    if (syncMode == SYNC_FOLLOWER && fileChanged) {
        pSyncable->setBpm(masterBpm());
        return;
    }

    setMasterBpm(pSyncable, bpm);
}

void EngineSync::notifyInstantaneousBpmChanged(Syncable* pSyncable, double bpm) {
    //qDebug() << "EngineSync::notifyInstantaneousBpmChanged" << pSyncable->getGroup() << bpm;
    if (pSyncable->getSyncMode() != SYNC_MASTER) {
        return;
    }

    // Do not update the master rate slider because instantaneous changes are
    // not user visible.
    setMasterInstantaneousBpm(pSyncable, bpm);
}

void EngineSync::notifyBeatDistanceChanged(Syncable* pSyncable, double beat_distance) {
    //qDebug() << "EngineSync::notifyBeatDistanceChanged" << pSyncable->getGroup() << beat_distance;
    if (pSyncable->getSyncMode() != SYNC_MASTER) {
        return;
    }

    setMasterBeatDistance(pSyncable, beat_distance);
}

void EngineSync::activateFollower(Syncable* pSyncable) {
    pSyncable->notifySyncModeChanged(SYNC_FOLLOWER);
    pSyncable->setBpm(masterBpm());
    pSyncable->setBeatDistance(masterBeatDistance());
}

void EngineSync::activateMaster(Syncable* pSyncable) {
    if (pSyncable == NULL) {
        qDebug() << "WARNING: Logic Error: Called activateMaster on a NULL Syncable.";
        return;
    }

    // Already master, no need to do anything.
    if (m_pMasterSyncable == pSyncable) {
        // Sanity check.
        if (m_pMasterSyncable->getSyncMode() != SYNC_MASTER) {
            qDebug() << "WARNING: Logic Error: m_pMasterSyncable is a syncable that does not think it is master.";
        }
        return;
    }

    // If a channel is master, disable it.
    Syncable* pOldChannelMaster = m_pMasterSyncable;

    m_pMasterSyncable = NULL;
    if (pOldChannelMaster) {
        activateFollower(pOldChannelMaster);
    }

    // Only consider channels that have a track loaded and are in the master
    // mix.
    // TODO(rryan): We don't actually do what this comment describes.
    qDebug() << "Setting up master " << pSyncable->getGroup();
    m_pMasterSyncable = pSyncable;
    pSyncable->notifySyncModeChanged(SYNC_MASTER);
    // TODO(rryan): Iffy? We should not be calling these methods. But there's no
    // other method that does exactly this.

    // If there is no existing master, and this is the internal clock, pick up BPM from
    // the playing deck, or any deck if it comes to that.
    if (pOldChannelMaster == NULL && pSyncable == m_pInternalClock) {
        // Should we use PlayerInfo / getCurrentPlayingDeck()? It returns an int which is
        // hard to convert to a Syncable.
        Syncable* pInternalInitSyncable = NULL;
        foreach (Syncable* pOtherSyncable, m_syncables) {
            if (pOtherSyncable->isPlaying()) {
                // We prefer decks that are playing.
                pInternalInitSyncable = pOtherSyncable;
                break;
            } else if (pOtherSyncable->getBpm() != 0) {
                // We will also accept a deck that simply has a non-zero bpm.
                pInternalInitSyncable = pOtherSyncable;
            }
        }
        if (pInternalInitSyncable != NULL) {
            m_pInternalClock->setBpm(pInternalInitSyncable->getBpm());
            m_pInternalClock->setBeatDistance(pInternalInitSyncable->getBeatDistance());
        }
    } else {
        notifyBpmChanged(pSyncable, pSyncable->getBpm());
    }

    notifyBeatDistanceChanged(pSyncable, pSyncable->getBeatDistance());
}

void EngineSync::findNewMaster(Syncable* pDontPick) {
    qDebug() << "EngineSync::findNewMaster" << (pDontPick ? pDontPick->getGroup() : "(null)");
    int playing_sync_decks = 0;
    int paused_sync_decks = 0;
    Syncable *new_master = NULL;

    if (m_pMasterSyncable != NULL) {
        qDebug() << "WARNING: Logic Error: findNewMaster called when a master is selected.";
    }

    foreach (Syncable* pSyncable, m_syncables) {
        if (pSyncable == pDontPick) {
            qDebug() << "findNewMaster: Skipping" << pSyncable->getGroup() << "because DONTPICK";
            continue;
        }

        SyncMode sync_mode = pSyncable->getSyncMode();
        if (sync_mode == SYNC_NONE) {
            qDebug() << "findNewMaster: Skipping" << pSyncable->getGroup() << "because SYNC_NONE";
            continue;
        }

        if (sync_mode == SYNC_MASTER) {
            qDebug() << "WARNING: Logic Error: findNewMaster: A Syncable with SYNC_MASTER exists.";
            return;
        }

        if (pSyncable->isPlaying()) {
            ++playing_sync_decks;
            new_master = pSyncable;
        } else {
            ++paused_sync_decks;
        }
    }

    if (playing_sync_decks == 1) {
        if (new_master != NULL) {
            activateMaster(new_master);
        }
    } else if (pDontPick != m_pInternalClock) {
        // If there are no more synced decks, there is no need for a master.
        if (playing_sync_decks + paused_sync_decks > 0) {
            activateMaster(m_pInternalClock);
        }
    } else {
        // Clock master was specifically disabled. Just go with new_master if it
        // exists, otherwise give up and pick nothing.
        if (new_master != NULL) {
            activateMaster(new_master);
        }
    }
    // Even if we didn't successfully find a new master, unset this value.
    m_bExplicitMasterSelected = false;
}

void EngineSync::deactivateSync(Syncable* pSyncable) {
    if (pSyncable->getSyncMode() == SYNC_MASTER) {
        m_pMasterSyncable = NULL;
        findNewMaster(pSyncable);
    }
    pSyncable->notifySyncModeChanged(SYNC_NONE);
}
