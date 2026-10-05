[![OpenCloud Desktop CI](https://github.com/opencloud-eu/desktop/actions/workflows/main.yml/badge.svg)](https://github.com/opencloud-eu/desktop/actions/workflows/main.yml)
# `OpenCloud Desktop`

## Introduction

`OpenCloud Desktop` is a tool to synchronize files from `OpenCloud`
with your computer.

## Download

### Binary packages

- Please have a look at our releases https://github.com/opencloud-eu/desktop/releases

### Source code

The `OpenCloud Desktop` is developed in Git. Since Git makes it easy to
fork and improve the source code and to adapt it to your need, many copies
can be found on the Internet, in particular on GitHub. However, the
authoritative repository maintained by the developers is located at
https://github.com/opencloud-eu/desktop/.

## macOS sync modes

On macOS 26 or later, when the bundled FileProvider extension is available, OpenCloud
uses on-demand files in Finder by default. Traditional folder sync stays inactive.

To use local sync folders, select **Use traditional folder sync** in Settings,
then click **Restart to apply sync mode**. This disconnects on-demand files before
starting traditional sync. Existing local files and saved folder settings are kept.
If no folders are configured, use **Add Space** on the account page after restarting.

Clear the option and restart to return to on-demand files. Installations without a
compatible FileProvider extension use traditional folder sync automatically.

See [Finder behavior, requirements, and limits](shell_integration/MacOSX/README.md)
and [FileProvider test instructions](test/macos/README.md).

## Reporting issues and contributing

If you find any bugs or have any suggestion for improvement, please
file an issue at https://github.com/opencloud-eu/desktop/issues. Do not
contact the authors directly by mail, as this increases the chance
of your report being lost.

If you created a patch, please submit a [Pull
Request](https://github.com/opencloud-eu/desktop/pulls).

## Maintainers and Contributors

`OpenCloud Desktop` is developed by the `OpenCloud` community and [receives
patches from a variety of authors](https://github.com/opencloud-eu/desktop/graphs/contributors).


## License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    for more details.
