# UNDER CONSTRUCTION -- ANY DAY NOW

## *Dilemma*
You have professional audio composition, audio production, or audio engineering software that is designed to run on Windows, _but you'd prefer to work in [Linux](https://en.wikipedia.org/wiki/Linux)._

How?

You'd like to use the Windows Emulator ([WINE](https://www.winehq.org)) for Linux, but you need *low-latency, high performance audio ([ASIO](https://en.wikipedia.org/wiki/Audio_Stream_Input/Output))* support.  Plus you'd like to *route your audio through [JACK](http://jackaudio.org)* so you can send audio output from one program to the input of another.

## *Resolution*

_WineASIO_ is a free software driver for WINE that provides all of the above, including:

* ASIO v2.3 support, backwards-compatible to v1.0
* JACK Audio Connection Kit support (JACK and JACK2)
* Hardware-independent; use with your specialized audio gear
* Compatible with 32-bit and 64-bit WINE and Windows software
* Tested with WINE 3.0.x and 3.1.x (both "vanilla" and "staging" releases)
* Fully configurable
---
---
## About
_WineASIO_ is a device driver for WINE in the form of two (2) .dll files (32-bit and 64-bit).  The WineASIO .dlls are installed into WINE's system-wide .dll directories once.  Then whenever you create a _new_ WINEPREFIX directory (to install your Windows apps into), the WineASIO .dlls need to be _registered_ into that WINEPREFIX.  From that time forward, whenever you use WINE to run ASIO-enabled Windows apps within that WINEPREFIX, you will be able to select 'WineASIO' as the default audio driver _using those apps_.  (The exact procedure varies from app to app, but is usually done in the specific app's "Audio Settings" dialog or similar.)

WineASIO will automatically start the JACK server and route capture and playback audio to/from your Windows app through JACK.  It is up to you to determine how you want to route your audio further (for instance, to another app, from a microphone or microphones, or to your monitor speakers, etc).  The typical way to do this is through a JACK-control utility such as 'qjackctl' which can be installed from your Linux distribution or from source.

You can also pass parmeters to WineASIO through the Unix environment (when you start your Windows app) to tailor WineASIO's behaviour.  But for most uses the defaults compiled into WineASIO should work fine.

And that's all there is to it.

## Building and Installing
Unfortunately, due to the restrictive licensing terms of the ASIO Software Development Kit (SDK), we cannot offer prebuilt binary .dll files for WineASIO.  You must first _build_ the .dll's on your computer.  Then they can be installed.

Fortunately, this has been made a relatively-painless process, and you will find specific instructions for all the major Linux distributions.

See [docs/INSTALL.md](docs/INSTALL.md) for details.

## Application-Specific Notes
Browse [docs/TESTED-APPS.md](docs/TESTED-APPS.md) to see if there are any issues with your Windows app and WineASIO.  If your app is not listed (and in the early days of beta, it won't be), try it and let us know!  Chances are, if your Windows audio app runs correctly under WINE, and was designed to take advantage of ASIO, then it will work fine with WineASIO.

## Changing WineASIO Parmeters
Check out [docs/PARAMS.md](docs/PARAMS.md) for info on passing parameters to WineASIO to suit specific use-cases and tastes.

## Questions?
Check the [docs/FAQ.md](docs/FAQ.md).

## IRC Chatroom
Join us on channel **#wineasio** on server **irc.freenode.net** to ask questions or get help.

## Troubles and Issues
If you absolutely _can't_ get WineASIO to work with your app, have a look at the [Issues](https://github.com/wineasio/wineasio/issues) page for a solution, and if your issue is unreported, create a [new issue](https://github.com/wineasio/wineasio/issues/new).

## For The Techies
If you're wondering how WineASIO works on the inside, check out [docs/INTERNALS.md](docs/INTERNALS.md) for some technical nitty-gritty.

## Contributing
If you fix a bug or want to make an improvement to the WineASIO codebase (there must be _at least_ one bug!), first deeply contemplate [docs/CODING-STYLE.md](docs/CODING-STYLE.md) then make your changes and submit a [pull request](https://github.com/wineasio/wineasio/pulls).

---

# Help Wanted
If you'd like to take a crack at creating a better **WineASIO logo**, then by all means, have at it!  The current logo is admittedly a 5-minute dog's breakfast in 500x500 pixels.

---
_"No deadlines, man.  You get the software, bug-fixes, and updates when they_ arrive.  _The Dude abides."_
