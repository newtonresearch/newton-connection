/*
	File:		PreferenceKeys.h

	Contains:	Preference keys for the NCX app.

	Written by:	Newton Research Group, 2005.
*/

// Application
#define kNewton1SessionType	@"Newton1SessionType"
#define kNoDeleteWarningPref	@"DontShowDeleteWarning"
#define kNoAddWarningPref		@"DontShowAddWarning"
#define kNoSyncWarningPref		@"DontShowSyncWarning"
#define kUserFontName			@"UserFontName"
#define kUserFontSize			@"UserFontSize"

// Preference Panes
// General
#define kSetNewtonTimePref		@"SetNewtonClock"
#define kAutoBackupPref			@"BackUpOnAutoDock"
#define kSelectiveBackupPref	@"SelectiveBackup"
#define kAutoSyncPref			@"SyncOnAutoDock"

// Serial
#define kTCPIPPortPref			@"TCPIPPort"
#define kSerialPortPref			@"SerialPort"
#define kSerialBaudPref			@"BaudRate"

// Security
#define kPasswordPref			@"Password"

// Software Update
#define kAutoUpdatePref			@"SUPerformScheduledCheck"
#define kUpdateFreqPref			@"SUScheduledCheckInterval"

// ROM dump parameters
#define kROMAddrPref				@"ROMAddress"
#define kROMSizePref				@"ROMSize"

// Debug
#define kLogToFilePref			@"LogToFile"
#define kLogLevelPref			@"LogLevel"


// Not preference keys:
// Notifications
#define kSerialPortChanged		@"SerialPortChanged"
