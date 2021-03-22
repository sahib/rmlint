/*
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
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef RM_GUI_H
#define RM_GUI_H

/**
 * @file gui.h
 * @brief Launch rmlint gui session (python)
 **/

/**
 * @brief Launch rmlint gui session (python)
 *
 * @param argc arg count passed from main
 * @param argv command line args; argv[0] is "shredder"
 * @retval EXIT_SUCCESS or EXIT_FAILURE
 *
 **/
int rm_gui_launch(int argc, const char **argv);

#endif /* end of include guard */
