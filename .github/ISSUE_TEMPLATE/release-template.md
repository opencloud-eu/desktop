---
name: Release
about: Checklist for preparing and publishing a release
title: 'X.Y.Z Release'
---

## Preparation

- [ ] Merge all planned bug fixes and features
- [ ] Prepare changelogs
- [ ] Create RC tag (`vX.Y.Z-rc.N`)
- [ ] Test artifacts are available
  - [ ] Linux (`AppImage`)
  - [ ] macOS (`.pkg`)
  - [ ] Windows (Microsoft Store)

## QA

> [!NOTE]
>
> Desktop client: [vX.Y.Z-rc.N](https://github.com/opencloud-eu/desktop/releases/tag/vx.y.z-rc.n)
>
> OpenCloud server: [vX.Y.Z](https://github.com/opencloud-eu/opencloud/releases/tag/vx.y.z)

- [ ] Automated GUI tests passed on RC release CI
- [ ] [Smoke testing](https://github.com/opencloud-eu/desktop/blob/main/test/gui/docs/test-plans/smoke_testing.md)
- [ ] Changelog testing
- [ ] Automated GUI tests
  - [ ] Linux
  - [ ] Windows
- [ ] [Regression testing](https://github.com/opencloud-eu/desktop/blob/main/test/gui/docs/test-plans/regression_testing.md)
- [ ] No release blockers

#### Issues Found During QA

- ...

## Final Release

- [ ] Create final release tag
- [ ] Release CI is green
- [ ] Release artifacts are available
  - [ ] Linux (`AppImage`)
  - [ ] macOS (`.pkg`)
  - [ ] Windows (Microsoft Store)
