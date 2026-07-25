@echo off
setlocal
set QBRAIN_SCHEMA=D:\Projects\Qbrain\schema\001_init.sql
"D:\Projects\Qbrain\build\cl\qbrain.exe" serve --allow-write %*
