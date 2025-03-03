#!/bin/sh

PROGRESS_CURR=0
PROGRESS_TOTAL=0                           

# This file was autowritten by rmlint
# rmlint was executed from: %s
# Your command line was: %s

RMLINT_BINARY="%s"

# Only use sudo if we're not root yet:
# (See: https://github.com/sahib/rmlint/issues/27://github.com/sahib/rmlint/issues/271)
SUDO_COMMAND="sudo"
if [ "$(id -u)" -eq "0" ]
then
  SUDO_COMMAND=""
fi

USER='%s'
GROUP='%s'

# Set to true on -n
DO_DRY_RUN=

# Set to true on -p
DO_PARANOID_CHECK=

# Set to true on -r
DO_CLONE_READONLY=

# Set to true on -q
DO_SHOW_PROGRESS=true

# Set to true on -c
DO_DELETE_EMPTY_DIRS=

# Set to true on -k
DO_KEEP_DIR_TIMESTAMPS=

# Set to true on -i
DO_ASK_BEFORE_DELETE=

# Tempfiles for saving timestamps
STAMPFILE=
STAMPFILE2=

##################################
# GENERAL LINT HANDLER FUNCTIONS #
##################################

COL_RED='\e[0;31m'
COL_BLUE='\e[1;34m'
COL_GREEN='\e[0;32m'
COL_YELLOW='\e[0;33m'
COL_RESET='\e[0m'

exit_cleanup() {
    trap - INT TERM EXIT
    if [ -n "$STAMPFILE" ]; then
        rm -f -- "$STAMPFILE"
    fi
    if [ -n "$STAMPFILE2" ]; then
        rm -f -- "$STAMPFILE2"
    fi
}

trap exit_cleanup EXIT
trap exit INT TERM

print_progress_prefix() {
    if [ -n "$DO_SHOW_PROGRESS" ]; then
        PROGRESS_PERC=0
        if [ $((PROGRESS_TOTAL)) -gt 0 ]; then
            PROGRESS_PERC=$((PROGRESS_CURR * 100 / PROGRESS_TOTAL))
        fi
        printf %s "${COL_BLUE}" "$PROGRESS_PERC" "${COL_RESET}"
        if [ $# -eq "1" ]; then
            PROGRESS_CURR=$((PROGRESS_CURR+$1))
        else
            PROGRESS_CURR=$((PROGRESS_CURR+1))
        fi
    fi
}

# NOTE: In the template file, these must be written %%s to avoid interpretation.
handle_emptyfile() {
    print_progress_prefix
    printf "${COL_GREEN}Deleting empty file:${COL_RESET} %%s\n" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        rm -f "$1"
    fi
}

handle_emptydir() {
    print_progress_prefix
    printf "${COL_GREEN}Deleting empty directory:${COL_RESET} %%s\n" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        rmdir "$1"
    fi
}

handle_bad_symlink() {
    print_progress_prefix
    printf "${COL_GREEN} Deleting symlink pointing nowhere:${COL_RESET} %%s\n" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        rm -f "$1"
    fi
}

handle_unstripped_binary() {
    print_progress_prefix
    printf "${COL_GREEN} Stripping debug symbols of:${COL_RESET} %%s\n" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        strip -s "$1"
    fi
}

handle_bad_user_id() {
    print_progress_prefix
    printf "${COL_GREEN}chown %%s${COL_RESET} %%s\n" "$USER" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        chown -- "$USER" "$1"
    fi
}

handle_bad_group_id() {
    print_progress_prefix
    printf "${COL_GREEN}chgrp %%s${COL_RESET} %%s\n" "$GROUP" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        chgrp -- "$GROUP" "$1"
    fi
}

handle_bad_user_and_group_id() {
    print_progress_prefix
    printf "${COL_GREEN}chown %%s:%%s${COL_RESET} %%s\n" "$USER" "$GROUP" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        chown -- "$USER:$GROUP" "$1"
    fi
}

###############################
# DUPLICATE HANDLER FUNCTIONS #
###############################

check_for_equality() {
    if [ -f "$1" ]; then
        # Use the more lightweight builtin `cmp` for regular files:
        cmp -s -- "$1" "$2"
    else
        # Fallback to `rmlint --equal` for directories:
        "$RMLINT_BINARY" -p --equal %s -- "$1" "$2"
    fi
}

original_check() {
    if [ ! -e "$2" ]; then
        printf "${COL_RED}^^^^^^ Error: original has disappeared - cancelling.....${COL_RESET}\n"
        return 1
    fi

    if [ ! -e "$1" ]; then
        printf "${COL_RED}^^^^^^ Error: duplicate has disappeared - cancelling.....${COL_RESET}\n"
        return 1
    fi

    # Check they are not the exact same file (hardlinks allowed):
    if [ "$1" = "$2" ]; then
        printf "${COL_RED}^^^^^^ Error: original and duplicate point to the *same* path - cancelling.....${COL_RESET}\n"
        return 1
    fi

    # Do double-check if requested:
    if [ -z "$DO_PARANOID_CHECK" ]; then
        return 0
    else
        if ! check_for_equality "$1" "$2"; then
            printf "${COL_RED}^^^^^^ Error: files no longer identical - cancelling.....${COL_RESET}\n"
            return 1
        fi
    fi
}

cp_symlink() {
    print_progress_prefix
    printf "${COL_YELLOW}Symlinking to original: ${COL_RESET}%%s\n" "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            # replace duplicate with symlink
            mv -- "$1" "$1.temp"
            if ln -s "$2" "$1"; then
                # make the symlink's mtime the same as the original
                touch -mr "$2" -h "$1"
                rm -rf -- "$1.temp"
            else
               # Failed to link file, move back:
                mv -- "$1.temp" "$1"
            fi
        fi
    fi
}

cp_hardlink() {
    if [ -d "$1" ]; then
        # for duplicate dir's, can't hardlink so use symlink
        cp_symlink "$@"
        return $?
    fi

    print_progress_prefix
    printf "${COL_YELLOW}Hardlinking to original: ${COL_RESET}%%s\n" "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            # replace duplicate with hardlink
            mv -- "$1" "$1.temp"
            if ln "$2" "$1"; then
                rm -rf -- "$1.temp"
            else
               # Failed to link file, move back:
                mv -- "$1.temp" "$1"
            fi
        fi
    fi
}

cp_reflink() {
    if [ -d "$1" ]; then
        # for duplicate dir's, can't clone so use symlink
        cp_symlink "$@"
        return $?
    fi
    print_progress_prefix
    # reflink $1 to $2's data, preserving $1's  mtime
    printf "${COL_YELLOW}Reflinking to original: ${COL_RESET}%%s\n" "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            if [ -z "$STAMPFILE2" ]; then
                STAMPFILE2=$(mktemp "${TMPDIR:-/tmp}/rmlint.XXXXXXXX.stamp")
            fi
            touch -mr "$1" -- "$STAMPFILE2"
            if [ -d "$1" ]; then
                rm -rf -- "$1"
            fi
            cp --archive --reflink=always -- "$2" "$1"
            touch -mr "$STAMPFILE2" -- "$1"
        fi
    fi
}

clone() {
    print_progress_prefix
    # clone $1 from $2's data
    # note: no original_check() call because rmlint --dedupe takes care of this
    printf "${COL_YELLOW}Cloning to: ${COL_RESET}%%s\n" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        if [ -n "$DO_CLONE_READONLY" ]; then
            $SUDO_COMMAND $RMLINT_BINARY --dedupe %s --readonly -- "$2" "$1"
        else
            $RMLINT_BINARY --dedupe %s -- "$2" "$1"
        fi
    fi
}

skip_hardlink() {
    print_progress_prefix
    printf "${COL_BLUE}Leaving as-is (already hardlinked to original): ${COL_RESET}%%s\n" "$1"
}

skip_reflink() {
    print_progress_prefix
    printf "${COL_BLUE}Leaving as-is (already reflinked to original): ${COL_RESET}%%s\n" "$1"
}

user_command() {
    print_progress_prefix

    printf "${COL_YELLOW}Executing user command: ${COL_RESET}%%s\n" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        # You can define this function to do what you want:
        %s
    fi
}

remove_cmd() {
    print_progress_prefix
    printf "${COL_YELLOW}Deleting: ${COL_RESET}%%s\n" "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            if [ -n "$DO_KEEP_DIR_TIMESTAMPS" ]; then
                touch -r "$(dirname "$1")" -- "$STAMPFILE"
            fi
            if [ -n "$DO_ASK_BEFORE_DELETE" ]; then
              rm -ri -- "$1"
            else
              rm -rf -- "$1"
            fi
            if [ -n "$DO_KEEP_DIR_TIMESTAMPS" ]; then
                # Swap back old directory timestamp:
                touch -r "$STAMPFILE" -- "$(dirname "$1")"
            fi

            if [ -n "$DO_DELETE_EMPTY_DIRS" ]; then
                DIR=$(dirname "$1")
                while [ -z "$(ls -A -- "$DIR")" ]; do
                    print_progress_prefix 0
                    printf "${COL_GREEN}Deleting resulting empty dir: ${COL_RESET}%%s\n" "$DIR"
                    rmdir -- "$DIR"
                    DIR=$(dirname "$DIR")
                done
            fi
        fi
    fi
}

original_cmd() {
    print_progress_prefix
    printf "${COL_GREEN}Keeping:  ${COL_RESET}%%s\n" "$1"
}

##################
# OPTION PARSING #
##################

ask() {
    cat << EOF

This script will delete certain files rmlint found.
It is highly advisable to view the script first!

Rmlint was executed in the following way:

   $ %s

Execute this script with -d to disable this informational message.
Type any string to continue; CTRL-C, Enter or CTRL-D to abort immediately
EOF
    read -r eof_check
    if [ -z "$eof_check" ]
    then
        # Count Ctrl-D and Enter as aborted too.
        printf "${COL_RED}Aborted on behalf of the user.${COL_RESET}\n"
        exit 1;
    fi
}

usage() {
    cat << EOF
usage: $0 OPTIONS

OPTIONS:

  -h   Show this message.
  -d   Do not ask before running.
  -x   Keep rmlint.sh; do not autodelete it.
  -p   Recheck that files are still identical before removing duplicates.
  -r   Allow deduplication of files on read-only btrfs snapshots. (requires sudo)
  -n   Do not perform any modifications, just print what would be done. (implies -d and -x)
  -c   Clean up empty directories while deleting duplicates.
  -q   Do not show progress.
  -k   Keep the timestamp of directories when removing duplicates.
  -i   Ask before deleting each file
EOF
}

DO_REMOVE=
DO_ASK=

while getopts "dhxnrpqcki" OPTION
do
  case $OPTION in
     h)
       usage
       exit 0
       ;;
     d)
       DO_ASK=false
       ;;
     x)
       DO_REMOVE=false
       ;;
     n)
       DO_DRY_RUN=true
       DO_REMOVE=false
       DO_ASK=false
       DO_ASK_BEFORE_DELETE=false
       ;;
     r)
       DO_CLONE_READONLY=true
       ;;
     p)
       DO_PARANOID_CHECK=true
       ;;
     c)
       DO_DELETE_EMPTY_DIRS=true
       ;;
     q)
       DO_SHOW_PROGRESS=
       ;;
     k)
       DO_KEEP_DIR_TIMESTAMPS=true
       ;;
     i)
       DO_ASK_BEFORE_DELETE=true
       ;;
     *)
       usage
       exit 1
  esac
done

if [ -z $DO_REMOVE ]
then
    printf "#${COL_YELLOW} ///${COL_RESET}This script will be deleted after it runs${COL_YELLOW}///${COL_RESET}\n"
fi

if [ -z $DO_ASK ]
then
  usage
  ask
fi

if [ -n "$DO_DRY_RUN" ]; then
    printf "#${COL_YELLOW} ////////////////////////////////////////////////////////////${COL_RESET}\n"
    printf "#${COL_YELLOW} /// ${COL_RESET} This is only a dry run; nothing will be modified! ${COL_YELLOW}///${COL_RESET}\n"
    printf "#${COL_YELLOW} ////////////////////////////////////////////////////////////${COL_RESET}\n"
elif [ -n "$DO_KEEP_DIR_TIMESTAMPS" ]; then
    STAMPFILE=$(mktemp "${TMPDIR:-/tmp}/rmlint.XXXXXXXX.stamp")
fi

######### START OF AUTOGENERATED OUTPUT #########


