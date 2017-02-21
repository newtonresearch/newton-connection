/*
	File:		Comms.h

	Contains:	Public interface to Newton Connection comms.

	Written by:	Newton Research Group, 2011.
*/

#if !defined(__COMMS_H)
#define __COMMS_H 1

typedef int NCError;


/* --- General errors --- */

#define kErrBase_NC					(-98000)

#define kNCOutOfMemory				(kErrBase_NC - 1)
#define kNCInvalidParameter		(kErrBase_NC - 2)
#define kNCInternalError			(kErrBase_NC - 3)
#define kNCErrorReadingFromPipe	(kErrBase_NC - 4)
#define kNCErrorWritingToPipe		(kErrBase_NC - 5)
#define kNCInvalidFile				(kErrBase_NC - 6)

/* --- Base error numbers --- */

#define kErrBase_Comms				(kErrBase_NC - 200)
#define kErrBase_Frames				(kErrBase_NC - 400)
#define kErrBase_Session			(kErrBase_NC - 600)

/* --- Comms error numbers --- */

#define kCommsNoDispatchSource	(kErrBase_Comms - 1)
#define kCommsPartialData			(kErrBase_Comms - 2)
#define kCommsNotAllWritten		(kErrBase_Comms - 3)

#endif	/* __COMMS_H */
