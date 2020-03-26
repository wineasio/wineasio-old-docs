s/unsigned long/ULONG/g
s/long long int/LONGLONG/g
s/long int/LONG/g
s/long/LONG/g

# The following 4 lines are necessary for win64 but cause win32 to crash
s/(\(\*bufferSwitch)\)/(CALLBACK \1/g
s/(\(\*sampleRateDidChange)\)/(CALLBACK \1/g
s/(\(\*asioMessage)\)/(CALLBACK \1/g
s/(\(\*bufferSwitchTimeInfo)\)/(CALLBACK \1/g
