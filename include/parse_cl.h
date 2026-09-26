/******************************************************************************
**
** parse_cl.h
**
** Sat Sep 26 09:32:06 2026
** Linux 6.8.0-51-generic (#52-Ubuntu SMP PREEMPT_DYNAMIC Thu Dec  5 13:09:44 UTC 2024) x86_64
** jrichard@SpinRun42 (Jason Richards)
**
** Header file for command line parser class
**
** Automatically created by genparse v0.9.3
**
** See http://genparse.sourceforge.net for details and updates
**
******************************************************************************/

#ifndef CMDLINE_H
#define CMDLINE_H

#include <iostream>
#include <string>

/*----------------------------------------------------------------------------
**
** class Cmdline
**
** command line parser class
**
**--------------------------------------------------------------------------*/

class Cmdline
{
private:
  /* parameters */
  bool _server;
  std::string _messageAddress;
  int _messagePort;
  std::string _stunAddress;
  int _stunPort;
  std::string _name;
  std::string _directory;
  std::string _room;
  std::string _file;
  bool _help;
  bool _version;

  /* other stuff to keep track of */
  std::string _program_name;
  int _optind;

public:
  /* constructor and destructor */
  Cmdline (int, char **) ;
  ~Cmdline (){}

  /* usage function */
  void usage (int status);

  /* return next (non-option) parameter */
  int next_param () { return _optind; }

  bool server () const { return _server; }
  std::string messageAddress () const { return _messageAddress; }
  int messagePort () const { return _messagePort; }
  std::string stunAddress () const { return _stunAddress; }
  int stunPort () const { return _stunPort; }
  std::string name () const { return _name; }
  std::string directory () const { return _directory; }
  std::string room () const { return _room; }
  std::string file () const { return _file; }
  bool help () const { return _help; }
  bool version () const { return _version; }
};

#endif
