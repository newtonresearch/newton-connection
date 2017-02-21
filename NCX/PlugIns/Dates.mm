/*
	File:		Dates.mm

	Contains:   Newton Dates import/export functions.

	Written by: Newton Research Group, 2006.

	To do:
	check repeat rule for both import & export
	export
		exceptions to repeating meetings/notes
		instance notes
		to do list
	import
		repeat exceptions
		instance notes
		to do list
*/

#import "NCXPlugIn.h"
#import "PlugInUtilities.h"
extern "C" {
#import "ical.h"
}
#import "ListIterator.h"

extern void		SetRect(Rect * r, short left, short top, short right, short bottom);

/*
	Soups
	Calendar				meeting frames for non-repeating meetings
	Repeat Meetings	meeting frames for repeating meetings; notes frames for specific instances

	Calendar Notes		meeting frames for non-repeating events
	Repeat Notes		meeting frames for repeating events; notes frames for specific instances

	Slots
	mtgStartDate
	mtgDuration		minutes
	mtgText			rich string
	mtgAlarm
	mtgInvitees		[nameRef] use GetMeetingInvitees
	mtgLocation		 nameRef  use GetMeetingLocation
	notesData		[note]

	mtgStopDate		date repeating meeting should stop
	repeatType		
	mtgInfo			depends on repeatType
	exceptions		[[]…]
*/

// 12:00am 1/1/2090 -- default mtgStopDate
#define kEndOfTime 97829280

// Constants for repeatType
enum
{
	kDayOfWeek,
	kWeekInMonth,
	kDateInMonth,
	kDateInYear,
	kPeriod,
	kNever,
	k6Reserved,
	kWeekInYear
};

// Constants for day of week
enum
{
	kSunday = 0x00000800,
	kMonday = 0x00000400,
	kTuesday = 0x00000200,
	kWednesday = 0x00000100,
	kThursday = 0x00000080,
	kFriday = 0x00000040,
	kSaturday = 0x00000020,
	kEveryday = 0x00000FE0 
};

// Constants for week in month 
enum
{
	kFirstWeek = 0x00000010,
	kSecondWeek = 0x00000008,
	kThirdWeek = 0x00000004,
	kFourthWeek = 0x00000002,
	kLastWeek = 0x00000001,
	kEveryWeek = 0x0000001F
};

// icalrecurrencetype_weekday -> Newton lookup
const int kWeekDayLookup[] =
{
	kEveryday,
	kSunday,
	kMonday,
	kTuesday,
	kWednesday,
	kThursday,
	kFriday,
	kSaturday
};

// ical -> Newton lookup
const int kWeekLookup[] =
{
	kLastWeek,
	kEveryWeek,
	kFirstWeek,
	kSecondWeek,
	kThirdWeek,
	kFourthWeek
};



/*------------------------------------------------------------------------------
	D a t e T o I C a l
------------------------------------------------------------------------------*/

@interface DateToICal : NCXTranslator
{
	icalcomponent * calendar;
}
- (icalcomponent *) exportMeeting: (RefArg) inEntry;
- (icalcomponent *) exportRepeatingMeeting: (RefArg) inEntry;
- (icalcomponent *) exportCribNote: (RefArg) inEntry;

- (void) exportLocation: (RefArg) inEntry to: (icalcomponent *) outEvent;
- (void) exportInvitees: (RefArg) inEntry to: (icalcomponent *) outEvent;
- (void) exportNotesData: (RefArg) inEntry to: (icalcomponent *) outEvent;
- (void) exportInstanceNotesData: (RefArg) inEntry to: (icalcomponent *) outEvent;
- (void) exportRepeatInfo: (RefArg) inEntry to: (icalcomponent *) outEvent;
@end


/*------------------------------------------------------------------------------
	Make a NewtonScript date (number of minutes since 1904) from a POSIX date.
	Args:		inTime			libical time struct
	Return:	a NewtonScript integer
------------------------------------------------------------------------------*/

#define kMinutesSince1904 34714080

Ref
MakeDate(struct icaltimetype inTime)
{
	inTime.is_utc = YES;		// not really, obviously, but it stops icaltime_as_timet() from adjusting anything
	time_t theTime = icaltime_as_timet(inTime);
	// convert to minutes since 1970, then since 1904
	return MAKEINT(kMinutesSince1904 + theTime/60);
}


/*------------------------------------------------------------------------------
	Convert time in minutes from 1 Jan 1904
		  to ical time.
	Args:		inRef			Newton time in minutes
	Return:	libical time struct
------------------------------------------------------------------------------*/
extern "C" Ref		FDate(RefArg inRcvr, RefArg inMinsSince1904);

struct icaltimetype
MakeiCalTime(RefArg inRef)
{
	RefVar dt(FDate(RA(NILREF), inRef));

	struct icaltimetype tt = icaltime_null_time();
	tt.second = (int)RINT(GetFrameSlot(dt, SYMA(second)));
	tt.minute = (int)RINT(GetFrameSlot(dt, SYMA(minute)));
	tt.hour = (int)RINT(GetFrameSlot(dt, SYMA(hour)));

	tt.day = (int)RINT(GetFrameSlot(dt, SYMA(Date)));
	tt.month = (int)RINT(GetFrameSlot(dt, SYMA(month)));
	tt.year = (int)RINT(GetFrameSlot(dt, SYMA(year)));

	tt.is_utc = NO;
	tt.is_date = NO; 

	return tt;
}


void
SetDaysOfWeek(int32_t inMtgInfo, struct icalrecurrencetype * outRecurrence)
{
	if (inMtgInfo & kEveryday)
	{
		int i = 0;
		int wom = 0;

		if ((inMtgInfo & kEveryWeek) != 0
		&&  (inMtgInfo & kEveryWeek) != kEveryWeek)
		{
			if (inMtgInfo & kFirstWeek)
				wom = 1 << 3;
			if (inMtgInfo & kSecondWeek)
				wom = 2 << 3;
			if (inMtgInfo & kThirdWeek)
				wom = 3 << 3;
			if (inMtgInfo & kFourthWeek)
				wom = 4 << 3;
			if (inMtgInfo & kLastWeek)
				wom = (~0UL) << 3;
		}

		if (inMtgInfo & kMonday)
			outRecurrence->by_day[i++] = wom + ICAL_MONDAY_WEEKDAY;
		if (inMtgInfo & kTuesday)
			outRecurrence->by_day[i++] = wom + ICAL_TUESDAY_WEEKDAY;
		if (inMtgInfo & kWednesday)
			outRecurrence->by_day[i++] = wom + ICAL_WEDNESDAY_WEEKDAY;
		if (inMtgInfo & kThursday)
			outRecurrence->by_day[i++] = wom + ICAL_THURSDAY_WEEKDAY;
		if (inMtgInfo & kFriday)
			outRecurrence->by_day[i++] = wom + ICAL_FRIDAY_WEEKDAY;
		if (inMtgInfo & kSaturday)
			outRecurrence->by_day[i++] = wom + ICAL_SATURDAY_WEEKDAY;
		if (inMtgInfo & kSunday)
			outRecurrence->by_day[i++] = wom + ICAL_SUNDAY_WEEKDAY;

		outRecurrence->by_day[i] = ICAL_RECURRENCE_ARRAY_MAX;
	}
}


const char *
MakeUTF8String(RefArg inStr)
{
	return [MakeNSString(inStr) UTF8String];
}


extern Ref
MakeStringOfLength(const UniChar * str, ArrayIndex numChars);

Ref
MakeStringFromUTF8String(const char * inStr)
{
	NSString * str = [NSString stringWithUTF8String: inStr];
	ArrayIndex bufLen = (ArrayIndex)str.length;
	UniChar * buf = (UniChar *) malloc(bufLen * sizeof(UniChar));
	[str getCharacters: buf];
	RefVar s(MakeStringOfLength(buf, bufLen));
	free(buf);
	return s;
}


/*------------------------------------------------------------------------------
	Make a NextStep string of a person’s full name from a Newton name frame.
	(Also used in Names plugin.)
	Args:		inPerson			AddressBook record
				inProperty		name of property to set
				inFrame			a NewtonScript frame eg for name or address
				inTag				slot to read
	Return:	an auto-released UTF8 string
------------------------------------------------------------------------------*/

const char *
MakeUTF8Name(RefArg inName)
{
	RefVar firstName(GetFrameSlot(inName, SYMA(first)));
	RefVar lastName(GetFrameSlot(inName, MakeSymbol("last")));
	NSString * firstNameStr = StrEmpty(firstName) ? nil : MakeNSString(firstName);
	NSString * lastNameStr = StrEmpty(lastName) ? nil : MakeNSString(lastName);
	NSString * fullNameStr = nil;

	if (firstNameStr != nil)
	{
		if (lastNameStr != nil)
			fullNameStr = [NSString stringWithFormat: @"%@ %@", firstNameStr, lastNameStr];
		else
			fullNameStr = firstNameStr;
	}
	else if (lastNameStr != nil)
		fullNameStr = lastNameStr;

	return fullNameStr ? [[NSString stringWithFormat: @"\"%@\"", fullNameStr] UTF8String] : nil;
}



/*------------------------------------------------------------------------------
	Make a meeting frame containing location or invitees to be set by the 'meet'
	protocol extension.
	Args:		inEntry			Names entry containing time and description
									required by calendar function
	Return:	a meeting frame
------------------------------------------------------------------------------*/

Ref
MakeMeetingFrame(RefArg inEntry)
{
	RefVar mtgStartSym(MakeSymbol("mtgStartDate"));
	RefVar mtgTextSym(MakeSymbol("mtgText"));

	RefVar item(AllocateFrame());
	SetFrameSlot(item, mtgStartSym, GetFrameSlot(inEntry, mtgStartSym));
	SetFrameSlot(item, mtgTextSym, GetFrameSlot(inEntry, mtgTextSym));
	return item;
}


@implementation DateToICal

/*------------------------------------------------------------------------------
	Dates app soup entries are aggregated into a single ics file on export.
	Args:		--
	Return:	YES
------------------------------------------------------------------------------*/

+ (BOOL)aggregatesEntries {
	return YES;
}


- (id) init
{
	if (self = [super init])
	{
		calendar = icalcomponent_new_vcalendar();
		icalcomponent_add_property(calendar, icalproperty_new_prodid("-//Newton Research//NONSGML NCX//EN"));
		icalcomponent_add_property(calendar, icalproperty_new_version("2.0"));
	}
	return self;
}


/*------------------------------------------------------------------------------
	Convert Dates app soup entry of class 'meeting to ics.
	Args:		inEntry			soup entry from Calendar, Calendar Notes,
									Repeat Meetings or Repeat Notes
	Return:	--
				the event is added to our calendar object which is written out
				when we stop exporting
------------------------------------------------------------------------------*/

- (NSString *) export: (RefArg) inEntry
{
	icalcomponent * event = NULL;

	RefVar stationery(GetFrameSlot(inEntry, MakeSymbol("viewStationery")));
	if (EQRef(stationery, MakeSymbol("meeting")))
		event = [self exportMeeting: inEntry];
	else if (EQRef(stationery, MakeSymbol("repeatingMeeting"))
		  ||  EQRef(stationery, MakeSymbol("exceptionMeeting")))
		event = [self exportRepeatingMeeting: inEntry];
	else if (EQRef(stationery, MakeSymbol("cribNote")))
		event = [self exportCribNote: inEntry];

	if (event != NULL)
		icalcomponent_add_component(calendar, event);

	return nil;
}

- (NSString *) exportDone: (NSString *) inAppName
{
	char * str = icalcomponent_as_ical_string(calendar);
	NSData * data = [NSData dataWithBytesNoCopy:str length:strlen(str) freeWhenDone:NO];
	return [self write:data toFile:[NSString stringWithFormat:@"Newton %@", inAppName] extension:@".ics"];
}


/*------------------------------------------------------------------------------
	Convert Calendar soup entry of viewStationery type 'meeting to icalcomponent.
	This entry represents a single meeting.

	Slots
	mtgStartDate
	mtgDuration		minutes
	mtgText			rich string
	mtgAlarm
	mtgInvitees		[nameRef] use GetMeetingInvitees
	mtgLocation		 nameRef  use GetMeetingLocation
	notesData		[note]

	Args:		inEntry			Calendar soup entry
	Return:	an icalcomponent object
------------------------------------------------------------------------------*/

- (icalcomponent *) exportMeeting: (RefArg) inEntry
{
	icalcomponent * event = icalcomponent_new_vevent();

	newton_try
	{
		RefVar item;
		RefVar start, duration, alarmTime;
		const char * text;

// required slots
		if (NOTNIL(start = GetFrameSlot(inEntry, MakeSymbol("mtgStartDate"))))
		{
			struct icaltimetype theTime = MakeiCalTime(start);
			icalcomponent_add_property(event, icalproperty_new_dtstart(theTime));
		}

		if (NOTNIL(duration = GetFrameSlot(inEntry, MakeSymbol("mtgDuration"))))
		{
			struct icaldurationtype theDuration = icaldurationtype_from_int((int)RINT(duration)*60);
			icalcomponent_add_property(event, icalproperty_new_duration(theDuration));
		}

		if (NOTNIL(item = GetFrameSlot(inEntry, MakeSymbol("mtgText"))))
		{
			text = MakeUTF8String(item);
			icalcomponent_add_property(event, icalproperty_new_summary(text));
		}

// optional slots
		if (NOTNIL(alarmTime = GetFrameSlot(inEntry, MakeSymbol("mtgAlarm"))))
		{
		// alarmTime is absolute time
			struct icaltriggertype trigger;
			trigger.time = MakeiCalTime(alarmTime);
			trigger.duration = icaldurationtype_null_duration();
			icalcomponent * alarm = icalcomponent_new_valarm();
			icalcomponent_add_property(alarm, icalproperty_new_action(ICAL_ACTION_DISPLAY));
			icalcomponent_add_property(alarm, icalproperty_new_trigger(trigger));
			icalcomponent_add_property(alarm, icalproperty_new_description(text));
			icalcomponent_add_component(event, alarm);
		}

		[self exportLocation: inEntry to: event];
		[self exportInvitees: inEntry to: event];
		[self exportNotesData: inEntry to: event];
	}
	newton_catch_all
	{}
	end_try;

	return event;
}


/*------------------------------------------------------------------------------
	Convert Calendar soup entry of viewStationery type 'repeatingMeeting to icalcomponent.
	This entry represents a repeating meeting.
	Args:		inEntry			Calendar soup entry
	Return:	an icalcomponent object
------------------------------------------------------------------------------*/

- (icalcomponent *) exportRepeatingMeeting: (RefArg) inEntry
{
	icalcomponent * event = icalcomponent_new_vevent();

	newton_try
	{
		RefVar item;
		RefVar start, duration, alarmTime;
		const char * text;

// required slots
		if (NOTNIL(start = GetFrameSlot(inEntry, MakeSymbol("mtgStartDate"))))
		{
			struct icaltimetype theTime = MakeiCalTime(start);
			icalcomponent_add_property(event, icalproperty_new_dtstart(theTime));
		}

		if (NOTNIL(duration = GetFrameSlot(inEntry, MakeSymbol("mtgDuration"))))
		{
			struct icaldurationtype theDuration = icaldurationtype_from_int((int)RINT(duration)*60);
			icalcomponent_add_property(event, icalproperty_new_duration(theDuration));
		}

		if (NOTNIL(item = GetFrameSlot(inEntry, MakeSymbol("mtgText"))))
		{
			text = MakeUTF8String(item);
			icalcomponent_add_property(event, icalproperty_new_summary(text));
		}

		[self exportRepeatInfo: inEntry to: event];

// optional slots
		if (NOTNIL(alarmTime = GetFrameSlot(inEntry, MakeSymbol("mtgAlarm"))))
		{
		// alarmTime is offset from event time
			struct icaltriggertype trigger;
			trigger.time = icaltime_null_time();
			trigger.duration = icaldurationtype_from_int(-(int)RINT(alarmTime)*60);
			icalcomponent * alarm = icalcomponent_new_valarm();
			icalcomponent_add_property(alarm, icalproperty_new_action(ICAL_ACTION_DISPLAY));
			icalcomponent_add_property(alarm, icalproperty_new_trigger(trigger));
			icalcomponent_add_property(alarm, icalproperty_new_description(text));
			icalcomponent_add_component(event, alarm);
		}

		[self exportLocation: inEntry to: event];
		[self exportInvitees: inEntry to: event];
		[self exportInstanceNotesData: inEntry to: event];
	}
	newton_catch_all
	{}
	end_try;

	return event;
}


/*------------------------------------------------------------------------------
	Convert Calendar soup entry of viewStationery type 'cribNote to icalcomponent.
	This entry represents a (possibly repeating) day-long event.
	Args:		inEntry			Calendar soup entry
	Return:	an icalcomponent object
------------------------------------------------------------------------------*/

- (icalcomponent *) exportCribNote: (RefArg) inEntry
{
	icalcomponent * event = icalcomponent_new_vevent();	// don’t use VJOURNAL; iCal does not recognise it

	newton_try
	{
		RefVar item;
		RefVar start;

// required slots
		if (NOTNIL(start = GetFrameSlot(inEntry, MakeSymbol("mtgStartDate"))))
		{
			icalproperty * startDate;
			struct icaltimetype theTime = MakeiCalTime(start);
			theTime.hour = theTime.minute = theTime.second = 0;
			theTime.is_date = YES;
			startDate = icalproperty_new_dtstart(theTime);
			icalproperty_set_value(startDate, icalvalue_new_date(theTime));	// overwrite with DATE instead of DATE-TIME
			icalcomponent_add_property(event, startDate);
		}

		if (NOTNIL(item = GetFrameSlot(inEntry, MakeSymbol("mtgText"))))
		{
			const char * text = MakeUTF8String(item);
			icalcomponent_add_property(event, icalproperty_new_summary(text));
		}

// confusing because the documentation suggests we have to cope with both single and repeating events
		[self exportRepeatInfo: inEntry to: event];

// optional slots
		[self exportNotesData: inEntry to: event];
		[self exportInstanceNotesData: inEntry to: event];
	}
	newton_catch_all
	{}
	end_try;

	return event;
}

#pragma mark -

- (void) exportLocation: (RefArg) inEntry to: (icalcomponent *) outEvent
{
	RefVar item(GetFrameSlot(inEntry, MakeSymbol("mtgLocation")));

	if (NOTNIL(item))
	{
	/* item is a name reference:
		{	class: 'nameRef.meetingPlace,
			_entryClass: 'company,
			_alias: <to full Names entry>,
			company: "Alexander Bell’s" of class 'company,
			labels: nil
		} */

		const char * text = MakeUTF8String(GetFrameSlot(item, MakeSymbol("company")));
		icalcomponent_add_property(outEvent, icalproperty_new_location(text));
	}
}


- (void) exportInvitees: (RefArg) inEntry to: (icalcomponent *) outEvent
{
	RefVar item(GetFrameSlot(inEntry, MakeSymbol("mtgInvitees")));

	if (NOTNIL(item))
	{
		FOREACH(item, invitee)
		/* invitee is a name reference:
			{	class: 'nameRef.people,
				_entryClass: 'person,
				_alias: <to full Names entry>,
				name: { class: 'person, first: "Alexander", last: "Bell", honorific: nil },
				labels: 'Personal
			} */
			RefVar name(GetFrameSlot(invitee, MakeSymbol("name")));
			const char * nameStr = MakeUTF8Name(name);
			if (nameStr != NULL)
			{
				icalproperty * attendee = icalproperty_new_attendee("invalid:nomail");	// should set mailto: for attendee
				icalparameter * name = icalparameter_new_cn(nameStr);
				icalproperty_add_parameter(attendee, name);
				icalcomponent_add_property(outEvent, attendee);
			}
		END_FOREACH
	}
}


- (void) exportNotesData: (RefArg) inEntry to: (icalcomponent *) outEvent
{
	RefVar item(GetFrameSlot(inEntry, MakeSymbol("notesData")));

	if (NOTNIL(item))
	{
		// convert notes frame to text
		NSMutableString * noteText = [NSMutableString stringWithCapacity: 256];
		FOREACH(item, para)
			if (EQRef(GetFrameSlot(para, SYMA(viewStationery)), MakeSymbol("para")))
			{
				NSString * paraText = MakeNSString(GetFrameSlot(para, SYMA(text)));
				// no point collecting styles because ics description is plain text only
				if ([noteText length] > 0)
					[noteText appendFormat: @"\n%@", paraText];
				else
					[noteText setString: paraText];
			}
		END_FOREACH
		if ([noteText length] > 0)
			icalcomponent_add_property(outEvent, icalproperty_new_description([noteText UTF8String]));
	}
}


- (void) exportInstanceNotesData: (RefArg) inEntry to: (icalcomponent *) outEvent
{
	RefVar item(GetFrameSlot(inEntry, MakeSymbol("instanceNotesData")));

	if (NOTNIL(item))
	{
		/* item is an array of two-element arrays
			[
				date|time of meeting|event instance
				alias to another entry in this soup
			]
			we will have to resolve these aliases later... */
	}
}


- (void) exportRepeatInfo: (RefArg) inEntry to: (icalcomponent *) outEvent
{
	Ref repType, repInfo;

	if (NOTNIL(repType = GetFrameSlot(inEntry, MakeSymbol("repeatType")))
	&&  NOTNIL(repInfo = GetFrameSlot(inEntry, MakeSymbol("mtgInfo"))))
	{
		struct icalrecurrencetype recur;
		int32_t mtgInfo = RVALUE(repInfo) & 0x00FFFFFF;
		icalrecurrencetype_clear(&recur);
		switch (RVALUE(repType))
		{
		case kDayOfWeek:
		// mtgInfo is any day of week | any week in month
			if ((mtgInfo & kEveryday) == kEveryday)
//				repStr = [NSString stringWithFormat: @"FREQ=DAILY"];
				recur.freq = ICAL_DAILY_RECURRENCE;
			else
			{
//				repStr = [NSString stringWithFormat: @"FREQ=WEEKLY;BYDAY=%@", DayOfWeek(mtgInfo)];
				recur.freq = ICAL_WEEKLY_RECURRENCE;
				SetDaysOfWeek(mtgInfo, &recur);
			}
			break;
		case kWeekInMonth:
		// mtgInfo is one day of week | any week in month
//			repStr = [NSString stringWithFormat: @"FREQ=MONTHLY;BYDAY=%@;BYSETPOS=%@", DayOfWeek(mtgInfo), WeekOfMonth(mtgInfo)];
			recur.freq = ICAL_MONTHLY_RECURRENCE;
			SetDaysOfWeek(mtgInfo, &recur);
			break;
		case kDateInMonth:
		// mtgInfo is date in month
//			repStr = [NSString stringWithFormat: @"FREQ=MONTHLY;BYMONTHDAY=%i", mtgInfo];
			recur.freq = ICAL_MONTHLY_RECURRENCE;
			recur.by_month_day[0] = mtgInfo;
			recur.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
			break;
		case kDateInYear:
		// mtgInfo is set to (month<<8) + date
//			repStr = [NSString stringWithFormat: @"FREQ=YEARLY;BYMONTH=%i;BYMONTHDAY=%i", mtgInfo >> 8, mtgInfo & 0x1F];
			recur.freq = ICAL_YEARLY_RECURRENCE;
			recur.by_month[0] = mtgInfo >> 8;
			recur.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;
			recur.by_month_day[0] = mtgInfo & 0x1F;
			recur.by_month_day[1] = ICAL_RECURRENCE_ARRAY_MAX;
			break;
		case kPeriod:
		// mtgInfo is set to (mtgDay<<8) + period
//			*outStartDate = MakeNSDate((mtgInfo >> 8)*1440);
//			repStr = [NSString stringWithFormat: @"FREQ=DAILY;INTERVAL=%i", mtgInfo & 0xFF];
			recur.freq = ICAL_DAILY_RECURRENCE;
			recur.interval = mtgInfo & 0xFF;
			break;
		case kNever:
			break;
		case kWeekInYear:
		// mtgInfo is set to (month<<12) | one day of week | one week in month
//			repStr = [NSString stringWithFormat: @"FREQ=YEARLY;BYMONTH=%i;BYDAY=%@;BYSETPOS=%@", mtgInfo >> 12, DayOfWeek(mtgInfo), WeekOfMonth(mtgInfo)];
			recur.freq = ICAL_YEARLY_RECURRENCE;
			recur.by_month[0] = mtgInfo >> 12;
			recur.by_month[1] = ICAL_RECURRENCE_ARRAY_MAX;
			SetDaysOfWeek(mtgInfo, &recur);
			break;
		}

		RefVar stop(GetFrameSlot(inEntry, MakeSymbol("mtgStopDate")));
		if (NOTNIL(stop))
			recur.until = MakeiCalTime(stop);

		icalcomponent_add_property(outEvent, icalproperty_new_rrule(recur));
	}

	RefVar item(GetFrameSlot(inEntry, MakeSymbol("exceptions")));
	if (NOTNIL(item))
	{
		/* item is an array of two-element arrays
			[
				date|time of expected meeting|event instance
				nil => meeting|event has been erased; or {} containing changed mtgStartDate, mtgDuration
			]
			we will have to resolve these aliases later... */
	}
}


- (void) dealloc
{
	icalcomponent_free(calendar);
}


@end


/*------------------------------------------------------------------------------
	D a t e F r o m I C a l
------------------------------------------------------------------------------*/

enum
{
	kSingleMeeting,
	kSingleNote,
	kRepeatMeeting,
	kRepeatNote,
//	kToDoItem,
	kNumOfDateSoups
};

NSString * const kDateSoupNames[] = {@"Calendar", @"Calendar Notes", @"Repeat Meetings", @"Repeat Notes", @"To Do List"};
const SEL kSoupTranslators[] = { @selector(importMeeting:), @selector(importCribNote:), @selector(importRepeatingMeeting:), @selector(importRepeatingCribNote:), @selector(importToDo:) };


@interface DateFromICal : NCXTranslator
{
	RefStruct mtgFrame;
	icalcomponent * calendar;
	CList * dateSoup[kNumOfDateSoups];
	CListIterator * iter;
	int soupIndex;
	icalcomponent * currentComponent;
	SEL currentTranslator;
}
- (Ref) importMeeting: (icalcomponent *) inEvent;
- (Ref) importCribNote: (icalcomponent *) inEvent;
- (Ref) importRepeatingMeeting: (icalcomponent *) inEvent;
- (Ref) importRepeatingCribNote: (icalcomponent *) inEvent;
- (Ref) importToDo: (icalcomponent *) inEvent;

- (void) importAlarm: (icalcomponent *) inEvent into: (RefArg) outEntry;
- (void) importRepeatingAlarm: (icalcomponent *) inEvent into: (RefArg) outEntry;
- (void) importLocation: (icalcomponent *) inEvent into: (RefArg) outEntry;
- (void) importInvitees: (icalcomponent *) inEvent into: (RefArg) outEntry;
- (void) importNotesData: (icalcomponent *) inEvent into: (RefArg) outEntry;
- (void) importRepeatingNotesData: (icalcomponent *) inEvent into: (RefArg) outEntry;
- (void) importRepeatInfo: (icalcomponent *) inEvent into: (RefArg) outEntry;
@end


BOOL
IsRepeating(icalcomponent * inEvent)
{
	return (icalcomponent_get_first_property(inEvent, ICAL_RRULE_PROPERTY) != NULL);
}


BOOL
IsMeeting(icalcomponent * inEvent)
{
	icalproperty * p = icalcomponent_get_first_property(inEvent, ICAL_DTSTART_PROPERTY);
	struct icaltimetype t = icalproperty_get_dtstart(p);
	return !t.is_date;
}


@implementation DateFromICal

/*------------------------------------------------------------------------------
	Parse the iCal (ics) file and create ical components.
	Allocate each component to a soup depending upon its type.
	soups := ["Calendar", "Calendar Notes", "Repeat Meetings", "Repeat Notes", "To Do List"];
	Args:		inURL
				inDocument
	Return:	--
------------------------------------------------------------------------------*/

- (void) beginImport: (NSURL *) inURL context: (NCDocument *) inDocument
{
	[super beginImport: inURL context: inDocument];

	// construct C++ dynamic arrays
	int i;
	for (i = 0; i < kNumOfDateSoups; i++)
		dateSoup[i] = CList::make();
	iter = nil;

	calendar = icalparser_parse_string((const char *)[[NSData dataWithContentsOfURL: inURL] bytes]);

	// build arrays of events by type
	icalcomponent * c;
	for (c = icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT);
		  c != NULL;
		  c = icalcomponent_get_next_component(calendar, ICAL_VEVENT_COMPONENT))
	{
		i = IsMeeting(c) ? kSingleMeeting : kSingleNote;
		if (IsRepeating(c))
			i += 2;
		dateSoup[i]->insert(c);
	}
//	for (c = icalcomponent_get_first_component(calendar, ICAL_VTODO_COMPONENT);
//		  c != NULL;
//		  c = icalcomponent_get_next_component(calendar, ICAL_VTODO_COMPONENT))
//	{
//		dateSoup[kToDoItem]->insert(c);
//	}
	// set up iterator
	soupIndex = -1;
	currentComponent = NULL;
}


/*------------------------------------------------------------------------------
	Read one iCal (ics) event and convert to Calendar soup entry.
	soups := ["Calendar", "Calendar Notes", "Repeat Meetings", "Repeat Notes", "To Do List"];
	Args:		--
	Return:	Calendar soup entry
------------------------------------------------------------------------------*/

- (Ref) import
{
	RefVar entry;
//	NCSession * session = theContext.dock.session;

	if (NOTNIL(mtgFrame))	// location|invitees need to be set up
	{
//PrintObject(mtgFrame, 0);
//		[session callExtension: 'meet' with: mtgFrame];											// <-- can we do this w/ local db?
		mtgFrame = NILREF;
	}

	// fetch the next item parsed from the .ics file
	// NULL currentComponent => we haven’t fetched the firstItem() yet
	if (currentComponent != NULL)
		currentComponent = (icalcomponent *) iter->nextItem();
	// NULL => end of list; we need to fetch the firstItem() in the next dateSoup
	if (currentComponent == NULL)
		while (soupIndex < (kNumOfDateSoups-1) && currentComponent == NULL)
		{
			soupIndex++;
			currentTranslator = kSoupTranslators[soupIndex];
			if (iter)
				delete iter;
			iter = new CListIterator(dateSoup[soupIndex]);	// NSEnumerator
			currentComponent = (icalcomponent *) iter->firstItem();
			if (currentComponent != NULL)
			{
				// we have entries for this soup
				// set the soup name so they are imported to the right soup, not the generic Calendar
				theContext.pluginController.importSoupName = kDateSoupNames[soupIndex];
			}
			else
				theContext.pluginController.importSoupName = nil;
		}
	if (currentComponent != NULL)
{
//REPprintf(icalcomponent_as_ical_string(currentComponent));
		entry = (Ref)[self performSelector: currentTranslator withObject: (__bridge id)currentComponent];
//PrintObject(entry, 0);
}
	return entry;
}


/*------------------------------------------------------------------------------
	Convert icalcomponent to Calendar soup entry of viewStationery type 'meeting.
	This entry represents a single meeting.

	Slots
	mtgStartDate
	mtgDuration		minutes
	mtgText			rich string
	mtgAlarm
	mtgInvitees		[nameRef] use GetMeetingInvitees
	mtgLocation		 nameRef  use GetMeetingLocation
	notesData		[note]

	Args:		inEvent			an icalcomponent object
	Return:	Calendar soup entry
------------------------------------------------------------------------------*/

- (Ref) importMeeting: (icalcomponent *) inEvent
{
	RefVar entry(AllocateFrame());
	SetFrameSlot(entry, SYMA(class), MakeSymbol("meeting"));
	SetFrameSlot(entry, SYMA(viewStationery), MakeSymbol("meeting"));

	newton_try
	{
		struct icaltimetype theTime;
		struct icaldurationtype theDuration;
		const char * text;

// required slots
		theTime = icalcomponent_get_dtstart(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgStartDate"), MakeDate(theTime));

		theDuration = icalcomponent_get_duration(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgDuration"), MAKEINT(icaldurationtype_as_int(theDuration)/60));

		text = icalcomponent_get_summary(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgText"), MakeStringFromUTF8String(text));

// optional slots
		[self importAlarm: inEvent into: entry];
		[self importLocation: inEvent into: entry];
		[self importInvitees: inEvent into: entry];
		[self importNotesData: inEvent into: entry];
	}
	newton_catch_all
	{}
	end_try;

	return entry;
}


/*------------------------------------------------------------------------------
	Convert icalcomponent to Repeat Meetings soup entry of viewStationery type
	'repeatingMeeting.
	This entry represents a repeating meeting.

	Slots
	mtgStartDate
	mtgDuration		minutes
	mtgText			rich string
	mtgAlarm
	mtgInvitees		[nameRef] use GetMeetingInvitees
	mtgLocation		 nameRef  use GetMeetingLocation
	instanceNotesData		[note]

	Args:		inEvent			an icalcomponent object
	Return:	Repeat Meetings soup entry
------------------------------------------------------------------------------*/

- (Ref) importRepeatingMeeting: (icalcomponent *) inEvent
{
	RefVar entry(AllocateFrame());
	SetFrameSlot(entry, SYMA(class), MakeSymbol("meeting"));
	SetFrameSlot(entry, SYMA(viewStationery), MakeSymbol("repeatingMeeting"));

	newton_try
	{
		struct icaltimetype theTime;
		struct icaldurationtype theDuration;
		const char * text;

// required slots
		theTime = icalcomponent_get_dtstart(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgStartDate"), MakeDate(theTime));

		theDuration = icalcomponent_get_duration(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgDuration"), MAKEINT(icaldurationtype_as_int(theDuration)/60));

		text = icalcomponent_get_summary(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgText"), MakeStringFromUTF8String(text));

		[self importRepeatInfo: inEvent into: entry];

// optional slots
		[self importRepeatingAlarm: inEvent into: entry];
		[self importLocation: inEvent into: entry];
		[self importInvitees: inEvent into: entry];
		[self importRepeatingNotesData: inEvent into: entry];
	}
	newton_catch_all
	{}
	end_try;

	return entry;
}


/*------------------------------------------------------------------------------
	Convert icalcomponent to Calendar Notes soup entry of viewStationery type
	'cribNote.
	This entry represents a single note.

	Slots
	mtgStartDate
	mtgText			rich string
	mtgAlarm
	notesData		[note]

	Args:		inEvent			an icalcomponent object
	Return:	Calendar Notes soup entry
------------------------------------------------------------------------------*/

- (Ref) importCribNote: (icalcomponent *) inEvent
{
	RefVar entry(AllocateFrame());
	SetFrameSlot(entry, SYMA(class), MakeSymbol("meeting"));
	SetFrameSlot(entry, SYMA(viewStationery), MakeSymbol("cribNote"));

	newton_try
	{
		struct icaltimetype theTime;
		const char * text;

// required slots
		theTime = icalcomponent_get_dtstart(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgStartDate"), MakeDate(theTime));

		SetFrameSlot(entry, MakeSymbol("mtgDuration"), MAKEINT(0));

		text = icalcomponent_get_summary(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgText"), MakeStringFromUTF8String(text));

// optional slots
		[self importAlarm: inEvent into: entry];
		[self importNotesData: inEvent into: entry];
	}
	newton_catch_all
	{}
	end_try;

	return entry;
}


/*------------------------------------------------------------------------------
	Convert icalcomponent to Repeat Notes soup entry of viewStationery type
	'cribNote.
	This entry represents a repeating note.

	Slots
	mtgStartDate
	mtgText			rich string
	mtgAlarm
	instanceNotesData		[alias]

	Args:		inEvent			an icalcomponent object
	Return:	Repeat Notes soup entry
------------------------------------------------------------------------------*/

- (Ref) importRepeatingCribNote: (icalcomponent *) inEvent
{
	RefVar entry(AllocateFrame());
	SetFrameSlot(entry, SYMA(class), MakeSymbol("meeting"));
	SetFrameSlot(entry, SYMA(viewStationery), MakeSymbol("cribNote"));

	newton_try
	{
		struct icaltimetype theTime;
		const char * text;

// required slots
		theTime = icalcomponent_get_dtstart(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgStartDate"), MakeDate(theTime));

		SetFrameSlot(entry, MakeSymbol("mtgDuration"), MAKEINT(0));

		text = icalcomponent_get_summary(inEvent);
		SetFrameSlot(entry, MakeSymbol("mtgText"), MakeStringFromUTF8String(text));

		[self importRepeatInfo: inEvent into: entry];

// optional slots
		[self importRepeatingAlarm: inEvent into: entry];
		[self importRepeatingNotesData: inEvent into: entry];
	}
	newton_catch_all
	{}
	end_try;

	return entry;
}


/*------------------------------------------------------------------------------
	Convert icalcomponent to To Do List soup entry.
	This entry represents a to do list item.
	I think we need to gather items with the same start date and put them all in
	the topics array for one soup entry with that date.
	Or maybe treat todos completely separately, with their own plugin?

	Slots
	class				'todo
	needsSort		true?
	date
	topics			[{}]

	Args:		inEvent			an icalcomponent object
	Return:	To Do List soup entry
------------------------------------------------------------------------------*/

- (Ref) importToDo: (icalcomponent *) inEvent
{
	return NILREF;
}


#pragma mark -

- (void) importAlarm: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalcomponent * alarm;
	icalproperty * actionProperty;
	icalproperty * triggerProperty;
	enum icalproperty_action action;

	if ((alarm = icalcomponent_get_first_component(inEvent, ICAL_VALARM_COMPONENT)) != NULL
	&&  (triggerProperty = icalcomponent_get_first_property(alarm, ICAL_TRIGGER_PROPERTY)) != NULL
	&&  (actionProperty = icalcomponent_get_first_property(alarm, ICAL_ACTION_PROPERTY)) != NULL	// ML Calendar adds an alarm to EVERY event w/ ACTION:NONE so ignore those
	&&  (action = icalproperty_get_action(actionProperty)) != ICAL_ACTION_X)
	{
		icaltimetype alarmTime;
		struct icaltriggertype trigger = icalproperty_get_trigger(triggerProperty);
		if (!icaltime_is_null_time(trigger.time))
			alarmTime = trigger.time;		// time for single alarm
		else if (!icaldurationtype_is_null_duration(trigger.duration))
			alarmTime = icaltime_add(icalcomponent_get_dtstart(inEvent), trigger.duration);	// offset for repeating alarm
		SetFrameSlot(outEntry, MakeSymbol("mtgAlarm"), MakeDate(alarmTime));
	}
}


- (void) importRepeatingAlarm: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalcomponent * alarm;
	icalproperty * actionProperty;
	icalproperty * triggerProperty;
	enum icalproperty_action action;

	if ((alarm = icalcomponent_get_first_component(inEvent, ICAL_VALARM_COMPONENT)) != NULL
	&&  (triggerProperty = icalcomponent_get_first_property(alarm, ICAL_TRIGGER_PROPERTY)) != NULL
	&&  (actionProperty = icalcomponent_get_first_property(alarm, ICAL_ACTION_PROPERTY)) != NULL	// ML Calendar adds an alarm to EVERY event w/ ACTION:NONE so ignore those
	&&  (action = icalproperty_get_action(actionProperty)) != ICAL_ACTION_X)
	{
		int alarmOffset = 0;
		struct icaltriggertype trigger = icalproperty_get_trigger(triggerProperty);
		if (!icaldurationtype_is_null_duration(trigger.duration))
			alarmOffset = icaldurationtype_as_int(trigger.duration);	// offset for repeating alarm
		SetFrameSlot(outEntry, MakeSymbol("mtgAlarm"), MAKEINT(-alarmOffset/60));
	}
}


- (void) importLocation: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalproperty * property;

	if ((property = icalcomponent_get_first_property(inEvent, ICAL_LOCATION_PROPERTY)) != NULL)
	{
		const char * locationStr = icalproperty_get_location(property);

		if (ISNIL(mtgFrame))
			mtgFrame = MakeMeetingFrame(outEntry);
		SetFrameSlot(mtgFrame, MakeSymbol("illocation"), MakeStringFromUTF8String(locationStr));
	}
}


- (void) importInvitees: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalproperty * property;

	if ((property = icalcomponent_get_first_property(inEvent, ICAL_ATTENDEE_PROPERTY)) != NULL)
	{
		RefVar invitees(MakeArray(0));
	// array of:
	//		{ class: 'person, first: "Alexander", last: "Bell" }

		for ( ; property != NULL; property = icalcomponent_get_next_property(inEvent, ICAL_ATTENDEE_PROPERTY))
		{
			icalparameter * param = icalproperty_get_first_parameter(property, ICAL_CN_PARAMETER);
			const char * nameStr = icalparameter_get_cn(param);
			size_t nameStrLen;
			if ((nameStrLen = strlen(nameStr)) == 0)
				nameStr = icalproperty_get_attendee(property);
			if ((nameStrLen = strlen(nameStr)) > 0)
			{
			// strip enclosing ""
				if (nameStr[0] == '"' && nameStr[nameStrLen-1] == '"')
				{
					((char *)nameStr)[nameStrLen-1] = 0;
					nameStr++;
				}
			}

			RefVar name(AllocateFrame());
			SetFrameSlot(name, SYMA(class), MakeSymbol("person"));
//			SetFrameSlot(name, SYMA(title), NILREF);
			char * lastNameStr = strrchr(nameStr, ' ');
			if (lastNameStr == NULL)
				SetFrameSlot(name, SYMA(first), MakeStringFromUTF8String(nameStr));
			else
			{
				char c = *lastNameStr;
				*lastNameStr = 0;
				SetFrameSlot(name, SYMA(first), MakeStringFromUTF8String(nameStr));
				SetFrameSlot(name, MakeSymbol("last"), MakeStringFromUTF8String(lastNameStr+1));
				*lastNameStr = c;
			}
//			SetFrameSlot(name, SYMA(honorific), NILREF);
			AddArraySlot(invitees, name);
		}

		if (ISNIL(mtgFrame))
			mtgFrame = MakeMeetingFrame(outEntry);
		SetFrameSlot(mtgFrame, MakeSymbol("ilinvitees"), invitees);
	}
}


- (void) importNotesData: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalproperty * property;

	if ((property = icalcomponent_get_first_property(inEvent, ICAL_DESCRIPTION_PROPERTY)) != NULL)
	{
		const char * noteStr = icalproperty_get_description(property);

		RefVar notes(MakeArray(1));

		RefVar para(Clone(MAKEMAGICPTR(267)));
		Rect bounds;
		SetRect(&bounds, 5, 5, 320-5, 500+5);
		SetFrameSlot(para, SYMA(text), MakeStringFromUTF8String(noteStr));
		SetFrameSlot(para, SYMA(viewBounds), ToObject(&bounds));
		SetFrameSlot(para, SYMA(viewFont), [theContext userFontRef]);

		SetArraySlot(notes, 0, para);

		SetFrameSlot(outEntry, MakeSymbol("notesData"), notes);
	}
}


- (void) importRepeatingNotesData: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalproperty * property;

	if ((property = icalcomponent_get_first_property(inEvent, ICAL_DESCRIPTION_PROPERTY)) != NULL)
	{
/*
		const char * noteStr = icalproperty_get_description(property);
		RefVar notes(MakeArray(1));

		RefVar para(Clone(MAKEMAGICPTR(267));
		Rect bounds;
		SetRect(&bounds, 5, 5, 320-5, 500+5);
		SetFrameSlot(para, SYMA(text), MakeStringFromUTF8String(noteStr));
		SetFrameSlot(para, SYMA(viewBounds), ToObject(&bounds));
		SetFrameSlot(para, SYMA(viewFont), [theContext userFontRef]);

		SetArraySlot(notes, 0, para);

		// item is an array of two-element arrays
		//	[
		//		date|time of meeting|event instance
		//		alias to another entry in this soup
		//	]

		SetFrameSlot(outEntry, MakeSymbol("instanceNotesData"), notes);
*/
	}
}


- (void) importRepeatInfo: (icalcomponent *) inEvent into: (RefArg) outEntry
{
	icalproperty * property;

	if ((property = icalcomponent_get_first_property(inEvent, ICAL_RRULE_PROPERTY)) != NULL)
	{
		int repType = kNever, repInfo = 0;
		int month, day;
		struct icalrecurrencetype recur = icalproperty_get_rrule(property);
		switch (recur.freq)
		{
		case ICAL_NO_RECURRENCE:			// we can’t handle these; but placate the compiler
		case ICAL_SECONDLY_RECURRENCE:
		case ICAL_MINUTELY_RECURRENCE:
		case ICAL_HOURLY_RECURRENCE:
			break;

		case ICAL_DAILY_RECURRENCE:
			if (recur.interval == 1)
			{
				repType = kDayOfWeek;
				repInfo = kEveryday + kEveryWeek;
			}
			else
			{
				repType = kPeriod;
				repInfo = recur.interval + ((int)(RVALUE(GetFrameSlot(outEntry, MakeSymbol("mtgStartDate")))/1440) << 8);
			}
			break;
		case ICAL_WEEKLY_RECURRENCE:
			if ((day = recur.by_day[0]) != ICAL_RECURRENCE_ARRAY_MAX)
			{
				repType = kDayOfWeek;
			// mtgInfo is any day of week | any week in month
				repInfo = kEveryWeek;
				for (int i = 0; (day = recur.by_day[i]) != ICAL_RECURRENCE_ARRAY_MAX; i++)
					repInfo |= kWeekDayLookup[day & 0x07];
			}
			break;
		case ICAL_MONTHLY_RECURRENCE:
			if ((day = recur.by_day[0]) != ICAL_RECURRENCE_ARRAY_MAX)
			{
				if (recur.interval == 12)
				{
					repType = kWeekInYear;
				// mtgInfo is set to (month<<12) | one day of week | one week in month
					struct icaltimetype mtgStartTime = icalcomponent_get_dtstart(inEvent);
					int wom = (day >> 3) + 1;
					if (wom < 0)
						wom = 0;
					repInfo = (mtgStartTime.month << 12) + kWeekDayLookup[day & 0x07] + kWeekLookup[wom];
				}
				else
				{
					repType = kWeekInMonth;
				// mtgInfo is one day of week | any week in month
					int wom = (day >> 3) + 1;
					if (wom < 0)
						wom = 0;
					repInfo = kWeekDayLookup[day & 0x07] + kWeekLookup[wom];
				}
			}
			else if ((day = recur.by_month_day[0]) != ICAL_RECURRENCE_ARRAY_MAX)
			{
				repType = kDateInMonth;
			// mtgInfo is date in month
				repInfo = day;
			}
			break;
		case ICAL_YEARLY_RECURRENCE:
			if ((month = recur.by_month[0]) != ICAL_RECURRENCE_ARRAY_MAX)
			{
				if ((day = recur.by_month_day[0]) != ICAL_RECURRENCE_ARRAY_MAX)
				{
					repType = kDateInYear;
				// mtgInfo is set to (month<<8) + date
					repInfo = (month << 8) + day;
				}
				else if ((day = recur.by_day[0]) != ICAL_RECURRENCE_ARRAY_MAX)
				{
					repType = kWeekInYear;
				// mtgInfo is set to (month<<12) | one day of week | one week in month
					int wom = (day >> 3) + 1;
					if (wom < 0)
						wom = 0;
					repInfo = (month << 12) + kWeekDayLookup[day & 0x07] + kWeekLookup[wom];
				}
			}
		//	bymonth or byday may not be specified
		//	in which case assume repeat on the same month/day as the start
			if (repType == kNever)
			{
				struct icaltimetype theTime;
				theTime = icalcomponent_get_dtstart(inEvent);
				repType = kDateInYear;
				repInfo = (theTime.month << 8) + theTime.day;
			}
			break;
		}

		if (repType == kNever)
			NSLog(@"Cannot translate recurrence rule.");

		SetFrameSlot(outEntry, MakeSymbol("repeatType"), MAKEINT(repType));
		SetFrameSlot(outEntry, MakeSymbol("mtgInfo"), MAKEINT(repInfo));

		SetFrameSlot(outEntry, MakeSymbol("mtgStopDate"), !icaltime_is_null_time(recur.until) ? MakeDate(recur.until) : MAKEINT(kEndOfTime));

/*		if ((property = icalcomponent_get_first_property(inEvent, ICAL_EXRULE_PROPERTY)) != NULL)
		{
			struct icalrecurrencetype recur = icalproperty_get_exrule(property);
			RefVar except(MakeArray(0));
			while ()
			{
				RefVar item(MakeArray(2));
				// item is an array of two-element arrays
				//	[
				//		date|time of expected meeting|event instance
				//		nil => meeting|event has been erased; or {} containing changed mtgStartDate, mtgDuration
				//	]
				AddArraySlot(except, item);
			}
			SetFrameSlot(outEntry, MakeSymbol("exceptions"), except);
		}
*/
	}
}


- (void)importDone
{
	if (calendar)
		icalcomponent_free(calendar);

	// destruct C++ dynamic arrays
	if (iter)
		delete iter;
	for (int i = 0; i < kNumOfDateSoups; i++)
		delete dateSoup[i];
}

@end
