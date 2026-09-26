/******************************************************************************
**
** parse_cl.cpp
**
** Sat Sep 26 09:32:06 2026
** Linux 6.8.0-51-generic (#52-Ubuntu SMP PREEMPT_DYNAMIC Thu Dec  5 13:09:44 UTC 2024) x86_64
** jrichard@SpinRun42 (Jason Richards)
**
** Definition of command line parser class
**
** Automatically created by genparse v0.9.3
**
** See http://genparse.sourceforge.net for details and updates
**
******************************************************************************/

#include <getopt.h>
#include <stdlib.h>
#include "parse_cl.h"

/*----------------------------------------------------------------------------
**
** Cmdline::Cmdline ()
**
** Constructor method.
**
**--------------------------------------------------------------------------*/

Cmdline::Cmdline (int argc, char *argv[]) 
{
  extern char *optarg;
  extern int optind;
  int c;

  static struct option long_options[] =
  {
    {"server", no_argument, NULL, 's'},
    {"messageAddress", required_argument, NULL, 256},
    {"messagePort", required_argument, NULL, 257},
    {"stunAddress", required_argument, NULL, 258},
    {"stunPort", required_argument, NULL, 259},
    {"name", required_argument, NULL, 'n'},
    {"directory", required_argument, NULL, 'd'},
    {"room", required_argument, NULL, 'r'},
    {"file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  _program_name += argv[0];

  /* default values */
  _server = false;
  _messageAddress = "ws://localhost";
  _messagePort = 8080;
  _stunAddress = "stun.l.google.com";
  _stunPort = 19302;
  _room = "DefaultRoom#1";
  _help = false;
  _version = false;

  optind = 0;
  while ((c = getopt_long (argc, argv, "sn:d:r:f:hv", long_options, &optind)) != - 1)
    {
      switch (c)
        {
        case 's': 
          _server = true;
          break;

        case 256: 
          _messageAddress = optarg;
          break;

        case 257: 
          _messagePort = atoi (optarg);
          break;

        case 258: 
          _stunAddress = optarg;
          break;

        case 259: 
          _stunPort = atoi (optarg);
          break;

        case 'n': 
          _name = optarg;
          break;

        case 'd': 
          _directory = optarg;
          break;

        case 'r': 
          _room = optarg;
          break;

        case 'f': 
          _file = optarg;
          break;

        case 'h': 
          _help = true;
          this->usage (EXIT_SUCCESS);
          break;

        case 'v': 
          _version = true;
          break;

        default:
          this->usage (EXIT_FAILURE);

        }
    } /* while */

  _optind = optind;
}

/*----------------------------------------------------------------------------
**
** Cmdline::usage ()
**
** Print out usage information, then exit.
**
**--------------------------------------------------------------------------*/

void Cmdline::usage (int status)
{
  if (status != EXIT_SUCCESS)
    std::cerr << "Try `" << _program_name << " --help' for more information.\n";
  else
    {
      std::cout << "\
usage: " << _program_name << " [options]\n\
WebRTC file transfer client/server.\n\
   [ -s ] [ --server ] (type=FLAG)\n\
          start in server mode\n\
   [ --messageAddress ] (type=STRING, default=ws://localhost)\n\
          message server hostname or IP address\n\
   [ --messagePort ] (type=INTEGER, default=8080)\n\
          message server port\n\
   [ --stunAddress ] (type=STRING, default=stun.l.google.com)\n\
          STUN server hostname or IP address\n\
   [ --stunPort ] (type=INTEGER, default=19302)\n\
          STUN server port\n\
   [ -n ] [ --name ] (type=STRING)\n\
          client name\n\
   [ -d ] [ --directory ] (type=STRING)\n\
          directory containing files\n\
   [ -r ] [ --room ] (type=STRING, default=DefaultRoom#1)\n\
          message room name\n\
   [ -f ] [ --file ] (type=STRING)\n\
          file to transfer\n\
   [ -h ] [ --help ] (type=FLAG)\n\
          Display this help and exit.\n\
   [ -v ] [ --version ] (type=FLAG)\n\
          Output version information and exit.\n";
    }
  exit (status);
}
