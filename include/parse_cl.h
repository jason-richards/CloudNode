/******************************************************************************
**
** parse_cl.h
**
** Tue Sep 22 05:47:18 2026
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
  std::string _messageAddress;
  int _messagePort;
  std::string _stunAddress;
  int _stunPort;
  std::string _n;
  std::string _d;
  std::string _r;
  std::string _f;
  bool _h;
  bool _v;

  /* other stuff to keep track of */
  std::string _program_name;
  int _optind;

public:
  /* constructor and destructor */
  Cmdline (int, char **);
  ~Cmdline (){}

  /* usage function */
  void usage (int status);

  /* return next (non-option) parameter */
  int next_param () { return _optind; }

  std::string messageAddress () const { return _messageAddress; }
  int messagePort () const { return _messagePort; }
  std::string stunAddress () const { return _stunAddress; }
  int stunPort () const { return _stunPort; }
  std::string n () const { return _n; }
  std::string d () const { return _d; }
  std::string r () const { return _r; }
  std::string f () const { return _f; }
  bool h () const { return _h; }
  bool v () const { return _v; }
};

#endif
