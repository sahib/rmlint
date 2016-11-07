#!/bin/sh
# This file was autowritten by rmlint
# rmlint was executed from: %s
# Your command line was: %s

USER='%s'
GROUP='%s'

# Set to true on -n
DO_DRY_RUN=

# Set to true on -p
DO_PARANOID_CHECK=

# Set to true on -r
DO_CLONE_READONLY=

##################################
# GENERAL LINT HANDLER FUNCTIONS #
##################################


handle_emptyfile() {
    echo 'Deleting empty file:' "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        rm -f "$1"
    fi
}

handle_emptydir() {
    echo 'Deleting empty directory:' "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        rmdir "$1"
    fi
}

handle_bad_symlink() {
    echo 'Deleting symlink pointing nowhere:' "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        rm -f "$1"
    fi
}

handle_unstripped_binary() {
    echo 'Stripping debug symbols of:' "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        strip -s "$1"
    fi
}

handle_bad_user_id() {
    echo 'chown' "$USER" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        chown "$USER" "$1"
    fi
}

handle_bad_group_id() {
    echo 'chgrp' "$GROUP" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        chgrp "$GROUP" "$1"
    fi
}

handle_bad_user_and_group_id() {
    echo 'chown' "$USER:$GROUP" "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        chown "$USER:$GROUP" "$1"
    fi
}

###############################
# DUPLICATE HANDLER FUNCTIONS #
###############################

original_check() {
    if [ ! -e "$2" ]; then
        echo "^^^^^^ Error: original has disappeared - cancelling....."
        return 1
    fi

    if [ ! -e "$1" ]; then
        echo "^^^^^^ Error: duplicate has disappeared - cancelling....."
        return 1
    fi

    # Check they are not the exact same file (hardlinks allowed):
    if [ "$1" = "$2" ]; then
        echo "^^^^^^ Error: original and duplicate point to the *same* path - cancelling....."
        return 1
    fi

    # Do double-check if requested:
    if [ -z "$DO_PARANOID_CHECK" ]; then
        return 0
    else
        if cmp -s "$1" "$2"; then
            return 0
        else
            echo "^^^^^^ Error: files no longer identical - cancelling....."
            return 1
        fi
    fi
}

cp_hardlink() {
    echo 'Hardlinking to original:' "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            cp --remove-destination --archive --link "$2" "$1"
        fi
    fi
}

cp_symlink() {
    echo 'Symlinking to original:' "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            touch -mr "$1" "$0"
            cp --remove-destination --archive --symbolic-link "$2" "$1"
            touch -mr "$0" "$1"
        fi
    fi
}

cp_reflink() {
    # reflink $1 to $2's data, preserving $1's  mtime
    echo 'Reflinking to original:' "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            touch -mr "$1" "$0"
            cp --reflink=always "$2" "$1"
            touch -mr "$0" "$1"
        fi
    fi
}

clone() {
    # clone $1 from $2's data
    echo 'Cloning to: ' "$1"
    if [ -z "$DO_DRY_RUN" ]; then
        if [ -n "$DO_CLONE_READONLY" ]; then
            sudo rmlint --btrfs-clone -r "$2" "$1"
        else
            rmlint --btrfs-clone "$2" "$1"
        fi
    fi
}

skip_hardlink() {
    echo 'Leaving as-is (already hardlinked to original):' "$1"
}

skip_reflink() {
    echo 'Leaving as-is (already reflinked to original):' "$1"
}

user_command() {
    # You can define this function to do what you want:
    %s
}

remove_cmd() {
    echo 'Deleting:' "$1"
    if original_check "$1" "$2"; then
        if [ -z "$DO_DRY_RUN" ]; then
            rm -rf "$1"
        fi
    fi
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
    read eof_check
    if [ -z "$eof_check" ]
    then
        # Count Ctrl-D and Enter as aborted too.
        echo "Aborted on behalf of the user."
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
  -r   Allow btrfs-clone to clone to read-only snapshots (requires sudo)
  -n   Do not perform any modifications, just print what would be done.
EOF
}

DO_REMOVE=
DO_ASK=

while getopts "dhxnrp" OPTION
do
  case $OPTION in
     h)
       usage
       exit 1
       ;;
     d)
       DO_ASK=false
       ;;
     x)
       DO_REMOVE=false
       ;;
     n)
       DO_DRY_RUN=true
       ;;
     r)
       DO_CLONE_READONLY=true
       echo DO_CLONE_READONLY=true
       ;;
     p)
       DO_PARANOID_CHECK=true
  esac
done

if [ -z $DO_ASK ]
then
  usage
  ask
fi

######### START OF AUTOGENERATED OUTPUT #########


