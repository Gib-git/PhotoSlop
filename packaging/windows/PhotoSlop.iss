; Inno Setup script for the Windows installer (built by the Release workflow).
; iscc /DAppVersion=0.5.0 /DAppSource=C:\full\path\dist\PhotoSlop /DOutputDir=C:\full\path\dist packaging\windows\PhotoSlop.iss
; Relative paths are resolved from this folder, so pass absolute ones.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppSource
  #define AppSource "..\..\dist\PhotoSlop"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6A1F3C52-8E0B-4B8B-9C55-2F0D9B7E4A11}
AppName=PhotoSlop
AppVersion={#AppVersion}
AppVerName=PhotoSlop {#AppVersion}
AppPublisher=PhotoSlop
DefaultDirName={autopf}\PhotoSlop
DefaultGroupName=PhotoSlop
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=PhotoSlop-{#AppVersion}-windows-x64-setup
SetupIconFile=PhotoSlop.ico
UninstallDisplayIcon={app}\PhotoSlop.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "openpsd"; Description: "Offer PhotoSlop in ""Open with"" for Photoshop (.psd) files"; GroupDescription: "File types:"

[Files]
Source: "{#AppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\PhotoSlop"; Filename: "{app}\PhotoSlop.exe"
Name: "{autodesktop}\PhotoSlop"; Filename: "{app}\PhotoSlop.exe"; Tasks: desktopicon

[Registry]
; .pslop documents open in PhotoSlop.
Root: HKA; Subkey: "Software\Classes\.pslop"; ValueType: string; ValueName: ""; ValueData: "PhotoSlop.Document"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\PhotoSlop.Document"; ValueType: string; ValueName: ""; ValueData: "PhotoSlop Document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\PhotoSlop.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\PhotoSlop.exe,0"
Root: HKA; Subkey: "Software\Classes\PhotoSlop.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\PhotoSlop.exe"" ""%1"""
; Photoshop files: listed under "Open with" without taking over the default.
Root: HKA; Subkey: "Software\Classes\.psd\OpenWithProgids"; ValueType: string; ValueName: "PhotoSlop.Document"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openpsd
Root: HKA; Subkey: "Software\Classes\Applications\PhotoSlop.exe\SupportedTypes"; ValueType: string; ValueName: ".psd"; ValueData: ""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\PhotoSlop.exe\SupportedTypes"; ValueType: string; ValueName: ".pslop"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\PhotoSlop.exe\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\PhotoSlop.exe\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\PhotoSlop.exe\SupportedTypes"; ValueType: string; ValueName: ".tif"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\PhotoSlop.exe\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""

[Run]
Filename: "{app}\PhotoSlop.exe"; Description: "{cm:LaunchProgram,PhotoSlop}"; Flags: nowait postinstall skipifsilent
