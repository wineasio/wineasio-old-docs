# Applications Tested with WineASIO

## Work Without Issue

| Application | Version | Bits | Notes |
| :--- | :--- | :---: | :--- |

## Work With Issues

| Application | Version | Bits | Notes |
| :--- | :--- | :---: | :--- |
| FL Studio | 12, 20 | 64/32 | _Sample Rate_ will be reset to 44,100 on startup regardless of registry/environment. To change, use **Audio Dialog** after application starts.  Also FL Studio must be invoked as follows: **wine/wine64 [path-to-fl.exe-or-fl64.exe] 2>&1 \| tee /dev/null**.  The postfix is important, otherwise FL Studio 20 will not start; its okay to put this command in a script. |

## Do Not Work

| Application | Version | Bits | Notes |
| :--- | :--- | :---: | :--- |
