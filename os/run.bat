@echo off
rem Starts NovaOS in the QEMU emulator (install it from qemu.org)
set QEMU="C:\Program Files\qemu\qemu-system-i386.exe"
if not exist %QEMU% set QEMU=qemu-system-i386
%QEMU% -kernel "%~dp0build\nova.bin"
