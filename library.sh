#!/bin/sh


# colors https://stackoverflow.com/questions/5947742/how-to-change-the-output-color-of-echo-in-linux
GREEN='\033[1;32m'
RED='\033[1;31m'
ORANGE='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

err()
{
	msg=$@
	echo "${RED}ERROR${NC}: $msg"
}

fatal()
{
	msg=$@
	echo "${RED}FATAL${NC}: $msg"
	exit 1
}

info()
{
	msg=$@
	echo "${BLUE}INFO${NC}: $msg"
}

warn()
{
	msg=$@
	echo "${ORANGE}WARNING${NC}: $msg"
}

debug() 
{
	msg=$@
	echo "${CYAN}DEBUG${NC}: $msg"
}

ok()
{
	echo "${GREEN}[OK]${NC}"
}

progress()
{
	echo -n "$@"
}

ok_echo()
{
	echo "${GREEN}$@${NC}"
}

run_command()
{
	msg=$1
	shift
	cmd="${@}" # remainning args
	progress "$msg ...\t"
	result=$($cmd 2>&1) || { echo; fatal $result; }
	ok
}

run()
{
	cmd="${@}"
	result=$($cmd 2>&1) || { echo; fatal $result; }
	ok
}


get_run()
{
	cmd="${@}"
	result=$($cmd 2>&1) || { echo; fatal $result; }
	echo $result
}

get_dir_of_file()
{
	get_run "echo $(cd $(dirname $1) && pwd -P)"
}

get_full_path_of_file()
{
	get_run "echo $(get_dir_of_file "$1")/$(get_fn_from_file "$1")"
}

get_fn_from_file()
{
	get_run "echo $(basename $1)"
}

get_pid()
{
	echo "$$"
}

folder_exists()
{
	if [ ! -d $1 ]; then
    	false
	else
		true
	fi
}

regular_file_exists()
{
	if [ ! -f $1 ]; then
    	false
	else
		true
	fi
}

file_exists()
{
	if [ ! -e $1 ]; then
    	false
	else
		true
	fi
}