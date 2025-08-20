# Changelog

## [3.0.0](https://github.com/opencloud-eu/desktop/releases/tag/v3.0.0) - 2025-08-20

### ❤️ Thanks to all contributors! ❤️

@Svanvith, @TheOneRing, @anon-pradip, @fschade, @individual-it, @kulmann, @prashant-gurung899

### 💥 Breaking changes

- Remove unused fields in sqlite table [[#419](https://github.com/opencloud-eu/desktop/pull/419)]
- Add Windows VFS [[#305](https://github.com/opencloud-eu/desktop/pull/305)]
- Enable http2 support by default [[#333](https://github.com/opencloud-eu/desktop/pull/333)]

### 📈 Enhancement

- Use utf16 for the rotating log, to reduce string conversions [[#467](https://github.com/opencloud-eu/desktop/pull/467)]
- change help URL to the right docs URL [[#466](https://github.com/opencloud-eu/desktop/pull/466)]
- Folder watcher: ignore changes in short lived files [[#455](https://github.com/opencloud-eu/desktop/pull/455)]
- Fix assert in httplogger if a cached request is actuall send [[#456](https://github.com/opencloud-eu/desktop/pull/456)]
- Sync description and space name to Windows [[#443](https://github.com/opencloud-eu/desktop/pull/443)]
- Replace csync C code with std::filesystem [[#393](https://github.com/opencloud-eu/desktop/pull/393)]
- Remove margins around the content widgets [[#377](https://github.com/opencloud-eu/desktop/pull/377)]

### 🐛 Bug Fixes

- Sync Scheduler: Ensure the current sync is actually running [[#452](https://github.com/opencloud-eu/desktop/pull/452)]
- Fix leak of accountstates [[#445](https://github.com/opencloud-eu/desktop/pull/445)]
- Fix color for selected space [[#437](https://github.com/opencloud-eu/desktop/pull/437)]
- Don't truncate inode on Windows [[#412](https://github.com/opencloud-eu/desktop/pull/412)]
- Fix printing of duration [[#400](https://github.com/opencloud-eu/desktop/pull/400)]
- Don't try LockFile on directories [[#366](https://github.com/opencloud-eu/desktop/pull/366)]
- OAuth: Only display user name in an error if we have one [[#355](https://github.com/opencloud-eu/desktop/pull/355)]

## [2.0.0](https://github.com/opencloud-eu/desktop/releases/tag/v2.0.0) - 2025-07-03

### ❤️ Thanks to all contributors! ❤️

@TheOneRing, @anon-pradip, @individual-it, @michaelstingl, @prashant-gurung899

### 💥 Breaking changes

- Enable http2 support by default [[#333](https://github.com/opencloud-eu/desktop/pull/333)]
- Since Qt 6.8 network headers are normalized to lowercase [[#308](https://github.com/opencloud-eu/desktop/pull/308)]
- Remove Theme::linkSharing and Theme::userGroupSharing [[#279](https://github.com/opencloud-eu/desktop/pull/279)]
- Remove unsupported solid avatar color branding [[#280](https://github.com/opencloud-eu/desktop/pull/280)]
- Remove Theme::wizardUrlPostfix [[#278](https://github.com/opencloud-eu/desktop/pull/278)]
- Read preconfigured server urls [[#275](https://github.com/opencloud-eu/desktop/pull/275)]
- Require global settings to always be located in /etc/ [[#268](https://github.com/opencloud-eu/desktop/pull/268)]
- Move default exclude file to a resource [[#266](https://github.com/opencloud-eu/desktop/pull/266)]

### 🐛 Bug Fixes

- OAuth: Only display user name in an error if we have one [[#355](https://github.com/opencloud-eu/desktop/pull/355)]
- Fix reuse of existing Space folders [[#311](https://github.com/opencloud-eu/desktop/pull/311)]
- Retry oauth refresh if wellknown request failed [[#310](https://github.com/opencloud-eu/desktop/pull/310)]
- Update KDSingleApplication to 1.2.0 [[#293](https://github.com/opencloud-eu/desktop/pull/293)]
- Fix casing of Spaces [[#272](https://github.com/opencloud-eu/desktop/pull/272)]
- Restart the client if the server url changed [[#254](https://github.com/opencloud-eu/desktop/pull/254)]
- Directly schedule sync once the etag changed [[#253](https://github.com/opencloud-eu/desktop/pull/253)]
- Update quota exeeded message [[#248](https://github.com/opencloud-eu/desktop/pull/248)]
- Fix sync location with manual setup [[#243](https://github.com/opencloud-eu/desktop/pull/243)]
- Properly handle `server_error` response from IDP [[#231](https://github.com/opencloud-eu/desktop/pull/231)]

### 📈 Enhancement

-  Remove settings update from connection validator, update settings only oce per hour [[#301](https://github.com/opencloud-eu/desktop/pull/301)]
- Handle return key for the url wizard page [[#300](https://github.com/opencloud-eu/desktop/pull/300)]
- Show profile images in Desktop Client [[#297](https://github.com/opencloud-eu/desktop/pull/297)]
- Enable native tooltips for the accounts on Qt >= 6.8.3 [[#255](https://github.com/opencloud-eu/desktop/pull/255)]
- Update dependencies to Qt 6.8.3 and OpenSSL 3.4.1 [[#252](https://github.com/opencloud-eu/desktop/pull/252)]
