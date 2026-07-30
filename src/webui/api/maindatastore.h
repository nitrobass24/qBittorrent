/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2026  nitrobass24
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

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "base/bittorrent/infohash.h"
#include "base/tag.h"

namespace BitTorrent
{
    class Torrent;
    struct TrackerEntryStatus;
}

// Application-wide storage of the "sync/maindata" state. It maintains one
// serialized snapshot of the session (torrents, categories, tags, trackers,
// server state), updated incrementally from Session events. Serializing and
// diffing changed torrents is done once here and the resulting changes are
// broadcast to all subscribed web sessions, instead of every session paying
// for its own snapshot and its own serialization of the same changes.
class MaindataStore final : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MaindataStore)

public:
    struct Data
    {
        QHash<QString, QVariantMap> categories;
        QStringList removedCategories;

        QVariantList tags;
        QStringList removedTags;

        QHash<QString, QVariantMap> torrents;
        QStringList removedTorrents;

        QHash<QString, QStringList> trackers;
        QStringList removedTrackers;

        QVariantMap serverState;
    };

    explicit MaindataStore(QObject *parent = nullptr);

    // Serializes pending changes, applies them to the snapshot and emits
    // `deltaProduced()` if there was anything to apply.
    void flush();

    const Data &snapshot() const;

signals:
    void deltaProduced(const Data &delta);

public slots:
    void updateFreeDiskSpace(qint64 freeDiskSpace);

private:
    void makeSnapshot();

    void onCategoryAdded(const QString &categoryName);
    void onCategoryRemoved(const QString &categoryName);
    void onCategoryOptionsChanged(const QString &categoryName);
    void onSubcategoriesSupportChanged();
    void onTagAdded(const Tag &tag);
    void onTagRemoved(const Tag &tag);
    void onTorrentAdded(BitTorrent::Torrent *torrent);
    void onTorrentAboutToBeRemoved(BitTorrent::Torrent *torrent);
    void onTorrentCategoryChanged(BitTorrent::Torrent *torrent, const QString &oldCategory);
    void onTorrentMetadataReceived(BitTorrent::Torrent *torrent);
    void onTorrentStopped(BitTorrent::Torrent *torrent);
    void onTorrentStarted(BitTorrent::Torrent *torrent);
    void onTorrentSavePathChanged(BitTorrent::Torrent *torrent);
    void onTorrentSavingModeChanged(BitTorrent::Torrent *torrent);
    void onTorrentTagAdded(BitTorrent::Torrent *torrent, const Tag &tag);
    void onTorrentTagRemoved(BitTorrent::Torrent *torrent, const Tag &tag);
    void onTorrentsUpdated(const QList<BitTorrent::Torrent *> &torrents);
    void onTorrentTrackersChanged(BitTorrent::Torrent *torrent);
    void onTorrentTrackerEntryStatusesUpdated(const BitTorrent::Torrent *torrent
            , const QHash<QString, BitTorrent::TrackerEntryStatus> &updatedTrackers);

    qint64 m_freeDiskSpace = 0;

    QHash<QString, QSet<BitTorrent::TorrentID>> m_knownTrackers;

    QSet<QString> m_updatedCategories;
    QSet<QString> m_removedCategories;
    QSet<QString> m_addedTags;
    QSet<QString> m_removedTags;
    QSet<QString> m_updatedTrackers;
    QSet<QString> m_removedTrackers;
    QSet<BitTorrent::TorrentID> m_updatedTorrents;
    QSet<BitTorrent::TorrentID> m_announcedTorrents;
    QSet<BitTorrent::TorrentID> m_removedTorrents;

    Data m_snapshot;
};
