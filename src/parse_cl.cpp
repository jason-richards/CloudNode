/******************************************************************************
**
** parse_cl.cpp
**
** Tue Sep 22 05:47:18 2026
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
    {"messageAddress", required_argument, NULL, 256},
    {"messagePort", required_argument, NULL, 257},
    {"stunAddress", required_argument, NULL, 258},
    {"stunPort", required_argument, NULL, 259},
    {"name", required_argument, NULL, 'n'},
    {"destination", required_argument, NULL, 'd'},
    {"room", required_argument, NULL, 'r'},
    {"file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
  };

  _program_name += argv[0];

  /* default values */
  //_messageAddress = "ws://localhost";
  _messagePort = 8080;
  _stunAddress = "stun.l.google.com";
  _stunPort = 19302;
  _r = "DefaultRoom#1";
  _h = false;
  _v = false;

  optind = 0;
  while ((c = getopt_long (argc, argv, "n:d:r:f:hv", long_options, &optind)) != - 1)
    {
      switch (c)
        {
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
          _n = optarg;
          break;

        case 'd': 
          _d = optarg;
          break;

        case 'r': 
          _r = optarg;
          break;

        case 'f': 
          _f = optarg;
          break;

        case 'h': 
          _h = true;
          this->usage (EXIT_SUCCESS);
          break;

        case 'v': 
          _v = true;
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
Cloud server client configuration.\n\
  [ --messageAddress ] (type=STRING)\n\
  [ --messagePort ] (type=INTEGER, default=8080)\n\
   [ --stunAddress ] (type=STRING, default=stun.l.google.com)\n\
   [ --stunPort ] (type=INTEGER, default=19302)\n\
   [ -n ] [ --name ] (type=STRING)\n\
   [ -d ] [ --destination ] (type=STRING)\n\
   [ -r ] [ --room ] (type=STRING)\n\
   [ -f ] [ --file ] (type=STRING)\n\
   [ -h ] [ --help ] (type=FLAG)\n\
          Display this help and exit.\n\
   [ -v ] [ --version ] (type=FLAG)\n\
          Output version information and exit.\n";
    }
  exit (status);
}
