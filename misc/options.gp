s    / server               flag    "start in server mode"
NONE / messageAddress       string  {"ws://localhost"}  "message server hostname or IP address"
NONE / messagePort          int     8080  "message server port"
NONE / stunAddress          string  {"stun.l.google.com"}  "STUN server hostname or IP address"
NONE / stunPort             int     19302 "STUN server port"
n    / name                 string  {""}  "client name"
d    / directory            string  {""}  "directory containing files"
r    / room                 string  {"DefaultRoom#1"}  "message room name"
f    / file                 string  {""}  "file to transfer"

#usage_begin
usage: __PROGRAM_NAME__ [options]
WebRTC file transfer client/server.
__GLOSSARY__
#usage_end
