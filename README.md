#### Dilemma
You have professional audio composition, production, or engineering software that is designed to run on Windows, _but you'd prefer to work in [Linux](https://en.wikipedia.org/wiki/Linux)._

How?

You'd like to use the Windows Emulator ([WINE](https://www.winehq.org)) for Linux, but you need *low-latency, high performance audio ([ASIO](https://en.wikipedia.org/wiki/Audio_Stream_Input/Output))* support.  Plus you'd like to *route your audio through [JACK](http://jackaudio.org)* so you can send audio output from one program to the input of another.

#### Solution
_WineASIO_ is a free software driver for WINE that provides all of the above, including:

* ASIO v2.3 support, backwards-compatible to v1.0
* JACK Audio Connection Kit support (JACK and JACK2)
* Hardware-independent; use with your specialized audio gear
* Compatible with 32-bit and 64-bit WINE and Windows software
* Tested with WINE 5.x (both "vanilla" and "staging" releases)
* Fully configurable
