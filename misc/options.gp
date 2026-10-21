s    / server               flag                            "start in server mode"
t    / timeout              int     10                      "ping/pong timeout interval."
m    / maxMessageSize       int     2097152                 "message buffer size (2MB default)."
NONE / messageAddress       string  {"ws://localhost"}      "message server hostname or IP address"
NONE / messagePort          int     8080                    "message server port"
NONE / stunAddress          string  {"stun.l.google.com"}   "STUN server hostname or IP address"
NONE / stunPort             int     19302                   "STUN server port"
n    / name                 string  {""}                    "client name"
d    / directory            string  {""}                    "directory containing files"
r    / room                 string  {"DefaultRoom#1"}       "message room name"
f    / file                 string  {""}                    "files to send as comma-separated peer:room:path entries"

#usage_begin
usage: __PROGRAM_NAME__ [options]
WebRTC file transfer client/server.
__GLOSSARY__
#usage_end
