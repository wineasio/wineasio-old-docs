# WineASIO Install -- UNDER CONSTRUCTION

## Overview

1. Install prerequisite software from your distribution.
2. Clone the WineASIO source code.
3. ./bootstrap.sh and ./configure the source in a manner consistant with your Linux distribution.
4. 'make' the dlls.
5. 'make install' the dlls.
6. 'make dll-register' to register WineASIO in your WINEPREFIX.

## 1. Software Prerequisites

### Gentoo

```shell
# emerge --ask autoconf bc wget unzip
```

#### Gentoo WINE

##### All versions of Gentoo WINE

```shell
# echo "app-emulation/winetricks ~amd64" >> /etc/portage/package.accept_keywords/wine
# echo "app-emulation/wine-mono ~amd64" >> /etc/portage/package.accept_keywords/wine
```

##### For 'vanilla' WINE version

```shell
# echo "app-emulation/wine-vanilla ~amd64" >> /etc/portage/package.accept_keywords/wine
# emerge --ask wine-vanilla winetricks
```

##### For 'staging' WINE version

```shell
# echo "app-emulation/wine-staging ~amd64" >> /etc/portage/package.accept_keywords/wine
# emerge --ask wine-staging winetricks
```

#### Gentoo JACK

##### All versions of Gentoo JACK

