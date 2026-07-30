/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2026  nitrobass24
 * Copyright (C) 2018-2025  Vladimir Golovnev <glassez@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "maindatastore.h"

#include <QJsonObject>
#include <QList>

#include "base/algorithm.h"
#include "base/bittorrent/cachestatus.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/sessionstatus.h"
#include "base/bittorrent/torrent.h"
#include "base/bittorrent/trackerentrystatus.h"
#include "base/global.h"
#include "base/utils/string.h"
#include "serialize/serialize_torrent.h"

namespace
{
    const QString KEY_SYNC_MAINDATA_QUEUEING = u"queueing"_s;
    const QString KEY_SYNC_MAINDATA_REFRESH_INTERVAL = u"refresh_interval"_s;
    const QString KEY_SYNC_MAINDATA_USE_ALT_SPEED_LIMITS = u"use_alt_speed_limits"_s;

    // TransferInfo keys
    const QString KEY_TRANSFER_CONNECTION_STATUS = u"connection_status"_s;
    const QString KEY_TRANSFER_DHT_NODES = u"dht_nodes"_s;
    const QString KEY_TRANSFER_DLDATA = u"dl_info_data"_s;
    const QString KEY_TRANSFER_DLRATELIMIT = u"dl_rate_limit"_s;
    const QString KEY_TRANSFER_DLSPEED = u"dl_info_speed"_s;
    const QString KEY_TRANSFER_FREESPACEONDISK = u"free_space_on_disk"_s;
    const QString KEY_TRANSFER_LAST_EXTERNAL_ADDRESS_V4 = u"last_external_address_v4"_s;
    const QString KEY_TRANSFER_LAST_EXTERNAL_ADDRESS_V6 = u"last_external_address_v6"_s;
    const QString KEY_TRANSFER_UPDATA = u"up_info_data"_s;
    const QString KEY_TRANSFER_UPRATELIMIT = u"up_rate_limit"_s;
    const QString KEY_TRANSFER_UPSPEED = u"up_info_speed"_s;

    // Statistics keys
    const QString KEY_TRANSFER_ALLTIME_DL = u"alltime_dl"_s;
    const QString KEY_TRANSFER_ALLTIME_UL = u"alltime_ul"_s;
    const QString KEY_TRANSFER_AVERAGE_TIME_QUEUE = u"average_time_queue"_s;
    const QString KEY_TRANSFER_GLOBAL_RATIO = u"global_ratio"_s;
    const QString KEY_TRANSFER_QUEUED_IO_JOBS = u"queued_io_jobs"_s;
    const QString KEY_TRANSFER_QUEUED_TRACKER_ANNOUNCES = u"queued_tracker_announces"_s;
    const QString KEY_TRANSFER_READ_CACHE_HITS = u"read_cache_hits"_s;
    const QString KEY_TRANSFER_READ_CACHE_OVERLOAD = u"read_cache_overload"_s;
    const QString KEY_TRANSFER_REQUEST_LATENCY = u"request_latency"_s;
    const QString KEY_TRANSFER_TOTAL_BUFFERS_SIZE = u"total_buffers_size"_s;
    const QString KEY_TRANSFER_TOTAL_PEER_CONNECTIONS = u"total_peer_connections"_s;
    const QString KEY_TRANSFER_TOTAL_QUEUED_SIZE = u"total_queued_size"_s;
    const QString KEY_TRANSFER_TOTAL_WASTE_SESSION = u"total_wasted_session"_s;
    const QString KEY_TRANSFER_WRITE_CACHE_OVERLOAD = u"write_cache_overload"_s;

    const QString KEY_TORRENT_HAS_TRACKER_WARNING = u"has_tracker_warning"_s;
    const QString KEY_TORRENT_HAS_TRACKER_ERROR = u"has_tracker_error"_s;
    const QString KEY_TORRENT_HAS_OTHER_ANNOUNCE_ERROR = u"has_other_announce_error"_s;

    QStringList asStrings(const QSet<BitTorrent::TorrentID> &torrentIDs)
    {
        QStringList result;
        result.reserve(torrentIDs.size());
        for (const BitTorrent::TorrentID &torrentID : torrentIDs)
            result.emplaceBack(torrentID.toString());

        return result;
    }

    bool hasWarningMessage(const BitTorrent::TrackerEntryStatus &status)
    {
        return std::ranges::any_of(status.endpoints, [](const BitTorrent::TrackerEndpointStatus &endpointEntry)
        {
            return (endpointEntry.state == BitTorrent::TrackerEndpointState::Working) && !endpointEntry.message.isEmpty();
        });
    }

    // Compare two flat structures (only scalar values) and return the changed fields.
    QVariantMap diffMap(const QVariantMap &prevData, const QVariantMap &data)
    {
        QVariantMap changes;
        for (auto i = data.cbegin(); i != data.cend(); ++i)
        {
            if (prevData[i.key()] != i.value())
                changes[i.key()] = i.value();
        }

        return changes;
    }

    QVariantMap getTransferInfo()
    {
        QVariantMap map;
        const auto *session = BitTorrent::Session::instance();

        const BitTorrent::SessionStatus &sessionStatus = session->status();
        const BitTorrent::CacheStatus &cacheStatus = session->cacheStatus();
        map[KEY_TRANSFER_DLSPEED] = sessionStatus.payloadDownloadRate;
        map[KEY_TRANSFER_DLDATA] = sessionStatus.totalPayloadDownload;
        map[KEY_TRANSFER_UPSPEED] = sessionStatus.payloadUploadRate;
        map[KEY_TRANSFER_UPDATA] = sessionStatus.totalPayloadUpload;
        map[KEY_TRANSFER_DLRATELIMIT] = session->downloadSpeedLimit();
        map[KEY_TRANSFER_UPRATELIMIT] = session->uploadSpeedLimit();

        const qint64 atd = sessionStatus.allTimeDownload;
        const qint64 atu = sessionStatus.allTimeUpload;
        map[KEY_TRANSFER_ALLTIME_DL] = atd;
        map[KEY_TRANSFER_ALLTIME_UL] = atu;
        map[KEY_TRANSFER_TOTAL_WASTE_SESSION] = sessionStatus.totalWasted;
        map[KEY_TRANSFER_GLOBAL_RATIO] = ((atd > 0) && (atu > 0)) ? Utils::String::fromDouble(static_cast<qreal>(atu) / atd, 2) : u"-"_s;
        map[KEY_TRANSFER_TOTAL_PEER_CONNECTIONS] = sessionStatus.peersCount;

        const qreal readRatio = cacheStatus.readRatio;  // TODO: remove when LIBTORRENT_VERSION_NUM >= 20000
        map[KEY_TRANSFER_READ_CACHE_HITS] = (readRatio > 0) ? Utils::String::fromDouble(100 * readRatio, 2) : u"0"_s;
        map[KEY_TRANSFER_TOTAL_BUFFERS_SIZE] = cacheStatus.totalUsedBuffers * 16 * 1024;

        map[KEY_TRANSFER_WRITE_CACHE_OVERLOAD] = ((sessionStatus.diskWriteQueue > 0) && (sessionStatus.peersCount > 0))
            ? Utils::String::fromDouble((100. * sessionStatus.diskWriteQueue / sessionStatus.peersCount), 2)
            : u"0"_s;
        map[KEY_TRANSFER_READ_CACHE_OVERLOAD] = ((sessionStatus.diskReadQueue > 0) && (sessionStatus.peersCount > 0))
            ? Utils::String::fromDouble((100. * sessionStatus.diskReadQueue / sessionStatus.peersCount), 2)
            : u"0"_s;

        map[KEY_TRANSFER_QUEUED_IO_JOBS] = cacheStatus.jobQueueLength;
        map[KEY_TRANSFER_AVERAGE_TIME_QUEUE] = cacheStatus.averageJobTime;
        map[KEY_TRANSFER_TOTAL_QUEUED_SIZE] = cacheStatus.queuedBytes;
        map[KEY_TRANSFER_REQUEST_LATENCY] = cacheStatus.requestLatency;

        map[KEY_TRANSFER_LAST_EXTERNAL_ADDRESS_V4] = session->lastExternalIPv4Address();
        map[KEY_TRANSFER_LAST_EXTERNAL_ADDRESS_V6] = session->lastExternalIPv6Address();
        map[KEY_TRANSFER_DHT_NODES] = sessionStatus.dhtNodes;
        map[KEY_TRANSFER_CONNECTION_STATUS] = session->isListening()
            ? (sessionStatus.hasIncomingConnections ? u"connected"_s : u"firewalled"_s)
            : u"disconnected"_s;

        // Tracker statistics
        map[KEY_TRANSFER_QUEUED_TRACKER_ANNOUNCES] = sessionStatus.queuedTrackerAnnounces;

        return map;
    }

    void addAnnounceStats(QVariantMap &serializedTorrent, const BitTorrent::Torrent *torrent)
    {
        bool hasTrackerWarning = false;
        bool hasTrackerError = false;
        bool hasOtherAnnounceError = false;
        for (const BitTorrent::TrackerEntryStatus &status : asConst(torrent->trackers()))
        {
            switch (status.state)
            {
            case BitTorrent::TrackerEndpointState::Working:
                if (!hasTrackerWarning && hasWarningMessage(status))
                    hasTrackerWarning = true;
                break;
            case BitTorrent::TrackerEndpointState::TrackerError:
                hasTrackerError = true;
                break;
            case BitTorrent::TrackerEndpointState::NotWorking:
            case BitTorrent::TrackerEndpointState::Unreachable:
                hasOtherAnnounceError = true;
                break;
            default:
                break;
            }

            if (hasTrackerWarning && hasTrackerError && hasOtherAnnounceError)
                break;
        }

        serializedTorrent[KEY_TORRENT_HAS_TRACKER_WARNING] = hasTrackerWarning;
        serializedTorrent[KEY_TORRENT_HAS_TRACKER_ERROR] = hasTrackerError;
        serializedTorrent[KEY_TORRENT_HAS_OTHER_ANNOUNCE_ERROR] = hasOtherAnnounceError;
    }
}

MaindataStore::MaindataStore(QObject *parent)
    : QObject(parent)
{
    makeSnapshot();

    const auto *btSession = BitTorrent::Session::instance();
    connect(btSession, &BitTorrent::Session::categoryAdded, this, &MaindataStore::onCategoryAdded);
    connect(btSession, &BitTorrent::Session::categoryRemoved, this, &MaindataStore::onCategoryRemoved);
    connect(btSession, &BitTorrent::Session::categoryOptionsChanged, this, &MaindataStore::onCategoryOptionsChanged);
    connect(btSession, &BitTorrent::Session::subcategoriesSupportChanged, this, &MaindataStore::onSubcategoriesSupportChanged);
    connect(btSession, &BitTorrent::Session::tagAdded, this, &MaindataStore::onTagAdded);
    connect(btSession, &BitTorrent::Session::tagRemoved, this, &MaindataStore::onTagRemoved);
    connect(btSession, &BitTorrent::Session::torrentAdded, this, &MaindataStore::onTorrentAdded);
    connect(btSession, &BitTorrent::Session::torrentAboutToBeRemoved, this, &MaindataStore::onTorrentAboutToBeRemoved);
    connect(btSession, &BitTorrent::Session::torrentCategoryChanged, this, &MaindataStore::onTorrentCategoryChanged);
    connect(btSession, &BitTorrent::Session::torrentMetadataReceived, this, &MaindataStore::onTorrentMetadataReceived);
    connect(btSession, &BitTorrent::Session::torrentStopped, this, &MaindataStore::onTorrentStopped);
    connect(btSession, &BitTorrent::Session::torrentStarted, this, &MaindataStore::onTorrentStarted);
    connect(btSession, &BitTorrent::Session::torrentSavePathChanged, this, &MaindataStore::onTorrentSavePathChanged);
    connect(btSession, &BitTorrent::Session::torrentSavingModeChanged, this, &MaindataStore::onTorrentSavingModeChanged);
    connect(btSession, &BitTorrent::Session::torrentTagAdded, this, &MaindataStore::onTorrentTagAdded);
    connect(btSession, &BitTorrent::Session::torrentTagRemoved, this, &MaindataStore::onTorrentTagRemoved);
    connect(btSession, &BitTorrent::Session::torrentsUpdated, this, &MaindataStore::onTorrentsUpdated);
    connect(btSession, &BitTorrent::Session::trackersAdded, this, &MaindataStore::onTorrentTrackersChanged);
    connect(btSession, &BitTorrent::Session::trackersRemoved, this, &MaindataStore::onTorrentTrackersChanged);
    connect(btSession, &BitTorrent::Session::trackersReset, this, &MaindataStore::onTorrentTrackersChanged);
    connect(btSession, &BitTorrent::Session::trackerEntryStatusesUpdated, this, &MaindataStore::onTorrentTrackerEntryStatusesUpdated);
}

const MaindataStore::Data &MaindataStore::snapshot() const
{
    return m_snapshot;
}

void MaindataStore::makeSnapshot()
{
    m_knownTrackers.clear();
    m_snapshot = {};

    const auto *session = BitTorrent::Session::instance();

    for (const BitTorrent::Torrent *torrent : asConst(session->torrents()))
    {
        const BitTorrent::TorrentID torrentID = torrent->id();

        QVariantMap serializedTorrent = serialize(*torrent);
        serializedTorrent.remove(KEY_TORRENT_ID);
        addAnnounceStats(serializedTorrent, torrent);

        for (const BitTorrent::TrackerEntryStatus &status : asConst(torrent->trackers()))
            m_knownTrackers[status.url].insert(torrentID);

        m_snapshot.torrents[torrentID.toString()] = serializedTorrent;
    }

    const QStringList categoriesList = session->categories();
    for (const auto &categoryName : categoriesList)
    {
        const BitTorrent::CategoryOptions categoryOptions = session->categoryOptions(categoryName);
        QJsonObject category = categoryOptions.toJSON();
        // adjust it to be compatible with existing WebAPI
        category[u"savePath"_s] = category.take(u"save_path"_s);
        category.insert(u"name"_s, categoryName);
        m_snapshot.categories[categoryName] = category.toVariantMap();
    }

    for (const Tag &tag : asConst(session->tags()))
        m_snapshot.tags.append(tag.toString());

    for (const auto &[tracker, torrentIDs] : asConst(m_knownTrackers).asKeyValueRange())
        m_snapshot.trackers[tracker] = asStrings(torrentIDs);

    m_snapshot.serverState = getTransferInfo();
    m_snapshot.serverState[KEY_TRANSFER_FREESPACEONDISK] = m_freeDiskSpace;
    m_snapshot.serverState[KEY_SYNC_MAINDATA_QUEUEING] = session->isQueueingSystemEnabled();
    m_snapshot.serverState[KEY_SYNC_MAINDATA_USE_ALT_SPEED_LIMITS] = session->isAltGlobalSpeedLimitEnabled();
    m_snapshot.serverState[KEY_SYNC_MAINDATA_REFRESH_INTERVAL] = session->refreshInterval();
}

void MaindataStore::flush()
{
    Data delta;
    const auto *session = BitTorrent::Session::instance();

    for (const QString &categoryName : asConst(m_updatedCategories))
    {
        const BitTorrent::CategoryOptions categoryOptions = session->categoryOptions(categoryName);
        auto category = categoryOptions.toJSON().toVariantMap();
        // adjust it to be compatible with existing WebAPI
        category[u"savePath"_s] = category.take(u"save_path"_s);
        category.insert(u"name"_s, categoryName);

        auto &categorySnapshot = m_snapshot.categories[categoryName];
        if (const QVariantMap changes = diffMap(categorySnapshot, category); !changes.isEmpty())
        {
            delta.categories[categoryName] = changes;
            categorySnapshot = category;
        }
    }
    m_updatedCategories.clear();

    for (const QString &category : asConst(m_removedCategories))
    {
        delta.removedCategories.append(category);
        m_snapshot.categories.remove(category);
    }
    m_removedCategories.clear();

    for (const QString &tag : asConst(m_addedTags))
    {
        delta.tags.append(tag);
        m_snapshot.tags.append(tag);
    }
    m_addedTags.clear();

    for (const QString &tag : asConst(m_removedTags))
    {
        delta.removedTags.append(tag);
        m_snapshot.tags.removeOne(tag);
    }
    m_removedTags.clear();

    for (const BitTorrent::TorrentID &torrentID : asConst(m_updatedTorrents))
    {
        const BitTorrent::Torrent *torrent = session->getTorrent(torrentID);
        Q_ASSERT(torrent);

        QVariantMap serializedTorrent = serialize(*torrent);
        serializedTorrent.remove(KEY_TORRENT_ID);

        const QString torrentIDStr = torrentID.toString();
        auto &torrentSnapshot = m_snapshot.torrents[torrentIDStr];

        if (m_announcedTorrents.contains(torrentID))
        {
            addAnnounceStats(serializedTorrent, torrent);
        }
        else
        {
            serializedTorrent[KEY_TORRENT_HAS_TRACKER_WARNING] = torrentSnapshot[KEY_TORRENT_HAS_TRACKER_WARNING];
            serializedTorrent[KEY_TORRENT_HAS_TRACKER_ERROR] = torrentSnapshot[KEY_TORRENT_HAS_TRACKER_ERROR];
            serializedTorrent[KEY_TORRENT_HAS_OTHER_ANNOUNCE_ERROR] = torrentSnapshot[KEY_TORRENT_HAS_OTHER_ANNOUNCE_ERROR];
        }

        if (const QVariantMap changes = diffMap(torrentSnapshot, serializedTorrent); !changes.isEmpty())
        {
            delta.torrents[torrentIDStr] = changes;
            torrentSnapshot = serializedTorrent;
        }
    }

    for (const BitTorrent::TorrentID &torrentID : asConst(m_announcedTorrents))
    {
        if (m_updatedTorrents.contains(torrentID))
            continue;

        const BitTorrent::Torrent *torrent = session->getTorrent(torrentID);
        Q_ASSERT(torrent);

        const QString torrentIDStr = torrentID.toString();
        auto &torrentSnapshot = m_snapshot.torrents[torrentIDStr];

        // Only announce stats are changed so don't need to serialize torrent again
        QVariantMap serializedTorrent = torrentSnapshot;
        addAnnounceStats(serializedTorrent, torrent);

        if (const QVariantMap changes = diffMap(torrentSnapshot, serializedTorrent); !changes.isEmpty())
        {
            delta.torrents[torrentIDStr] = changes;
            torrentSnapshot = serializedTorrent;
        }
    }

    m_updatedTorrents.clear();
    m_announcedTorrents.clear();

    for (const BitTorrent::TorrentID &torrentID : asConst(m_removedTorrents))
    {
        const QString torrentIDStr = torrentID.toString();

        delta.removedTorrents.append(torrentIDStr);
        m_snapshot.torrents.remove(torrentIDStr);
    }
    m_removedTorrents.clear();

    for (const QString &tracker : asConst(m_updatedTrackers))
    {
        const QStringList serializedTorrentIDs = asStrings(m_knownTrackers[tracker]);

        delta.trackers[tracker] = serializedTorrentIDs;
        m_snapshot.trackers[tracker] = serializedTorrentIDs;
    }
    m_updatedTrackers.clear();

    for (const QString &tracker : asConst(m_removedTrackers))
    {
        delta.removedTrackers.append(tracker);
        m_snapshot.trackers.remove(tracker);
    }
    m_removedTrackers.clear();

    QVariantMap serverState = getTransferInfo();
    serverState[KEY_TRANSFER_FREESPACEONDISK] = m_freeDiskSpace;
    serverState[KEY_SYNC_MAINDATA_QUEUEING] = session->isQueueingSystemEnabled();
    serverState[KEY_SYNC_MAINDATA_USE_ALT_SPEED_LIMITS] = session->isAltGlobalSpeedLimitEnabled();
    serverState[KEY_SYNC_MAINDATA_REFRESH_INTERVAL] = session->refreshInterval();
    if (const QVariantMap changes = diffMap(m_snapshot.serverState, serverState); !changes.isEmpty())
    {
        delta.serverState = changes;
        m_snapshot.serverState = serverState;
    }

    const bool isDeltaEmpty = delta.categories.isEmpty() && delta.removedCategories.isEmpty()
            && delta.tags.isEmpty() && delta.removedTags.isEmpty()
            && delta.torrents.isEmpty() && delta.removedTorrents.isEmpty()
            && delta.trackers.isEmpty() && delta.removedTrackers.isEmpty()
            && delta.serverState.isEmpty();
    if (!isDeltaEmpty)
        emit deltaProduced(delta);
}

void MaindataStore::updateFreeDiskSpace(const qint64 freeDiskSpace)
{
    m_freeDiskSpace = freeDiskSpace;
}

void MaindataStore::onCategoryAdded(const QString &categoryName)
{
    m_removedCategories.remove(categoryName);
    m_updatedCategories.insert(categoryName);
}

void MaindataStore::onCategoryRemoved(const QString &categoryName)
{
    m_updatedCategories.remove(categoryName);
    m_removedCategories.insert(categoryName);
}

void MaindataStore::onCategoryOptionsChanged(const QString &categoryName)
{
    Q_ASSERT(!m_removedCategories.contains(categoryName));

    m_updatedCategories.insert(categoryName);
}

void MaindataStore::onSubcategoriesSupportChanged()
{
    const QStringList categoriesList = BitTorrent::Session::instance()->categories();
    for (const auto &categoryName : categoriesList)
    {
        if (!m_snapshot.categories.contains(categoryName))
        {
            m_removedCategories.remove(categoryName);
            m_updatedCategories.insert(categoryName);
        }
    }
}

void MaindataStore::onTagAdded(const Tag &tag)
{
    m_removedTags.remove(tag.toString());
    m_addedTags.insert(tag.toString());
}

void MaindataStore::onTagRemoved(const Tag &tag)
{
    m_addedTags.remove(tag.toString());
    m_removedTags.insert(tag.toString());
}

void MaindataStore::onTorrentAdded(BitTorrent::Torrent *torrent)
{
    const BitTorrent::TorrentID torrentID = torrent->id();

    m_removedTorrents.remove(torrentID);
    m_updatedTorrents.insert(torrentID);
    m_announcedTorrents.insert(torrentID);

    for (const BitTorrent::TrackerEntryStatus &status : asConst(torrent->trackers()))
    {
        m_knownTrackers[status.url].insert(torrentID);
        m_updatedTrackers.insert(status.url);
        m_removedTrackers.remove(status.url);
    }
}

void MaindataStore::onTorrentAboutToBeRemoved(BitTorrent::Torrent *torrent)
{
    const BitTorrent::TorrentID torrentID = torrent->id();

    m_announcedTorrents.remove(torrentID);
    m_updatedTorrents.remove(torrentID);
    m_removedTorrents.insert(torrentID);

    for (const BitTorrent::TrackerEntryStatus &status : asConst(torrent->trackers()))
    {
        const auto iter = m_knownTrackers.find(status.url);
        Q_ASSERT(iter != m_knownTrackers.end());
        if (iter == m_knownTrackers.end()) [[unlikely]]
            continue;

        QSet<BitTorrent::TorrentID> &torrentIDs = iter.value();
        torrentIDs.remove(torrentID);
        if (torrentIDs.isEmpty())
        {
            m_knownTrackers.erase(iter);
            m_updatedTrackers.remove(status.url);
            m_removedTrackers.insert(status.url);
        }
        else
        {
            m_updatedTrackers.insert(status.url);
        }
    }
}

void MaindataStore::onTorrentCategoryChanged(BitTorrent::Torrent *torrent
        , [[maybe_unused]] const QString &oldCategory)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentMetadataReceived(BitTorrent::Torrent *torrent)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentStopped(BitTorrent::Torrent *torrent)
{
    m_updatedTorrents.insert(torrent->id());
    m_announcedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentStarted(BitTorrent::Torrent *torrent)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentSavePathChanged(BitTorrent::Torrent *torrent)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentSavingModeChanged(BitTorrent::Torrent *torrent)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentTagAdded(BitTorrent::Torrent *torrent, [[maybe_unused]] const Tag &tag)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentTagRemoved(BitTorrent::Torrent *torrent, [[maybe_unused]] const Tag &tag)
{
    m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentsUpdated(const QList<BitTorrent::Torrent *> &torrents)
{
    for (const BitTorrent::Torrent *torrent : torrents)
        m_updatedTorrents.insert(torrent->id());
}

void MaindataStore::onTorrentTrackersChanged(BitTorrent::Torrent *torrent)
{
    using namespace BitTorrent;

    const QList<TrackerEntryStatus> trackers = torrent->trackers();

    QSet<QString> currentTrackers;
    currentTrackers.reserve(trackers.size());
    for (const TrackerEntryStatus &status : trackers)
        currentTrackers.insert(status.url);

    const TorrentID torrentID = torrent->id();
    Algorithm::removeIf(m_knownTrackers
        , [this, torrentID, currentTrackers](const QString &knownTracker, QSet<TorrentID> &torrentIDs)
    {
        if (auto idIter = torrentIDs.find(torrentID)
                ; (idIter != torrentIDs.end()) && !currentTrackers.contains(knownTracker))
        {
            torrentIDs.erase(idIter);
            if (torrentIDs.isEmpty())
            {
                m_updatedTrackers.remove(knownTracker);
                m_removedTrackers.insert(knownTracker);
                return true;
            }

            m_updatedTrackers.insert(knownTracker);
            return false;
        }

        if (currentTrackers.contains(knownTracker) && !torrentIDs.contains(torrentID))
        {
            torrentIDs.insert(torrentID);
            m_updatedTrackers.insert(knownTracker);
            return false;
        }

        return false;
    });

    for (const QString &currentTracker : asConst(currentTrackers))
    {
        if (!m_knownTrackers.contains(currentTracker))
        {
            m_knownTrackers.insert(currentTracker, {torrentID});
            m_updatedTrackers.insert(currentTracker);
            m_removedTrackers.remove(currentTracker);
        }
    }

    m_announcedTorrents.insert(torrentID);
}

void MaindataStore::onTorrentTrackerEntryStatusesUpdated(const BitTorrent::Torrent *torrent
        , [[maybe_unused]] const QHash<QString, BitTorrent::TrackerEntryStatus> &updatedTrackers)
{
    m_announcedTorrents.insert(torrent->id());
}
