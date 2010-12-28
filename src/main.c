/**
*  This file is part of rmlint.
*
*  rmlint is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  rmlint is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with rmlint.  If not, see <http://www.gnu.org/licenses/>.
*
** Author: Christopher Pahl <sahib@online.de>:
** Hosted on http://github.com/sahib/rmlint
*
**/

#include "rmlint.h"

/* Only call the methods */
/* Original plan was to provide an API, but this would need some work */
int main(int argc, char **argv)
{
    /* Init */
    rmlint_settings mySettings;
    rmlint_set_default_settings(&mySettings);

    /* Parse commandline */
    if(rmlint_parse_arguments(argc,argv,&mySettings) == 0)
    {
        return -1;
    }

    /* do all the real work */
    return rmlint_main();
}
