/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "space.h"

#include "fonticon.h"
#include "libsync/account.h"
#include "libsync/graphapi/spacesmanager.h"
#include "libsync/networkjobs.h"
#include "libsync/networkjobs/resources.h"

#include "resources/resources.h"

using namespace OCC;
using namespace GraphApi;

namespace {

const auto personalC = QLatin1String("personal");

// https://github.com/cs3org/reva/blob/0cde0a3735beaa14ebdfd8988c3eb77b3c2ab0e6/pkg/utils/utils.go#L56-L59
const auto sharesIdC = QLatin1String("a0ca6a90-a365-4782-871e-d44447bbc668$a0ca6a90-a365-4782-871e-d44447bbc668");
}

Space::Space(SpacesManager *spacesManager, const QtOpenAPI::Drive &drive)
    : QObject(spacesManager)
    , _spaceManager(spacesManager)
    , _image(new SpaceImage(this))
{
    setDrive(drive);
    connect(_image, &SpaceImage::imageChanged, this, &Space::imageChanged);
}

QtOpenAPI::Drive Space::drive() const
{
    return _drive;
}

void Space::setDrive(const QtOpenAPI::Drive &drive)
{
    _drive = drive;
    _image->update();
}

SpaceImage::SpaceImage(Space *space)
    : QObject(space)
    , _space(space)
{
}

QIcon SpaceImage::image() const
{
    if (_image.isNull()) {
        // remix icons is not compatible with nerdfonts so the preview will be broken
        if (_space->drive().getDriveTypeValue() == personalC) {
            return Resources::FontIcon(Resources::FontIcon::FontFamily::RemixIcon, u'', Resources::FontIcon::Size::Half);
        } else if (_space->drive().getIdValue() == sharesIdC) {
            return Resources::FontIcon(Resources::FontIcon::FontFamily::RemixIcon, u'', Resources::FontIcon::Size::Half);
        }
        return Resources::FontIcon(Resources::FontIcon::FontFamily::RemixIcon, u'', Resources::FontIcon::Size::Half);
    }
    return _image;
}

QUrl SpaceImage::qmlImageUrl() const
{
    // while the icon is being loaded we provide a different url, qml caches the icon based on the url
    if (_fetched) {
        return QUrl(QStringLiteral("image://space/%1/%2").arg(etag(), _space->id()));
    } else {
        return QUrl(QStringLiteral("image://space/invalid/%1").arg(_space->id()));
    }
}

void SpaceImage::update()
{
    const auto &special = _space->drive().getSpecialValue();
    const auto img =
        std::find_if(special.cbegin(), special.cend(), [](const auto &it) { return it.getSpecialFolderValue().getNameValue() == QLatin1String("image"); });
    if (img != special.cend()) {
        _fetched = false;
        _url = QUrl(img->getWebDavUrlValue());
        _etag = Utility::normalizeEtag(img->getETagValue());
        auto job = _space->_spaceManager->account()->resourcesCache()->makeGetJob(_url, {}, _space);
        QObject::connect(job, &SimpleNetworkJob::finishedSignal, _space, [job, this] {
            _fetched = true;
            if (job->httpStatusCode() == 200) {
                _image = job->asIcon();
                Q_EMIT imageChanged();
            }
        });
        job->start();
    } else {
        // nothing to fetch
        _fetched = true;
    }
}

QString Space::displayName() const
{
    if (_drive.getDriveTypeValue() == personalC) {
        return tr("Personal");
    } else if (_drive.getIdValue() == sharesIdC) {
        // don't call it ShareJail
        return tr("Shares");
    }
    return _drive.getNameValue();
}

uint32_t Space::priority() const
{
    if (_drive.getDriveTypeValue() == personalC) {
        return 100;
    } else if (_drive.getIdValue() == sharesIdC) {
        return 50;
    }
    return 0;
}

bool Space::disabled() const
{
    // this is how disabled spaces are represented in the graph API
    return _drive.getRootValue().getDeletedValue().getStateValue() == QLatin1String("trashed");
}

SpaceImage *Space::image() const
{
    return _image;
}

QString Space::id() const
{
    return _drive.getRootValue().getIdValue();
}

QUrl Space::webdavUrl() const
{
    return QUrl(_drive.getRootValue().getWebDavUrlValue());
}
