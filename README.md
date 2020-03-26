# README.md

Lossless/skipless FL Studio audio piped through JACK on Linux with Wine.
(But other Windoze audio programs that make use of ASIO should work as well.)

## Installation for FL Studio 20

(Other Windoze audio programs that make use of ASIO should work as well.)

### Install Wine, JACK, and qjackctl

(Per your Linux distribution.)

### Install the WineASIO driver

```shell
$ git clone https://github.com/wineasio/wineasio
$ cd wineasio
$ make
# make install
$ export WINEARCH=win64
$ wine64 regsvr32 wineasio.dll
```

The `make install` assumes [Arch Linux](https://www.archlinux.org).  Adjust the `Makefile` to specify the directory of your system-wide Wine libraries if necessary.

### Configure Wine to emulate Windows 10

```shell
$ wine64 regedit setwin10.reg
```

### Install FL Studio

```shell
$ wine64 ~/path/to/flstudio-20-win-installer.exe
```

You can download a free demo of FL Studio at [https://www.image-line.com](https://www.image-line.com).  All components are enabled except Save.

### PAM tweaks for real-time/low-latency

* Create an `audio` group (if it does not already exist):
```shell
# groupadd audio
```
* Add yourself to the `audio` group:
```shell
# usermod -a -G audio yourUserID
```
* Add to `/etc/security/limits.conf`:
```
@audio		-	rtprio		99
@audio		-	memlock		unlimited
```
* Logout and login again.

## Running FL Studio

### Start Jack with qjackctl

```shell
$ qjackctl
```

* Set the `samplerate` to `44,100` Hz.
* Set the `frames/period` to `2,048` and `periods/buffer` to `4` to begin with, and adjust up or down as required.
* Start the JACK server with qjackctl.  Check for errors in the Messages/Status window.  Be sure JACK is running in real-time mode.

### Start FL Studio

```shell
$ wine64 ~/.wine/drive_c/Program\ Files/Image-Line/FL\ Studio\ 20/FL64.exe 2>&1 | tee /dev/null
```

(The somewhat bizarre 'tee' is necessary to prevent Wine from blocking indefinitely on start-up.  If anyone knows why this is the case, kindly let me know.)

### Within FL Studio

* On the main menu, click `Options | Audio settings...' and set `Device` to `WineASIO`.
* Observe the JACK graph in qjackctl.  Eight (8) inputs and eight (8) outputs will be created and connected.

Play the demo song, then dig in! :)
