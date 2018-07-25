### UNDER CONSTRUCTION -- CHECK BACK JULY 26, 2018

#### Dilemma
You have professional audio composition, production, or engineering software that is designed to run on Windows, _but you'd prefer to work in [Linux](https://en.wikipedia.org/wiki/Linux)._

How?

You'd like to use the Windows Emulator ([WINE](https://www.winehq.org)) for Linux, but you need _low-latency, high performance audio ([ASIO](https://en.wikipedia.org/wiki/Audio_Stream_Input/Output))_ support.  Plus you'd like to _route your audio through [JACK](http://jackaudio.org)_ so you can send audio output from one program to the input of another.

#### Solution
WineASIO is a free software driver for WINE that provides all of the above.

* ASIO v2.3 support, backwards-compatible to v1.0
* JACK Audio Connection Kit support (JACK and JACK2)
* Hardware-independent; use with your specialized audio gear
* Compatible with 32-bit and 64-bit WINE and Windows software
* Tested with WINE 3.0.x and 3.1.x (both "Vanilla" and "Staging" editions)
* Fully configurable
