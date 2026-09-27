/*
 * Copyright (C) 2025 OpenCloud GmbH
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

import Foundation
import OSLog

/// Processes lines received from the main app via socket connection.
class FileProviderSocketLineProcessor: NSObject, LineProcessor {

    weak var delegate: FileProviderExtension?
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "eu.opencloud.desktop.FileProviderExt", category: "SocketLineProcessor")

    init(delegate: FileProviderExtension) {
        self.delegate = delegate
        super.init()
    }

    func process(_ line: String) {
        // Don't log sensitive data
        if line.contains("~") {
            logger.debug("Processing line with potentially sensitive data")
        } else {
            logger.debug("Processing line: \(line)")
        }

        let splitLine = line.split(separator: ":", maxSplits: 1)
        guard let commandSubsequence = splitLine.first else {
            logger.error("Input line did not have a command")
            return
        }

        let command = String(commandSubsequence)
        logger.debug("Received command: \(command)")

        switch command {
        case "SEND_FILE_PROVIDER_DOMAIN_IDENTIFIER":
            // Main app is requesting our domain identifier
            delegate?.sendDomainIdentifier()

        case "ACCOUNT_NOT_AUTHENTICATED", "ACCOUNT_DETAILS":
            // Account state is controlled only through the code-signing-checked
            // XPC service. The legacy socket is not an authentication channel.
            logger.warning("Ignoring account configuration on the legacy socket")

        case "IGNORE_LIST":
            // Received ignore list patterns from main app
            guard let ignoreListSubsequence = splitLine.last else {
                logger.error("Ignore list missing content")
                return
            }

            let ignorePatterns = ignoreListSubsequence.components(separatedBy: "_~IL$~_")
            logger.debug("Received \(ignorePatterns.count) ignore patterns")
            // TODO: Apply ignore patterns

        default:
            logger.warning("Unknown command received: \(command)")
        }
    }
}
