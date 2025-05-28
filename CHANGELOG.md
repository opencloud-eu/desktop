# Changelog

## [2.0.0](https://github.com/opencloud-eu/desktop/releases/tag/v2.0.0) - 2025-05-28

### ❤️ Thanks to all contributors! ❤️

@TheOneRing, @anon-pradip, @individual-it, @prashant-gurung899

### 💥 Breaking changes

- Remove Theme::linkSharing and Theme::userGroupSharing [[#279](https://github.com/opencloud-eu/desktop/pull/279)]
- Remove unsupported solid avatar color branding [[#280](https://github.com/opencloud-eu/desktop/pull/280)]
- Remove Theme::wizardUrlPostfix [[#278](https://github.com/opencloud-eu/desktop/pull/278)]
- Read preconfigured server urls [[#275](https://github.com/opencloud-eu/desktop/pull/275)]
- Require global settings to always be located in /etc/ [[#268](https://github.com/opencloud-eu/desktop/pull/268)]
- Move default exclude file to a resource [[#266](https://github.com/opencloud-eu/desktop/pull/266)]

### 📈 Enhancement

- Handle return key for the url wizard page [[#300](https://github.com/opencloud-eu/desktop/pull/300)]
- Show profile images in Desktop Client [[#297](https://github.com/opencloud-eu/desktop/pull/297)]
- Enable native tooltips for the accounts on Qt >= 6.8.3 [[#255](https://github.com/opencloud-eu/desktop/pull/255)]
- Update dependencies to Qt 6.8.3 and OpenSSL 3.4.1 [[#252](https://github.com/opencloud-eu/desktop/pull/252)]

### 🐛 Bug Fixes

- Update KDSingleApplication to 1.2.0 [[#293](https://github.com/opencloud-eu/desktop/pull/293)]
- Fix casing of Spaces [[#272](https://github.com/opencloud-eu/desktop/pull/272)]
- Restart the client if the server url changed [[#254](https://github.com/opencloud-eu/desktop/pull/254)]
- Directly schedule sync once the etag changed [[#253](https://github.com/opencloud-eu/desktop/pull/253)]
- Update quota exeeded message [[#248](https://github.com/opencloud-eu/desktop/pull/248)]
- Fix sync location with manual setup [[#243](https://github.com/opencloud-eu/desktop/pull/243)]
- Properly handle `server_error` response from IDP [[#231](https://github.com/opencloud-eu/desktop/pull/231)]
