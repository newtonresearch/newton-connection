/*
	File:		Logging.h

	Abstract:	Interface to debug info logging.
					There are three levels of debug logging:
					0  None.
					1  Miniumum - protocol exchanges, progress box descriptions.
					2  Full - backup/restore entries.
					3  Developer(hidden from UI) - anything extra required from time to time for debug.

	Written by:	Newton Research, 2015.
*/


#if !defined(__LOGGING_H)
#define __LOGGING_H 1

extern int gLogLevel;

#define MINIMUM_LOG if (gLogLevel >= 1)
#define FULL_LOG if (gLogLevel >= 2)
#define DEVELOPER_LOG if (gLogLevel >= 3)

#endif	/* __LOGGING_H */
