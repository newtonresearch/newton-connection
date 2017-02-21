/*
	File:		Session.h

	Contains:	Public interface to the docker session class.
					init:ing a NCSession object creates an NCEndpoint object that initiates
					a listen on all available transports. When a transport connects to a Newton device,
					the remaining transports are closed and the dock session protocol is started.

	Written by:	Newton Research. 2011.
*/

#import "Component.h"
#import "DockEventQueue.h"
#import "DockErrors.h"
#import "DES.h"

/* --- NCSession error numbers --- */

#define kSessionNotInitialized		(kErrBase_Session - 1)
#define kSessionInvalidSession		(kErrBase_Session - 2)
#define kSessionInvalidStore			(kErrBase_Session - 3)
#define kSessionInvalidSoup			(kErrBase_Session - 4)
#define kSessionInvalidCursor			(kErrBase_Session - 5)
#define kSessionNewtonError			(kErrBase_Session - 6)
#define kSessionInvalidROMVersion	(kErrBase_Session - 7)
#define kSessionInvalidPassword		(kErrBase_Session - 8)


/* --- Id of event that matches any reply --- */

#define kDAnyEvent					0

/* --- The cursor --- */

@class NCCursor;


@interface NCSession : NSObject

@property (assign) BOOL isProtocolActive;

/* --- Session functions --- */

- (void)			open;
- (void)			close;
- (void)			registerEventHandler:(id<NCComponentProtocol>)inComponent;
- (id)			eventHandlerFor:(EventType)inCmd;
- (void)			startTickler;
- (void)			stopTickler;
- (void)			suppressTimeout:(BOOL)inDoSuppress;
- (void)			doEvent:(EventType)inCmd;
- (void)			doEvent:(EventType)inCmd data:(const void *)inData length:(unsigned int)inLength;

- (NewtonErr)	sendEvent:(EventType)inCmd;
- (NewtonErr)	sendEvent:(EventType)inCmd value:(int)inValue;
- (NewtonErr)	sendEvent:(EventType)inCmd ref:(RefArg)inRef;
- (NewtonErr)	sendEvent:(EventType)inCmd data:(const void *)inData length:(unsigned int)inLength;
- (NewtonErr)	sendEvent:(EventType)inCmd length:(unsigned int)inLength data:(const void *)inData length:(unsigned int)inDataLength;
- (NCDockEvent *) sendEvent:(EventType)inCmd expecting:(EventType)inReply;
- (NCDockEvent *) receiveEvent:(EventType)inCmd;
- (NewtonErr)	receiveResult;

/* --- Information functions --- */

- (Ref)			getUserFont;
- (Ref)			getUserFolders;
- (Ref)			getGestalt:(uint32_t)info;
- (void)			getHexade;
- (Ref)			setDateTime:(Ref)inTime;

- (void)			setStatusText:(const UniChar *)inText;

- (uint32_t)	setLastSyncTime:(uint32_t)inTime;

/* --- Store functions --- */

- (Ref)			getAllStores;
- (Ref)			getDefaultStore;

- (NewtonErr)	setCurrentStore:(RefArg)inStore /* if nil, set default store */ info:(BOOL)inSetStoreInfo;

/* --- Store & soup info functions --- */

- (Ref)			getAllSoups;
- (Ref)			getSoupIndexes;
- (Ref)			getSoupInfo;

- (NewtonErr)	setSoupInfo:(RefArg)inSoupInfo;

/* --- Soup functions --- */

- (NewtonErr)	createSoup:(RefArg)inSoupName index:(RefArg)inSoupIndex;
- (NewtonErr)	deleteSoup;
- (NewtonErr)	emptySoup;

- (NewtonErr)	setCurrentSoup:(RefArg)inSoupName;

/* --- Entry functions --- */

- (NewtonErr)	addEntry:(RefArg)inEntry;
- (NewtonErr)	changeEntry:(RefArg)inEntry;

- (NewtonErr)	deleteEntry:(RefArg)inEntry;
- (NewtonErr)	deleteEntryList:(RefArg)inEntryList;
- (NewtonErr)	deleteEntryId:(RefArg)inEntryId;
- (NewtonErr)	deleteEntryIdList:(RefArg)inEntryIdList;
// private helper
- (NewtonErr)	deleteEntries:(RefArg)inEntries;

- (Ref)			getEntry:(int)inUniqueId;
- (Ref)			getEntryIds;

/* --- Cursor functions --- */

- (NCCursor *)	query:(RefArg)inSoupName spec:(RefArg)inQuerySpec;

/* --- Package loading --- */

- (void)			sendPackage:(NSURL *)inURL
						callback:(NCProgressCallback)inCallback
					  frequency:(unsigned int)inFrequency;

/* --- Protocol Extensions --- */

- (NewtonErr)	loadExtension:(NSString *)inExtensionName;
- (NCDockEvent *)	callExtension:(EventType)inExtensionId with:(RefArg)inParams;
- (NewtonErr)	removeExtension:(EventType)inExtensionId;

/* --- Global/Root Functions --- */

- (Ref)		callGlobalFunction:(const char *)inFunctionName with:(RefArg)inArgsArray;
- (Ref)		callRootMethod:(const char *)inMethodName with:(RefArg)inArgsArray;
// private helper
- (Ref)		callGlobalFunctionOrRootMethod:(EventType)inCmd name:(const char *)inName with:(RefArg)inArgs;

@end

