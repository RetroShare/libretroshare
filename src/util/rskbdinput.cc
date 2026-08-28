/*******************************************************************************
 * libretroshare/src/retroshare/util/rskbdinput.cc                             *
 *                                                                             *
 * Copyright (C) 2019  Cyril Soler <csoler@users.sourceforge.net>              *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

#ifndef __ANDROID__

#include <iostream>
#include <util/rskbdinput.h>

#ifdef WINDOWS_SYS
#include <conio.h>
#include <stdio.h>

#define PASS_MAX 512

namespace RsUtil {
std::string rs_getpass(const std::string& prompt,bool /*no_echo*/,bool *eof)
{
    static char getpassbuf [PASS_MAX + 1];
    size_t i = 0;
    int c;

    if(eof) *eof = false;

    if (!prompt.empty()) {
        std::cerr << prompt ;
        std::cerr.flush();
    }

    for (;;) {
        c = _getch ();

        // A closed console gives EOF for every call from then on. Without this
        // the loop only leaves through the PASS_MAX branch, returning 512 bytes
        // of 0xFF as if the user had typed them.
        if (c == EOF) {
            if(eof) *eof = true;
            getpassbuf [0] = '\0';
            i = 0;
            break;
        }

        // '\n' is what a redirected stdin carries, '\r' what the console gives.
        if (c == '\r' || c == '\n') {
            getpassbuf [i] = '\0';
            break;
        }
        else if (i < PASS_MAX) {
            getpassbuf[i++] = static_cast<char>(c);
        }

        if (i >= PASS_MAX) {
            getpassbuf [i] = '\0';
            break;
        }
    }

    if (!prompt.empty()) {
        std::cerr << "\r\n" ;
        std::cerr.flush();
    }

    return std::string(getpassbuf);
}
}
#else

#include <stdio.h>
#include <string>
#include <iostream>
#include <termios.h>
#include <unistd.h>

static int getch()
{
    int ch;
    struct termios t_old, t_new;

    // tcgetattr fails whenever stdin is not a terminal, leaving t_old
    // uninitialized: applying it below would push a random terminal
    // configuration onto whatever stdin happens to be.
    const bool isTerminal = (tcgetattr(STDIN_FILENO, &t_old) == 0);

    if(isTerminal)
    {
        t_new = t_old;
        t_new.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t_new);
    }

    ch = getchar();

    if(isTerminal)
        tcsetattr(STDIN_FILENO, TCSANOW, &t_old);

    return ch;
}

namespace RsUtil {

std::string rs_getpass(const std::string& prompt, bool no_echo, bool *eof)
{
  const int BACKSPACE=127;
  const int RETURN=10;

  std::string password;

  if(eof) *eof = false;

  std::cout <<prompt; std::cout.flush();

  // ch must be an int: getch() forwards getchar(), whose EOF is -1. Stored in
  // an unsigned char that becomes 0xFF, which never equals RETURN, so a closed
  // stdin made this loop append one 0xFF byte per iteration forever -- one core
  // at 100% and a string growing until the process is killed.
  int ch = 0;

  while((ch=getch())!=RETURN)
    {
       if(ch==EOF)
         {
            if(eof) *eof = true;
            password.clear();
            break;
         }

       if(ch==BACKSPACE)
         {
            if(password.length()!=0)
              {
                 if(no_echo)
                 std::cout <<"\b \b";
                 password.resize(password.length()-1);
              }
         }
       else
         {
             password+=static_cast<char>(ch);
             if(no_echo)
                 std::cout <<'*';
             else
                 std::cout << static_cast<char>(ch),std::cout.flush();
         }
    }
  std::cout <<std::endl;

  return std::string(password);
}
}
#endif

#endif // ndef __ANDROID__
