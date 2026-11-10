REM SPDX-FileCopyrightText: BANANAPIE LIMITED
REM SPDX-License-Identifier: GPL-2.0-or-later
REM See LICENSES/GPL-2.0-or-later.txt for the full license text.

@echo off
python "%~dp0..\scripts\cici.py" build --config RelWithDebInfo %*
exit /b %errorlevel%
