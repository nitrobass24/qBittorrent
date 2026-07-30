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

#pragma once

#include <QVariantMap>

#include "apicontroller.h"
#include "maindatastore.h"

class SyncController : public APIController
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SyncController)

public:
    SyncController(MaindataStore *maindataStore, IApplication *app, QObject *parent = nullptr);

private slots:
    void maindataAction();
    void torrentPeersAction();

private:
    void onMaindataDeltaProduced(const MaindataStore::Data &delta);
    QJsonObject generateMaindataSyncData(int id, bool fullUpdate);

    MaindataStore *m_maindataStore = nullptr;

    QVariantMap m_lastPeersResponse;
    QVariantMap m_lastAcceptedPeersResponse;

    // changes sent in the last response, kept until the client acknowledges them
    MaindataStore::Data m_maindataUnackedBuf;
    // changes received from the store since the last response was built
    MaindataStore::Data m_maindataUnsentBuf;
    int m_maindataLastSentID = 0;
    int m_maindataAcceptedID = -1;
};
