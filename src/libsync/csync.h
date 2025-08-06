/*
 * libcsync -- a library to sync a directory with another
 *
 * Copyright (c) 2008-2013 by Andreas Schneider <asn@cryptomilk.org>
 * Copyright (c) 2012-2013 by Klaas Freitag <freitag@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file csync.h
 *
 * @brief Application developer interface for csync.
 *
 * @defgroup csyncPublicAPI csync public API
 *
 * @{
 */

#ifndef _CSYNC_H
#define _CSYNC_H

#include "libsync/opencloudsynclib.h"

#include <QObject>


namespace OCC {
class SyncJournalFileRecord;
}

namespace CSyncEnums {
OPENCLOUD_SYNC_EXPORT Q_NAMESPACE


    /**
     * Instruction enum. In the file traversal structure, it describes
     * the csync state of a file.
     */
    // clang-format off
enum SyncInstruction : uint16_t {
    CSYNC_INSTRUCTION_NONE            = 1 << 1,  /* Nothing to do (UPDATE|RECONCILE) */
    CSYNC_INSTRUCTION_REMOVE          = 1 << 2,  /* The file need to be removed (RECONCILE) */
    CSYNC_INSTRUCTION_RENAME          = 1 << 3,  /* The file need to be renamed (RECONCILE) */
    CSYNC_INSTRUCTION_NEW             = 1 << 4,  /* The file is new compared to the db (UPDATE) */
    CSYNC_INSTRUCTION_CONFLICT        = 1 << 5,  /* The file need to be downloaded because it is a conflict (RECONCILE) */
    CSYNC_INSTRUCTION_IGNORE          = 1 << 6,  /* The file is ignored (UPDATE|RECONCILE)
                                                  * Identical to CSYNC_INSTRUCTION_NONE but logged to the user.
                                                  */
    CSYNC_INSTRUCTION_SYNC            = 1 << 7,  /* The file need to be pushed to the other remote (RECONCILE) */
    CSYNC_INSTRUCTION_ERROR           = 1 << 8,
    CSYNC_INSTRUCTION_TYPE_CHANGE     = 1 << 9,  /* Like NEW, but deletes the old entity first (RECONCILE)
                                                    Used when the type of something changes from directory to file
                                                    or back. */
    CSYNC_INSTRUCTION_UPDATE_METADATA = 1 << 10, /* If the etag has been updated and need to be writen to the db,
                                                    but without any propagation (UPDATE|RECONCILE) */
};
// clang-format on
Q_FLAG_NS(SyncInstruction)
Q_DECLARE_FLAGS(SyncInstructions, SyncInstruction)
Q_DECLARE_OPERATORS_FOR_FLAGS(SyncInstructions)

// Also, this value is stored in the database, so beware of value changes.
enum ItemType : uint8_t {
    ItemTypeFile = 0,
    ItemTypeSymLink = 1,
    ItemTypeDirectory = 2,
    ItemTypeUnsupported = 3,

    /** The file is a dehydrated placeholder, meaning data isn't available locally */
    ItemTypeVirtualFile = 4,

    /** A ItemTypeVirtualFile that wants to be hydrated.
     *
     * Actions may put this in the db as a request to a future sync, such as
     * implicit hydration (when the user wants to access file data) when using
     * suffix vfs. For pin-state driven hydrations changing the database is
     * not necessary.
     *
     * For some vfs plugins the placeholder files on disk may be marked for
     * (de-)hydration (like with a file attribute) and then the local discovery
     * will return this item type.
     *
     * The discovery will also use this item type to mark entries for hydration
     * if an item's pin state mandates it, such as when encountering a AlwaysLocal
     * file that is dehydrated.
     */
    ItemTypeVirtualFileDownload = 5,

    /** A ItemTypeFile that wants to be dehydrated.
     *
     * Similar to ItemTypeVirtualFileDownload, but there's currently no situation
     * where it's stored in the database since there is no action that triggers a
     * file dehydration without changing the pin state.
     */
    ItemTypeVirtualFileDehydration = 6,
};
Q_ENUM_NS(ItemType)
}

using namespace CSyncEnums;

OPENCLOUD_SYNC_EXPORT QDebug operator<<(QDebug debug, const SyncInstructions &job);

/**
 * }@
 */
#endif /* _CSYNC_H */
/* vim: set ft=c.doxygen ts=8 sw=2 et cindent: */
