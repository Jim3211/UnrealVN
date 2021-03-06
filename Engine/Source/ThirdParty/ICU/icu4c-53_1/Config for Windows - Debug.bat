@echo off
REM Batch file for configuring Windows platforms of ICU
REM Run this from the ICU directory

REM Win32 Configs
if not exist ./Win32 mkdir Win32
cd ./Win32

	REM VS2012 Config
	if not exist ./VS2012 mkdir VS2012
	cd ./VS2012
		call "%VS110COMNTOOLS%..\..\VC\vcvarsall.bat" x86
		bash -c 'CFLAGS="/Zp4" CXXFLAGS="/Zp4" CPPFLAGS="-DUCONFIG_NO_TRANSLITERATION=1" ../../Source/runConfigureICU --enable-debug --disable-release Cygwin/MSVC --enable-static --enable-shared --with-library-bits=32 --with-data-packaging=files'
	cd ../

	REM VS2013 Config
	if not exist ./VS2013 mkdir VS2013
	cd ./VS2013
		call "%VS120COMNTOOLS%..\..\VC\vcvarsall.bat" x86
		bash -c 'CFLAGS="/Zp4" CXXFLAGS="/Zp4" CPPFLAGS="-DUCONFIG_NO_TRANSLITERATION=1" ../../Source/runConfigureICU --enable-debug --disable-release Cygwin/MSVC --enable-static --enable-shared --with-library-bits=32 --with-data-packaging=files'
	cd ../

REM Back to root
cd ../

REM Win64 Configs
if not exist ./Win64 mkdir Win64
cd ./Win64

	REM VS2012 Config
	if not exist ./VS2012 mkdir VS2012
	cd ./VS2012
		call "%VS110COMNTOOLS%..\..\VC\vcvarsall.bat" amd64
		bash -c 'CPPFLAGS="-DUCONFIG_NO_TRANSLITERATION=1" ../../Source/runConfigureICU --enable-debug --disable-release Cygwin/MSVC --enable-static --enable-shared --with-library-bits=64 --with-data-packaging=files'
	cd ../

	REM VS2013 Config
	if not exist ./VS2013 mkdir VS2013
	cd ./VS2013
		call "%VS120COMNTOOLS%..\..\VC\vcvarsall.bat" amd64
		bash -c 'CPPFLAGS="-DUCONFIG_NO_TRANSLITERATION=1" ../../Source/runConfigureICU --enable-debug --disable-release Cygwin/MSVC --enable-static --enable-shared --with-library-bits=64 --with-data-packaging=files'
	cd ../

REM Back to root
cd ../