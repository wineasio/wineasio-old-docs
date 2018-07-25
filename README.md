## UNDER CONSTRUCTION -- CHECK BACK JULY 26, 2018

### Dilemma
You have professional audio composition, production, or engineering software that is designed to run on Windows, _but you'd prefer to work in [Linux](https://en.wikipedia.org/wiki/Linux)._

How?

You'd like to use the Windows Emulator ([WINE](https://www.winehq.org)) for Linux, but you need _low-latency, high performance audio ([ASIO](https://en.wikipedia.org/wiki/Audio_Stream_Input/Output)) support.  Plus you'd like to _route your audio through [JACK](http://jackaudio.org)_ so you can send audio output from one program to the input of another.

### Solution
_WineASIO_ is a free software driver for WINE that provides all of the above.

* ASIO v2.3 support, backwards-compatible to v1.0
* JACK Audio Connection Kit support (JACK and JACK2)
* Hardware-independent; use with your specialized audio gear
* Compatible with 32-bit and 64-bit WINE and Windows software
* Tested with WINE 3.0.x and 3.1.x (both "Vanilla" and "Staging" editions)
* Fully configurable

### Hate Reading Documentation?
Follow the procedure in the [QUICKSTART.md](https://github.com/wineasio/wineasio/blob/master/QUICKSTART.md) file.  It should get you installed and up-and-running in most environments, but with little-to-no explanation.

### For the Patient
1. Browse the [KNOWN-APPS.md](https://github.com/wineasio/wineasio/blob/master/KNOWN-APPS.md) file to see if there are any issues related to the Windows application(s) you intend to use with WineASIO.  If your program is not listed (and in the early days of beta, it won't be) try it and let us know!  Chances are, if your Windows audio program runs correctly under WINE (albiet with a default audio driver) and was designed to take advantage of ASIO, then it will work with WineASIO.

2. Have a look at the [INSTALL.md](https://github.com/wineasio/wineasio/blob/master/INSTALL.md) to configure, compile, and install the software.

3. Once installed, check out the [USAGE.md](https://github.com/wineasio/wineasio/blob/master/USAGE.md) file for info on passing parameters to WineASIO through the environment, and tips on getting the most out of the program.

### Questions?
Check the [FAQ.md](https://github.com/wineasio/wineasio/blob/master/FAQ.md).

### Troubles?
If you still can't get it to work, have a look at WineASIO's [Issues](https://github.com/wineasio/wineasio/issues) page for a solution, and if your issue is unreported, create a [new issue](https://github.com/wineasio/wineasio/issues/new).

### For the Curious
If you're curious how WineASIO works, check out [INTERNALS.md](https://github.com/wineasio/wineasio/blob/master/INTERNALS.md) for some technical nitty-gritty.

### Contributing
If you want to fix a bug or make an improvement (there must be _at least_ one bug!), first deeply contemplate [CODING-STYLE.md](https://github.com/wineasio/wineasio/blob/master/CODING-STYLE.md), then make your changes and submit a [pull request](https://github.com/wineasio/wineasio/pulls).  Make sure you add yourself to the [AUTHORS.md](https://github.com/wineasio/wineasio/blob/master/AUTHORS.md) file.

Enjoy!

## Help Wanted
If you'd like to take a stab at creating a better **WineASIO logo** for this site, then by all means, have at it!  The current logo is admittedly a 5-minute dog's breakfast.

---
_"No deadlines, man.  You get the software, bug-fixes, and updates when they_ arrive.  _The Dude abides."_
