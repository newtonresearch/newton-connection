/*
	File:		Names-Contacts.mm

	Contains:   Newton Names import/export functions.

	Written by: Newton Research Group, 2006.
*/

#import "NCXTranslator.h"
#import "PlugInUtilities.h"
#import "Contacts/Contacts.h"

extern void		SetRect(Rect * r, short left, short top, short right, short bottom);

NSString *	MakeNSName(RefArg inName);


/*------------------------------------------------------------------------------
	N a m e T o V C a r d
------------------------------------------------------------------------------*/

@interface NameToVCard : NCXTranslator
{
	NSDictionary * affiliateDict;
	NSMutableArray<CNContact *> * contacts;
}
@end


/*------------------------------------------------------------------------------
	Make a NextStep string of a person’s full name from a Newton name frame.
	(Also used in Dates plugin.)
	Args:		inPerson			AddressBook record
				inProperty		name of property to set
				inFrame			a NewtonScript frame eg for name or address
				inTag				slot to read
	Return:	--
------------------------------------------------------------------------------*/

NSString *
MakeNSName(RefArg inName)
{
	RefVar firstName(GetFrameSlot(inName, SYMA(first)));
	RefVar lastName(GetFrameSlot(inName, SYMA(last)));
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

	return fullNameStr;
}


/*------------------------------------------------------------------------------
	Make a CNPostalAddress address from a NewtonScript frame.
	Args:		inEntry			a NewtonScript address frame
	Return:	a CNPostalAddress
------------------------------------------------------------------------------*/

CNPostalAddress *
MakeAddress(RefArg inEntry)
{
	CNMutablePostalAddress * postalAddress = nil;

	RefVar address(GetFrameSlot(inEntry, SYMA(address)));
	RefVar address2(GetFrameSlot(inEntry, MakeSymbol("address2")));
	RefVar city(GetFrameSlot(inEntry, MakeSymbol("city")));
	RefVar region(GetFrameSlot(inEntry, SYMA(region)));
	RefVar postalCode(GetFrameSlot(inEntry, MakeSymbol("postal_code")));
	RefVar country(GetFrameSlot(inEntry, MakeSymbol("country")));

	NSString * addressStr = StrEmpty(address) ? nil : MakeNSString(address);
	NSString * address2Str = StrEmpty(address2) ? nil : MakeNSString(address2);
	NSString * cityStr = StrEmpty(city) ? nil : MakeNSString(city);
	NSString * regionStr = StrEmpty(region) ? nil : MakeNSString(region);
	NSString * postalCodeStr = StrEmpty(postalCode) ? nil : MakeNSString(postalCode);
	NSString * countryStr = StrEmpty(country) ? nil : MakeNSString(country);

	if (addressStr
	||  address2Str
	||  cityStr
	||  regionStr
	||  postalCodeStr
	||  countryStr) {
		postalAddress = [[CNMutablePostalAddress alloc] init];

		if (addressStr) {
			if (address2Str) {
				postalAddress.street = [NSString stringWithFormat: @"%@\n%@", addressStr, address2Str];
			} else {
				postalAddress.street = addressStr;
			}
		}
		else if (address2Str)
			postalAddress.street = address2Str;
		if (cityStr)
			postalAddress.city = cityStr;
		if (regionStr)
			postalAddress.state = regionStr;
		if (postalCodeStr)
			postalAddress.postalCode = postalCodeStr;
		if (countryStr)
			postalAddress.country = countryStr;
	}

	return postalAddress;
}


@implementation NameToVCard

/*------------------------------------------------------------------------------
	Name app soup entries are aggregated into a single vcf file on export.
	Args:		--
	Return:	YES
------------------------------------------------------------------------------*/

+ (BOOL)aggregatesEntries {
	return YES;
}


- (void)beginExport:(NSString *)inAppName context:(NCDocument *)inDocument destination:(NSURL *)inURL
{
	[super beginExport: inAppName context: inDocument destination: inURL];

	affiliateDict = [[NSBundle bundleForClass: [self class]] objectForInfoDictionaryKey: @"NCAffiliateNames"];
	contacts = [[NSMutableArray alloc] initWithCapacity:4];
}


/*------------------------------------------------------------------------------
	Make an AddressBook affiliate name from a Names entry person’s title.
	Args:		inTitle			the Names entry’s person’s title
	Return:	an autoreleased NSString
------------------------------------------------------------------------------*/

- (NSString *)lookUpAffiliation:(NSString *)inTitle
{
	NSString * affiliate;

	if (inTitle == nil
	||  affiliateDict == nil)
		affiliate = @"also";

	else
	{
		affiliate = [affiliateDict objectForKey: [inTitle lowercaseString]];
		if (affiliate == nil)
			affiliate = inTitle;
	}

	return affiliate;
}


/*------------------------------------------------------------------------------
	Convert Names soup entry of class 'owner, 'person or 'company to vCard.
	We can only deal with 'person, 'owner or 'company,
	anything else '[group, worksite] is ignored.
	Args:		inEntry			Notes soup entry
	Return:	--					NSData containing vCard is appended to instance data
------------------------------------------------------------------------------*/

- (NSString *)export:(RefArg)inEntry
{
	//	Before we do anything, check to see whether this is actually a useful entry.
	if (Length(inEntry) < 5)
		return nil;

	CNMutableContact * person = [[CNMutableContact alloc] init];
	NSMutableArray * multiValue;

	// name
	RefVar item;
	RefVar name(GetFrameSlot(inEntry, SYMA(name)));
	if (NOTNIL(name))
	{
		if (!StrEmpty(item = GetFrameSlot(name, SYMA(first))))
			person.givenName = MakeNSString(item);
		if (!StrEmpty(item = GetFrameSlot(name, SYMA(last))))
			person.familyName = MakeNSString(item);
		if (!StrEmpty(item = GetFrameSlot(name, MakeSymbol("honorific"))))
			person.namePrefix = MakeNSString(item);
		if (!StrEmpty(item = GetFrameSlot(name, SYMA(title))))
			person.nameSuffix = MakeNSString(item);
	}

	// organisation
	if (!StrEmpty(item = GetFrameSlot(name, SYMA(company))))
		person.organizationName = MakeNSString(item);
	if (!StrEmpty(item = GetFrameSlot(name, SYMA(title))))
		person.jobTitle = MakeNSString(item);

	person.contactType = CNContactTypePerson;		// or CNContactTypeOrganization

	// affiliates
	multiValue = [[NSMutableArray alloc] init];
	if (IsArray(item = GetFrameSlot(inEntry, MakeSymbol("names"))))
	FOREACH(item, name)
		if (IsFrame(name)) {
			NSString * nameStr = MakeNSName(name);
			if (nameStr != nil) {
				RefVar title(GetFrameSlot(name, SYMA(title)));
				NSString * titleStr = StrEmpty(title) ? nil : MakeNSString(title);
				NSString * affiliate = [self lookUpAffiliation:titleStr];
				[multiValue addObject:[CNLabeledValue labeledValueWithLabel:affiliate value:[CNContactRelation contactRelationWithName:nameStr]]];
			}
		}
	END_FOREACH
	if (multiValue.count > 0)
		person.contactRelations = multiValue;

	// address
	multiValue = [[NSMutableArray alloc] init];
	CNPostalAddress * postalAddress;
	if ((postalAddress = MakeAddress(inEntry)) != NULL)
		[multiValue addObject:[CNLabeledValue labeledValueWithLabel:CNLabelHome value:postalAddress]];

	if (IsArray(item = GetFrameSlot(inEntry, MakeSymbol("addresses"))))
	FOREACH(item, addressItem)
		if ((postalAddress = MakeAddress(addressItem)) != NULL) {
			[multiValue addObject:[CNLabeledValue labeledValueWithLabel:CNLabelHome value:postalAddress]];
		}
	END_FOREACH

	if (multiValue.count > 0)
		person.postalAddresses = multiValue;

	// phone
	multiValue = [[NSMutableArray alloc] init];
	if (IsArray(item = GetFrameSlot(inEntry, MakeSymbol("phones"))))
	FOREACH(item, phone)
		if (!StrEmpty(phone)) {
			RefVar phoneClass(ClassOf(phone));
			NSString * phoneLabel = CNLabelPhoneNumberMain;
			if (EQ(phoneClass, SYMA(homePhone)))
				phoneLabel = CNLabelHome;
			else if (EQ(phoneClass, SYMA(workPhone)))
				phoneLabel = CNLabelWork;
			else if (EQ(phoneClass, SYMA(faxPhone)))
				phoneLabel = CNLabelPhoneNumberWorkFax;
			else if (EQ(phoneClass, SYMA(carPhone)))
				phoneLabel = CNLabelPhoneNumberMobile;
			else if (EQ(phoneClass, SYMA(mobilePhone)))
				phoneLabel = CNLabelPhoneNumberMobile;
			else if (EQ(phoneClass, SYMA(homefaxPhone)))
				phoneLabel = CNLabelPhoneNumberHomeFax;
			[multiValue addObject:[CNLabeledValue labeledValueWithLabel:phoneLabel value:[CNPhoneNumber phoneNumberWithStringValue:MakeNSString(phone)]]];
		}
	END_FOREACH

	// pager
	if (IsArray(item = GetFrameSlot(inEntry, MakeSymbol("pagers"))))
	FOREACH(item, pagerItem)
		if (IsFrame(pagerItem)) {
			RefVar pager(GetFrameSlot(pagerItem, MakeSymbol("pagerNum")));
			if (!StrEmpty(pager))
				[multiValue addObject:[CNLabeledValue labeledValueWithLabel:CNLabelPhoneNumberPager value:[CNPhoneNumber phoneNumberWithStringValue:MakeNSString(pager)]]];
		}
	END_FOREACH

	if (multiValue.count > 0)
		person.phoneNumbers = multiValue;

	// e-mail
	//•TO DO• normalize email addresses
	multiValue = [[NSMutableArray alloc] init];
	if (!StrEmpty(item = GetFrameSlot(inEntry, MakeSymbol("email")))) {
		[multiValue addObject:[CNLabeledValue labeledValueWithLabel:CNLabelWork value:MakeNSString(item)]];
	}

	if (IsArray(item = GetFrameSlot(inEntry, MakeSymbol("emailAddrs"))))
	FOREACH(item, emailItem)
		if (IsFrame(emailItem)) {
			RefVar email(GetFrameSlot(emailItem, MakeSymbol("email")));
			if (!StrEmpty(email)) {
				[multiValue addObject:[CNLabeledValue labeledValueWithLabel:CNLabelWork value:MakeNSString(email)]];
			}
		}
	END_FOREACH

	if (multiValue.count > 0)
		person.emailAddresses = multiValue;

	// URL
	multiValue = [[NSMutableArray alloc] init];
	if (IsArray(item = GetFrameSlot(inEntry, MakeSymbol("URLs"))))
	FOREACH(item, urlItem)
		if (!StrEmpty(urlItem)) {
			[multiValue addObject:[CNLabeledValue labeledValueWithLabel:CNLabelWork value:MakeNSString(urlItem)]];
		}
	END_FOREACH
	if (multiValue.count > 0)
		person.urlAddresses = multiValue;

	// birthday
	if (ISINT(item = GetFrameSlot(inEntry, MakeSymbol("bday")))) {
		person.birthday = MakeNSDateComponents(item);
	}

	// anniversary
	if (ISINT(item = GetFrameSlot(inEntry, MakeSymbol("anniversary")))) {
		NSMutableArray * dates = [[NSMutableArray alloc] initWithCapacity:1];
		[dates addObject:[CNLabeledValue labeledValueWithLabel:@"anniversary" value:MakeNSDateComponents(item)]];
		person.dates = dates;
	}

	// notes
	item = GetFrameSlot(inEntry, MakeSymbol("notes"));
	if (IsArray(item) && Length(item) > 0) {
		NSMutableString * note = [NSMutableString stringWithCapacity:256];
		FOREACH(item, para)
			RefVar paraText = GetFrameSlot(para, SYMA(text));
			if (!StrEmpty(paraText)) {
				NSMutableString * paraStr = [NSMutableString stringWithString: MakeNSString(paraText)];
			//	strip CATEGORIES: ... \n which we don’t want to re-export to Address Book
				NSRange categoriesText = [paraStr rangeOfString: @"CATEGORIES:"];
				if (categoriesText.location != NSNotFound) {
				// we might want actually to set the categories
					categoriesText = [paraStr lineRangeForRange: categoriesText];
					[paraStr deleteCharactersInRange: categoriesText];
				}
				if ([paraStr length] > 0) {
					NSRange fullRange = NSMakeRange(0, [paraStr length]);
					[paraStr replaceOccurrencesOfString: @"\r\n" withString: @"\n" options: 0 range: fullRange];
					[paraStr replaceOccurrencesOfString: @"\r"   withString: @"\n" options: 0 range: fullRange];

					if ([note length] > 0) {
						[note appendString: @"\n"];
					}
					[note appendString: paraStr];
				}
			}
		END_FOREACH
		person.note = note;
	}

	[contacts addObject:person];
	return nil;
}


- (NSString *)exportDone:(NSString *)inAppName
{
	NSError * __autoreleasing error;
	NSData * exportedData = [CNContactVCardSerialization dataWithContacts:contacts error:&error];
	return [self write:exportedData toFile:[NSString stringWithFormat:@"Newton %@", inAppName] extension:@".vcf"];
}

@end

#pragma mark -

/*------------------------------------------------------------------------------
	N a m e F r o m V C a r d
------------------------------------------------------------------------------*/

@interface NameFromVCard : NCXTranslator
{
	NSArray<CNContact *> * contacts;
	ArrayIndex index;
}
@end


/*------------------------------------------------------------------------------
	Set an AddressBook property into a NewtonScript frame.
	Args:		inFrame			a NewtonScript frame eg for name or address
				inTag				slot to set
				inPerson			AddressBook record
				inProperty		name of property to extract
	Return:	--
------------------------------------------------------------------------------*/

static Ref
MakeAddress(RefArg ioEntry, CNPostalAddress * inPostalAddress)
{
	// split address into address2 if newline
	NSString * value = inPostalAddress.street;
	if (value != nil && value.length > 0) {
		NSRange delim = [value rangeOfCharacterFromSet: [NSCharacterSet newlineCharacterSet]];
		if (delim.location == NSNotFound) {
			SetFrameSlot(ioEntry, SYMA(address), MakeString(value));
		} else {
			SetFrameSlot(ioEntry, SYMA(address), MakeString([value substringToIndex: delim.location]));
			SetFrameSlot(ioEntry, MakeSymbol("address2"), MakeString([value substringFromIndex: delim.location+1]));
		}
	}
	if (inPostalAddress.city != nil && inPostalAddress.city.length > 0)
		SetFrameSlot(ioEntry, MakeSymbol("city"), MakeString(inPostalAddress.city));
	if (inPostalAddress.state != nil && inPostalAddress.state.length > 0)
		SetFrameSlot(ioEntry, SYMA(region), MakeString(inPostalAddress.state));
	if (inPostalAddress.postalCode != nil && inPostalAddress.postalCode.length > 0)
		SetFrameSlot(ioEntry, MakeSymbol("postal_code"), MakeString(inPostalAddress.postalCode));
	if (inPostalAddress.country != nil && inPostalAddress.country.length > 0)
		SetFrameSlot(ioEntry, MakeSymbol("country"), MakeString(inPostalAddress.country));

	return ioEntry;
}


const struct PhoneLookup
{
	NSString * label;
	const char * phoneClass;
} phoneLookup[] = 
{
	{ CNLabelWork, "workPhone" },
	{ CNLabelHome, "homePhone" },
	{ CNLabelPhoneNumberWorkFax, "faxPhone" },
	{ CNLabelPhoneNumberHomeFax, "homeFaxPhone" },
	{ CNLabelPhoneNumberMobile, "mobilePhone" },
	{ nil, NULL }
};


@implementation NameFromVCard

- (void)beginImport:(NSURL *)inURL context:(NCDocument *)inDocument
{
	[super beginImport:inURL context:inDocument];

	NSError * __autoreleasing error;
	NSData * data = [NSData dataWithContentsOfURL:inURL];
	contacts = [CNContactVCardSerialization contactsWithData:data error:&error];
	index = 0;
}


/*------------------------------------------------------------------------------
	Convert vCard to Names soup entry.
	Since a vCard can contain many individuals, this method can be called many
	times, passing a context structure, until it returns NILREF.
	Args:		--
	Return:	Calendar soup entry
------------------------------------------------------------------------------*/

- (Ref)import
{
	if (index < contacts.count) {
		CNContact * person = contacts[index];
		RefVar theEntry;
		NSMutableArray * multiValue;
		ArrayIndex i, count;

		theEntry = AllocateFrame();
		SetFrameSlot(theEntry, SYMA(class), SYMA(person));

		// name
		RefVar name(AllocateFrame());
		SetFrameSlot(name, SYMA(class), SYMA(person));

		if (person.givenName != nil && person.givenName.length > 0) {
			SetFrameSlot(name, SYMA(first), MakeString(person.givenName));
		}
		if (person.familyName != nil && person.familyName.length > 0) {
			SetFrameSlot(name, SYMA(last), MakeString(person.familyName));
		}
		if (person.namePrefix != nil && person.namePrefix.length > 0) {
			SetFrameSlot(name, MakeSymbol("honorific"), MakeString(person.namePrefix));
		}
		if (person.nameSuffix != nil && person.nameSuffix.length > 0) {
			SetFrameSlot(name, SYMA(title), MakeString(person.nameSuffix));
		}
		SetFrameSlot(theEntry, SYMA(name), name);

		// organisation
		if (person.organizationName != nil && person.organizationName.length > 0) {
			SetFrameSlot(name, SYMA(company), MakeString(person.organizationName));
		}
		if (person.jobTitle != nil && person.jobTitle.length > 0) {
			SetFrameSlot(name, SYMA(title), MakeString(person.jobTitle));
		}

		// sorting
		NSString * sortStr = @"";
		NSString * firstNameStr = person.givenName;
		NSString * lastNameStr = person.familyName;
		if (firstNameStr != nil) {
			if (lastNameStr != nil)
				sortStr = [NSString stringWithFormat: @"%@ %@", lastNameStr, firstNameStr];
			else
				sortStr = firstNameStr;
		} else if (lastNameStr != nil) {
			sortStr = lastNameStr;
		} else {
			// we have no name
			SetFrameSlot(theEntry, SYMA(class), SYMA(company));
			sortStr = person.organizationName;
		}

		RefVar sorton(MakeString(sortStr));
		SetClass(sorton, SYMA(name));
		SetFrameSlot(theEntry, MakeSymbol("sorton"), sorton);

		// affiliates
		multiValue = person.contactRelations;
		if (multiValue != nil && (count = (ArrayIndex)multiValue.count) > 0)
		{
			RefVar names(MakeArray(count));
			for (i = 0; i < count; i++)
			{
				CNLabeledValue * value = multiValue[i];
				NSString * affiliation = value.label;
				if ([affiliation hasPrefix: @"_$!<"] && [affiliation hasSuffix: @">!$_"])
					affiliation = [affiliation substringWithRange: NSMakeRange(4, [affiliation length]-8)];
				name = AllocateFrame();
				SetFrameSlot(name, SYMA(class), MakeSymbol("person"));
				SetFrameSlot(name, SYMA(title), MakeString(affiliation));
				CNContactRelation * affiliate = value.value;
				NSString * nameStr = affiliate.name;
				NSRange delim = [nameStr rangeOfCharacterFromSet: [NSCharacterSet whitespaceCharacterSet]];
				if (delim.location == NSNotFound)
					SetFrameSlot(name, SYMA(first), MakeString(nameStr));
				else
				{
					SetFrameSlot(name, SYMA(first), MakeString([nameStr substringToIndex: delim.location]));
					SetFrameSlot(name, SYMA(last), MakeString([nameStr substringFromIndex: delim.location+1]));
				}
				SetArraySlot(names, i, name);
			}
			SetFrameSlot(theEntry, MakeSymbol("names"), names);
		}

		// address
		multiValue = person.postalAddresses;
		if (multiValue != nil && (count = (ArrayIndex)multiValue.count) > 0) {
			CNLabeledValue * value = multiValue[0];
			CNPostalAddress * postalAddress = value.value;
			MakeAddress(theEntry, postalAddress);
			if (count > 1) {
				RefVar addrs(MakeArray(count-1));
				for (i = 1; i < count; i++) {
					RefVar addr(AllocateFrame());
					value = multiValue[i];
					postalAddress = value.value;
					MakeAddress(addr, postalAddress);
					SetArraySlot(addrs, i-1, addr);
				}
				SetFrameSlot(theEntry, MakeSymbol("addresses"), addrs);
			}
		}

		// phone
		multiValue = person.phoneNumbers;
		if (multiValue != nil && (count = (ArrayIndex)multiValue.count) > 0) {
			RefVar phones(MakeArray(0));
			RefVar pagers(MakeArray(0));
			for (i = 0; i < count; i++)
			{
				CNLabeledValue * value = multiValue[i];
				NSString * label = value.label;
				CNPhoneNumber * phone = value.value;
				RefVar phoneNumber(MakeString(phone.stringValue));

				// pager
				if ([label isEqualToString:CNLabelPhoneNumberPager]) {
					RefVar pager(AllocateFrame());
					SetFrameSlot(pager, MakeSymbol("pagerNum"), phoneNumber);
					AddArraySlot(pagers, pager);
				} else {
					RefVar phoneClass = SYMA(phone);
					const PhoneLookup * p;
					for (p = phoneLookup; p->label != nil; p++) {
						if (label == p->label) {
							phoneClass = MakeSymbol(p->phoneClass);
							break;
						}
					}
					SetClass(phoneNumber, phoneClass);
					AddArraySlot(phones, phoneNumber);
				}
			}
			if (Length(phones) > 0)
				SetFrameSlot(theEntry, MakeSymbol("phones"), phones);
			if (Length(pagers) > 0)
				SetFrameSlot(theEntry, MakeSymbol("pagers"), pagers);
		}

		// e-mail
		multiValue = person.emailAddresses;
		if (multiValue != nil && (count = (ArrayIndex)multiValue.count) > 0) {
			CNLabeledValue * value = multiValue[0];
			SetFrameSlot(theEntry, MakeSymbol("email"), MakeString(value.value));
			if (count > 1) {
				RefVar emailAddrs(MakeArray(count-1));
				for (i = 1; i < count; i++) {
					RefVar addr(AllocateFrame());
					value = multiValue[i];
					SetFrameSlot(addr, MakeSymbol("email"), MakeString(value.value));
					SetArraySlot(emailAddrs, i-1, addr);
				}
				SetFrameSlot(theEntry, MakeSymbol("emailAddrs"), emailAddrs);
			}
		}

		// URL
		multiValue = person.urlAddresses;
		if (multiValue != nil && (count = (ArrayIndex)multiValue.count) > 0) {
			RefVar URLs(MakeArray(count));
			for (i = 0; i < count; i++) {
				CNLabeledValue * value = multiValue[i];
				SetArraySlot(URLs, i, MakeString(value.value));
			}
			SetFrameSlot(theEntry, MakeSymbol("URLs"), URLs);
		}

		// birthday
		NSDateComponents * bdayValue = person.birthday;
		if (bdayValue != nil)
			SetFrameSlot(theEntry, MakeSymbol("bday"), MakeDate([[NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian] dateFromComponents:bdayValue]));

		// anniversary
		multiValue = person.dates;
		if (multiValue != nil && (count = (ArrayIndex)multiValue.count) > 0) {
			for (i = 0; i < count; i++) {
				CNLabeledValue * value = multiValue[i];
				if ([value.label isEqualToString:@"_$!<anniversary>!$_"]) {
					NSDateComponents * dateValue = value.value;
					SetFrameSlot(theEntry, MakeSymbol("anniversary"), MakeDate([[NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian] dateFromComponents:dateValue]));
					break;
				}
			}
		}

		// notes
		NSString * value = person.note;
		if (value != nil && value.length > 0) {
			RefVar notes(MakeArray(1));

			RefVar para(Clone(MAKEMAGICPTR(267)));
			Rect bounds;
			SetRect(&bounds, 10, 10, 310, 100);
			SetFrameSlot(para, SYMA(text), MakeString(value));
			SetFrameSlot(para, SYMA(viewBounds), ToObject(&bounds));
		//	SetFrameSlot(para, SYMA(viewFont), [session getUserFont]);

			SetArraySlot(notes, 0, para);
			SetFrameSlot(theEntry, MakeSymbol("notes"), notes);
		}

		// map labels to categories?
		SetFrameSlot(theEntry, SYMA(labels), RA(NILREF));

		SetFrameSlot(theEntry, SYMA(version), MAKEINT(2));
		SetFrameSlot(theEntry, MakeSymbol("cardType"), MAKEINT(1));

		++index;
		return theEntry;
	}
	return NILREF;
}


- (void)importDone
{
	;
}

@end

