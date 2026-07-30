/*
 * Bittorrent Client using Qt and libtorrent.
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

#include "synccontroller.h"

#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

#include "base/bittorrent/infohash.h"
#include "base/bittorrent/peeraddress.h"
#include "base/bittorrent/peerinfo.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/bittorrent/torrentinfo.h"
#include "base/global.h"
#include "base/net/geoipmanager.h"
#include "base/net/reverseresolution.h"
#include "base/preferences.h"
#include "apierror.h"

namespace
{
    // Sync torrent peers keys
    const QString KEY_SYNC_TORRENT_PEERS_SHOW_FLAGS = u"show_flags"_s;

    // Peer keys
    const QString KEY_PEER_CLIENT = u"client"_s;
    const QString KEY_PEER_ID_CLIENT = u"peer_id_client"_s;
    const QString KEY_PEER_CONNECTION_TYPE = u"connection"_s;
    const QString KEY_PEER_COUNTRY = u"country"_s;
    const QString KEY_PEER_COUNTRY_CODE = u"country_code"_s;
    const QString KEY_PEER_DOWN_SPEED = u"dl_speed"_s;
    const QString KEY_PEER_FILES = u"files"_s;
    const QString KEY_PEER_FLAGS = u"flags"_s;
    const QString KEY_PEER_FLAGS_DESCRIPTION = u"flags_desc"_s;
    const QString KEY_PEER_HOST_NAME = u"host_name"_s;
    const QString KEY_PEER_IP = u"ip"_s;
    const QString KEY_PEER_I2P_DEST = u"i2p_dest"_s;
    const QString KEY_PEER_PORT = u"port"_s;
    const QString KEY_PEER_PROGRESS = u"progress"_s;
    const QString KEY_PEER_RELEVANCE = u"relevance"_s;
    const QString KEY_PEER_CONTRIBUTION = u"contribution"_s;
    const QString KEY_PEER_TOT_DOWN = u"downloaded"_s;
    const QString KEY_PEER_TOT_UP = u"uploaded"_s;
    const QString KEY_PEER_UP_SPEED = u"up_speed"_s;

    const QString KEY_SUFFIX_REMOVED = u"_removed"_s;

    const QString KEY_CATEGORIES = u"categories"_s;
    const QString KEY_CATEGORIES_REMOVED = KEY_CATEGORIES + KEY_SUFFIX_REMOVED;
    const QString KEY_TAGS = u"tags"_s;
    const QString KEY_TAGS_REMOVED = KEY_TAGS + KEY_SUFFIX_REMOVED;
    const QString KEY_TORRENTS = u"torrents"_s;
    const QString KEY_TORRENTS_REMOVED = KEY_TORRENTS + KEY_SUFFIX_REMOVED;
    const QString KEY_TRACKERS = u"trackers"_s;
    const QString KEY_TRACKERS_REMOVED = KEY_TRACKERS + KEY_SUFFIX_REMOVED;
    const QString KEY_SERVER_STATE = u"server_state"_s;
    const QString KEY_FULL_UPDATE = u"full_update"_s;
    const QString KEY_RESPONSE_ID = u"rid"_s;

    QVariantMap processMap(const QVariantMap &prevData, const QVariantMap &data);
    std::pair<QVariantMap, QVariantList> processHash(QVariantHash prevData, const QVariantHash &data);
    std::pair<QVariantList, QVariantList> processList(QVariantList prevData, const QVariantList &data);
    QJsonObject generateSyncData(int acceptedResponseId, const QVariantMap &data, QVariantMap &lastAcceptedData, QVariantMap &lastData);

    // Compare two structures (prevData, data) and calculate difference (syncData).
    // Structures encoded as map.
    QVariantMap processMap(const QVariantMap &prevData, const QVariantMap &data)
    {
        // initialize output variable
        QVariantMap syncData;

        for (auto i = data.cbegin(); i != data.cend(); ++i)
        {
            const QString &key = i.key();
            const QVariant &value = i.value();

            switch (value.userType())
            {
            case QMetaType::QVariantMap:
                {
                    const QVariantMap map = processMap(prevData[key].toMap(), value.toMap());
                    if (!map.isEmpty())
                        syncData[key] = map;
                }
                break;
            case QMetaType::QVariantHash:
                {
                    const auto [map, removedItems] = processHash(prevData[key].toHash(), value.toHash());
                    if (!map.isEmpty())
                        syncData[key] = map;
                    if (!removedItems.isEmpty())
                        syncData[key + KEY_SUFFIX_REMOVED] = removedItems;
                }
                break;
            case QMetaType::QVariantList:
                {
                    const auto [list, removedItems] = processList(prevData[key].toList(), value.toList());
                    if (!list.isEmpty())
                        syncData[key] = list;
                    if (!removedItems.isEmpty())
                        syncData[key + KEY_SUFFIX_REMOVED] = removedItems;
                }
                break;
            case QMetaType::QString:
            case QMetaType::LongLong:
            case QMetaType::Float:
            case QMetaType::Int:
            case QMetaType::Bool:
            case QMetaType::Double:
            case QMetaType::ULongLong:
            case QMetaType::UInt:
            case QMetaType::QDateTime:
            case QMetaType::Nullptr:
            case QMetaType::UnknownType:
                if (prevData[key] != value)
                    syncData[key] = value;
                break;
            default:
                Q_ASSERT_X(false, "processMap"
                        , u"Unexpected type: %1"_s.arg(QString::fromLatin1(value.metaType().name()))
                                .toUtf8().constData());
            }
        }

        return syncData;
    }

    // Compare two lists of structures (prevData, data) and calculate difference (syncData, removedItems).
    // Structures encoded as map.
    // Lists are encoded as hash table (indexed by structure key value) to improve ease of searching for removed items.
    std::pair<QVariantMap, QVariantList> processHash(QVariantHash prevData, const QVariantHash &data)
    {
        // initialize output variables
        std::pair<QVariantMap, QVariantList> result;
        auto &[syncData, removedItems] = result;

        if (prevData.isEmpty())
        {
            // If list was empty before, then difference is a whole new list.
            for (auto i = data.cbegin(); i != data.cend(); ++i)
                syncData[i.key()] = i.value();
        }
        else
        {
            for (auto i = data.cbegin(); i != data.cend(); ++i)
            {
                switch (i.value().userType())
                {
                case QMetaType::QVariantMap:
                    if (!prevData.contains(i.key()))
                    {
                        // new list item found - append it to syncData
                        syncData[i.key()] = i.value();
                    }
                    else
                    {
                        const QVariantMap map = processMap(prevData[i.key()].toMap(), i.value().toMap());
                        // existing list item found - remove it from prevData
                        prevData.remove(i.key());
                        if (!map.isEmpty())
                        {
                            // changed list item found - append its changes to syncData
                            syncData[i.key()] = map;
                        }
                    }
                    break;
                case QMetaType::QStringList:
                    if (!prevData.contains(i.key()))
                    {
                        // new list item found - append it to syncData
                        syncData[i.key()] = i.value();
                    }
                    else
                    {
                        const auto [list, removedList] = processList(prevData[i.key()].toList(), i.value().toList());
                        // existing list item found - remove it from prevData
                        prevData.remove(i.key());
                        if (!list.isEmpty() || !removedList.isEmpty())
                        {
                            // changed list item found - append entire list to syncData
                            syncData[i.key()] = i.value();
                        }
                    }
                    break;
                default:
                    Q_UNREACHABLE();
                    break;
                }
            }

            if (!prevData.isEmpty())
            {
                // prevData contains only items that are missing now -
                // put them in removedItems
                for (auto i = prevData.cbegin(); i != prevData.cend(); ++i)
                    removedItems << i.key();
            }
        }

        return result;
    }

    // Compare two lists of simple value (prevData, data) and calculate difference (syncData, removedItems).
    std::pair<QVariantList, QVariantList> processList(QVariantList prevData, const QVariantList &data)
    {
        // initialize output variables
        std::pair<QVariantList, QVariantList> result;
        auto &[syncData, removedItems] = result;

        if (prevData.isEmpty())
        {
            // If list was empty before, then difference is a whole new list.
            syncData = data;
        }
        else
        {
            for (const QVariant &item : data)
            {
                if (!prevData.contains(item))
                {
                    // new list item found - append it to syncData
                    syncData.append(item);
                }
                else
                {
                    // unchanged list item found - remove it from prevData
                    prevData.removeOne(item);
                }
            }

            if (!prevData.isEmpty())
            {
                // prevData contains only items that are missing now -
                // put them in removedItems
                removedItems = prevData;
            }
        }

        return result;
    }

    QJsonObject generateSyncData(int acceptedResponseId, const QVariantMap &data, QVariantMap &lastAcceptedData, QVariantMap &lastData)
    {
        QVariantMap syncData;
        bool fullUpdate = true;
        const int lastResponseId = (acceptedResponseId > 0) ? lastData[KEY_RESPONSE_ID].toInt() : 0;
        if (lastResponseId > 0)
        {
            if (lastResponseId == acceptedResponseId)
                lastAcceptedData = lastData;

            if (const int lastAcceptedResponseId = lastAcceptedData[KEY_RESPONSE_ID].toInt()
                    ; lastAcceptedResponseId == acceptedResponseId)
            {
                fullUpdate = false;
            }
        }

        if (fullUpdate)
        {
            lastAcceptedData.clear();
            syncData = data;
            syncData[KEY_FULL_UPDATE] = true;
        }
        else
        {
            syncData = processMap(lastAcceptedData, data);
        }

        const int responseId = (lastResponseId % 1000000) + 1;  // cycle between 1 and 1000000
        lastData = data;
        lastData[KEY_RESPONSE_ID] = responseId;
        syncData[KEY_RESPONSE_ID] = responseId;

        return QJsonObject::fromVariantMap(syncData);
    }

    // Merge `delta` into `target` so that `target` remains a consistent set of
    // changes relative to the state the client has acknowledged.
    void mergeData(MaindataStore::Data &target, const MaindataStore::Data &delta)
    {
        for (auto it = delta.categories.cbegin(); it != delta.categories.cend(); ++it)
        {
            target.removedCategories.removeOne(it.key());
            QVariantMap &categoryChanges = target.categories[it.key()];
            for (auto changeIt = it.value().cbegin(); changeIt != it.value().cend(); ++changeIt)
                categoryChanges.insert(changeIt.key(), changeIt.value());
        }
        for (const QString &category : delta.removedCategories)
        {
            target.categories.remove(category);
            if (!target.removedCategories.contains(category))
                target.removedCategories.append(category);
        }

        for (const QVariant &tag : delta.tags)
        {
            target.removedTags.removeOne(tag.toString());
            if (!target.tags.contains(tag))
                target.tags.append(tag);
        }
        for (const QString &tag : delta.removedTags)
        {
            target.tags.removeOne(tag);
            if (!target.removedTags.contains(tag))
                target.removedTags.append(tag);
        }

        for (auto it = delta.torrents.cbegin(); it != delta.torrents.cend(); ++it)
        {
            target.removedTorrents.removeOne(it.key());
            QVariantMap &torrentChanges = target.torrents[it.key()];
            for (auto changeIt = it.value().cbegin(); changeIt != it.value().cend(); ++changeIt)
                torrentChanges.insert(changeIt.key(), changeIt.value());
        }
        for (const QString &torrentID : delta.removedTorrents)
        {
            target.torrents.remove(torrentID);
            if (!target.removedTorrents.contains(torrentID))
                target.removedTorrents.append(torrentID);
        }

        for (auto it = delta.trackers.cbegin(); it != delta.trackers.cend(); ++it)
        {
            target.removedTrackers.removeOne(it.key());
            target.trackers[it.key()] = it.value();
        }
        for (const QString &tracker : delta.removedTrackers)
        {
            target.trackers.remove(tracker);
            if (!target.removedTrackers.contains(tracker))
                target.removedTrackers.append(tracker);
        }

        for (auto it = delta.serverState.cbegin(); it != delta.serverState.cend(); ++it)
            target.serverState.insert(it.key(), it.value());
    }
}

SyncController::SyncController(MaindataStore *maindataStore, IApplication *app, QObject *parent)
    : APIController(app, parent)
    , m_maindataStore {maindataStore}
{
}

// The function returns the changed data from the server to synchronize with the web client.
// Return value is map in JSON format.
// Map contain the key:
//  - "Rid": ID response
// Map can contain the keys:
//  - "full_update": full data update flag
//  - "torrents": dictionary contains information about torrents.
//  - "torrents_removed": a list of hashes of removed torrents
//  - "categories": map of categories info
//  - "categories_removed": list of removed categories
//  - "trackers": dictionary contains information about trackers
//  - "trackers_removed": a list of removed trackers
//  - "server_state": map contains information about the state of the server
// The keys of the 'torrents' dictionary are hashes of torrents.
// Each value of the 'torrents' dictionary contains map. The map can contain following keys:
//  - "name": Torrent name
//  - "size": Torrent size
//  - "progress": Torrent progress
//  - "dlspeed": Torrent download speed
//  - "upspeed": Torrent upload speed
//  - "priority": Torrent queue position (-1 if queuing is disabled)
//  - "num_seeds": Torrent seeds connected to
//  - "num_complete": Torrent seeds in the swarm
//  - "num_leechs": Torrent leechers connected to
//  - "num_incomplete": Torrent leechers in the swarm
//  - "ratio": Torrent share ratio
//  - "eta": Torrent ETA
//  - "state": Torrent state
//  - "seq_dl": Torrent sequential download state
//  - "f_l_piece_prio": Torrent first last piece priority state
//  - "completion_on": Torrent copletion time
//  - "tracker": Torrent tracker
//  - "dl_limit": Torrent download limit
//  - "up_limit": Torrent upload limit
//  - "downloaded": Amount of data downloaded
//  - "uploaded": Amount of data uploaded
//  - "downloaded_session": Amount of data downloaded since program open
//  - "uploaded_session": Amount of data uploaded since program open
//  - "amount_left": Amount of data left to download
//  - "save_path": Torrent save path
//  - "download_path": Torrent download path
//  - "completed": Amount of data completed
//  - "max_ratio": Upload max share ratio
//  - "max_seeding_time": Upload max seeding time
//  - "ratio_limit": Upload share ratio limit
//  - "seeding_time_limit": Upload seeding time limit
//  - "share_limit_action": Action to execute when the limit is reached
//  - "seen_complete": Indicates the time when the torrent was last seen complete/whole
//  - "last_activity": Last time when a chunk was downloaded/uploaded
//  - "total_size": Size including unwanted data
//  - "has_tracker_warning": the torrent has working tracker that has a message
//  - "has_tracker_error": the torrent has a tracker error
//  - "has_other_announce_error": the torrent has other problems announcing to a tracker
// Server state map may contain the following keys:
//  - "connection_status": connection status
//  - "dht_nodes": DHT nodes count
//  - "dl_info_data": bytes downloaded
//  - "dl_info_speed": download speed
//  - "dl_rate_limit: download rate limit
//  - "last_external_address_v4": last external address v4
//  - "last_external_address_v6": last external address v6
//  - "up_info_data: bytes uploaded
//  - "up_info_speed: upload speed
//  - "up_rate_limit: upload speed limit
//  - "queueing": queue system usage flag
//  - "refresh_interval": torrents table refresh interval
//  - "free_space_on_disk": Free space on the default save path
// GET param:
//   - rid (int): last response id
void SyncController::maindataAction()
{
    if (m_maindataAcceptedID < 0)
    {
        m_maindataAcceptedID = 0;
        connect(m_maindataStore, &MaindataStore::deltaProduced, this, &SyncController::onMaindataDeltaProduced);
    }

    const int acceptedID = params()[u"rid"_s].toInt();
    bool fullUpdate = true;
    if ((acceptedID > 0) && (m_maindataLastSentID > 0))
    {
        if (m_maindataLastSentID == acceptedID)
        {
            m_maindataAcceptedID = acceptedID;
            m_maindataUnackedBuf = {};
        }

        if (m_maindataAcceptedID == acceptedID)
        {
            // We are still able to send changes for the current state of the data having by client.
            fullUpdate = false;
        }
    }

    // serialize changes accumulated since the last request of any session
    // (the resulting delta reaches us through onMaindataDeltaProduced())
    m_maindataStore->flush();

    const int id = (m_maindataLastSentID % 1000000) + 1;  // cycle between 1 and 1000000
    setResult(generateMaindataSyncData(id, fullUpdate));
    m_maindataLastSentID = id;
}

void SyncController::onMaindataDeltaProduced(const MaindataStore::Data &delta)
{
    mergeData(m_maindataUnsentBuf, delta);
}

QJsonObject SyncController::generateMaindataSyncData(const int id, const bool fullUpdate)
{
    if (fullUpdate)
    {
        m_maindataUnackedBuf = m_maindataStore->snapshot();
    }
    else
    {
        mergeData(m_maindataUnackedBuf, m_maindataUnsentBuf);
    }
    m_maindataUnsentBuf = {};

    const MaindataStore::Data &syncBuf = m_maindataUnackedBuf;

    QJsonObject syncData;
    syncData[KEY_RESPONSE_ID] = id;
    if (fullUpdate)
        syncData[KEY_FULL_UPDATE] = true;

    if (!syncBuf.categories.isEmpty())
    {
        QJsonObject categories;
        for (auto it = syncBuf.categories.cbegin(); it != syncBuf.categories.cend(); ++it)
            categories[it.key()] = QJsonObject::fromVariantMap(it.value());
        syncData[KEY_CATEGORIES] = categories;
    }
    if (!syncBuf.removedCategories.isEmpty())
        syncData[KEY_CATEGORIES_REMOVED] = QJsonArray::fromStringList(syncBuf.removedCategories);

    if (!syncBuf.tags.isEmpty())
        syncData[KEY_TAGS] = QJsonArray::fromVariantList(syncBuf.tags);
    if (!syncBuf.removedTags.isEmpty())
        syncData[KEY_TAGS_REMOVED] = QJsonArray::fromStringList(syncBuf.removedTags);

    if (!syncBuf.torrents.isEmpty())
    {
        QJsonObject torrents;
        for (auto it = syncBuf.torrents.cbegin(); it != syncBuf.torrents.cend(); ++it)
            torrents[it.key()] = QJsonObject::fromVariantMap(it.value());
        syncData[KEY_TORRENTS] = torrents;
    }
    if (!syncBuf.removedTorrents.isEmpty())
        syncData[KEY_TORRENTS_REMOVED] = QJsonArray::fromStringList(syncBuf.removedTorrents);

    if (!syncBuf.trackers.isEmpty())
    {
        QJsonObject trackers;
        for (auto it = syncBuf.trackers.cbegin(); it != syncBuf.trackers.cend(); ++it)
            trackers[it.key()] = QJsonArray::fromStringList(it.value());
        syncData[KEY_TRACKERS] = trackers;
    }
    if (!syncBuf.removedTrackers.isEmpty())
        syncData[KEY_TRACKERS_REMOVED] = QJsonArray::fromStringList(syncBuf.removedTrackers);

    if (!syncBuf.serverState.isEmpty())
        syncData[KEY_SERVER_STATE] = QJsonObject::fromVariantMap(syncBuf.serverState);

    return syncData;
}

// GET param:
//   - hash (string): torrent hash (ID)
//   - rid (int): last response id
void SyncController::torrentPeersAction()
{
    const auto id = BitTorrent::TorrentID::fromString(params()[u"hash"_s]);
    const BitTorrent::Torrent *torrent = BitTorrent::Session::instance()->getTorrent(id);
    if (!torrent)
        throw APIError(APIErrorType::NotFound);

    QVariantMap data;
    QVariantHash peers;

    const QList<BitTorrent::PeerInfo> peersList = torrent->fetchPeerInfo().takeResult();

    const auto *pref = Preferences::instance();
    const bool resolvePeerHostNames = pref->resolvePeerHostNames();
    const bool resolvePeerCountries = pref->resolvePeerCountries();

    data[KEY_SYNC_TORRENT_PEERS_SHOW_FLAGS] = resolvePeerCountries;

    for (const BitTorrent::PeerInfo &pi : peersList)
    {
        const BitTorrent::PeerAddress address = pi.address();
        const bool useI2PSocket = pi.useI2PSocket();

        if (address.ip.isNull() && !useI2PSocket)
            continue;

        QVariantMap peer =
        {
            {KEY_PEER_CLIENT, pi.client()},
            {KEY_PEER_ID_CLIENT, pi.peerIdClient()},
            {KEY_PEER_PROGRESS, pi.progress()},
            {KEY_PEER_DOWN_SPEED, pi.payloadDownSpeed()},
            {KEY_PEER_UP_SPEED, pi.payloadUpSpeed()},
            {KEY_PEER_TOT_DOWN, pi.totalDownload()},
            {KEY_PEER_TOT_UP, pi.totalUpload()},
            {KEY_PEER_CONNECTION_TYPE, pi.connectionType()},
            {KEY_PEER_FLAGS, pi.flags()},
            {KEY_PEER_FLAGS_DESCRIPTION, pi.flagsDescription()},
            {KEY_PEER_RELEVANCE, pi.relevance()}
        };

        const qlonglong totalUpload = pi.totalUpload();
        qreal contribution = 0;

        if (totalUpload > 0)
        {
            const qlonglong totalSize = (torrent->totalSize() <= 0) ? totalUpload : torrent->totalSize();
            const qreal progressBytes = pi.progress() * totalSize;
            contribution = static_cast<qreal>(totalUpload) / ((progressBytes <= 0) ? totalSize : progressBytes);
        }

        peer[KEY_PEER_CONTRIBUTION] = contribution;

        if (torrent->hasMetadata())
        {
            const PathList filePaths = torrent->info().filesForPiece(pi.downloadingPieceIndex());
            QStringList filesForPiece;
            filesForPiece.reserve(filePaths.size());
            for (const Path &filePath : filePaths)
                filesForPiece.append(filePath.toString());
            peer.insert(KEY_PEER_FILES, filesForPiece.join(u'\n'));
        }

        if (useI2PSocket)
        {
            const QString i2pAddress = pi.I2PAddress();
            peer[KEY_PEER_I2P_DEST] = i2pAddress;
            peers[i2pAddress] = peer;
        }
        else
        {
            peer[KEY_PEER_IP] = address.ip.toString();
            peer[KEY_PEER_PORT] = address.port;

            peer[KEY_PEER_HOST_NAME] = resolvePeerHostNames
                ? Net::ReverseResolution::instance()->resolve(address.ip)
                : QString();

            if (resolvePeerCountries)
            {
                const QString country = pi.country();
                peer[KEY_PEER_COUNTRY_CODE] = country.toLower();
                peer[KEY_PEER_COUNTRY] = Net::GeoIPManager::CountryName(country);
            }
            else
            {
                peer[KEY_PEER_COUNTRY_CODE] = {};
                peer[KEY_PEER_COUNTRY] = {};
            }

            peers[address.toString()] = peer;
        }
    }
    data[u"peers"_s] = peers;

    const int acceptedResponseId = params()[u"rid"_s].toInt();
    setResult(generateSyncData(acceptedResponseId, data, m_lastAcceptedPeersResponse, m_lastPeersResponse));
}
