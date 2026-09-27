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

#ifndef ClientCommunicationProtocol_h
#define ClientCommunicationProtocol_h

#import <Foundation/Foundation.h>

/**
 * Protocol for XPC communication between the main app and FileProvider extension.
 * The main app connects to this service to configure account credentials.
 */
@protocol ClientCommunicationProtocol

/**
 * Get the raw file provider domain identifier value.
 */
- (void)getFileProviderDomainIdentifierWithCompletionHandler:(void (^)(NSString *domainIdentifier, NSError *error))completionHandler;

/** Read the current domain activity, pending items and last verified sync times. */
- (void)getSyncStatusWithCompletionHandler:(void (^)(NSDictionary<NSString *, id> *status, NSError *error))completionHandler;

/**
 * Configure account credentials for this FileProvider domain.
 * @param davPath The WebDAV path on the server (e.g., "/dav/spaces/<spaceId>" or "/remote.php/webdav")
 */
- (void)configureAccountWithUser:(NSString *)user
                          userId:(NSString *)userId
                       serverUrl:(NSString *)serverUrl
                        password:(NSString *)password
                         davPath:(NSString *)davPath;

/**
 * Configure account credentials with explicit authentication type.
 * @param davPath The WebDAV path on the server (e.g., "/dav/spaces/<spaceId>" or "/remote.php/webdav")
 * @param authType The authentication type: "bearer" for OAuth tokens, "basic" for username/password
 */
- (void)configureAccountWithUser:(NSString *)user
                          userId:(NSString *)userId
                       serverUrl:(NSString *)serverUrl
                        password:(NSString *)password
                         davPath:(NSString *)davPath
                        authType:(NSString *)authType;

/** Configure and acknowledge validation/persistence errors to the host. */
- (void)configureAccountWithUser:(NSString *)user
                          userId:(NSString *)userId
                       serverUrl:(NSString *)serverUrl
                        password:(NSString *)password
                         davPath:(NSString *)davPath
                        authType:(NSString *)authType
                      generation:(NSString *)generation
               completionHandler:(void (^)(NSError *error))completionHandler;

/**
 * Remove account configuration (e.g., on sign out).
 */
- (void)removeAccountConfig;

/** Acknowledge only after persisted credentials and domain state are cleared. */
- (void)removeAccountConfigWithCompletionHandler:(void (^)(NSError *error))completionHandler;
- (void)removeAccountConfigWithGeneration:(NSString *)generation completionHandler:(void (^)(NSError *error))completionHandler;

@end

#endif /* ClientCommunicationProtocol_h */
