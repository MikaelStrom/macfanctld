/*
 *  macfanctl.c -  Fan control daemon for MacBook
 *
 *  Copyright (C) 2010  Mikael Strom <mikael@sesamiq.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  Note:
 *  This was written for MacBook Pro 5,1 and Ubutnu 10.04
 *  Requires applesmc-dkms
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>

#include "control.h"
#include "config.h"

//------------------------------------------------------------------------------

#define PID_FILE	"/var/run/macfanctld.pid"
#define LOG_FILE	"/var/log/macfanctl.log"
#define CFG_FILE	"/etc/macfanctl.conf"

int running = 1;
int lock_fd = -1;
int reload = 0;

//------------------------------------------------------------------------------

void signal_handler(int sig)
{
	switch (sig)
	{
	case SIGHUP:
		reload = 1;
		break;
	case SIGINT:
	case SIGTERM:
		running = 0;
		break;
	}
}

//-----------------------------------------------------------------------------

void daemonize()
{
	if (getppid() == 1)
		return; // already a daemon

	// fork of new process
	pid_t pid = fork();

	if(pid < 0)
		exit(1); 		// fork error

	if(pid > 0)
		exit(0);		// parent exits

	// child (daemon) continues

#ifdef DEBUG
	sleep(20);			// time to attach debugger to this process
#endif

	setsid(); 			// create a new session

#ifdef DEBUG
	umask(0);
#else
	umask(022); // set createfile permissions
#endif

	freopen(LOG_FILE, "w", stdout);
	freopen("/dev/null", "r", stdin);

	chdir("/");

	// create lockfile
	int lock_fd = open(PID_FILE, O_RDWR | O_CREAT, 0640);

	if(lock_fd < 0)
		exit(1); 		// open failed, we're a duplicate

	if(lockf(lock_fd, F_TLOCK, 0) < 0)
		exit(0); 		// lock failed - no idea what this means...

	// first instance continues...
	// write pid to file, and leave file open (blocking duplicates)

	char str[32];
	sprintf(str, "%d\n", getpid());
	write(lock_fd, str, strlen(str));
}

//-----------------------------------------------------------------------------

void usage()
{
	printf("usage: macfanctld [-f]\n");
	printf("  -f  run in foregound\n");
	exit(-1);
}

//-----------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	int i;
	int daemon = 1;

	// setup daemon
	signal(SIGCHLD, SIG_IGN); 			// ignore child
	signal(SIGTSTP, SIG_IGN); 			// ignore tty signals
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGINT, signal_handler); 	// catch Ctrl-C signal (terminating in foreground mode)
	signal(SIGHUP, signal_handler); 	// catch hangup signal (reload config)
	signal(SIGTERM, signal_handler); 	// catch kill signal

	for(i = 1; i < argc; ++i)
	{
		if(strcmp(argv[i], "-f") == 0)
		{
			daemon = 0;
		}
		else
		{
			usage();
		}
	}

	if(daemon)
	{
		daemonize();
	}
	else
	{
		printf("Running in foreground, log to stdout.\n");
	}

	// main loop

	read_cfg(CFG_FILE);

	find_applesmc();
	scan_sensors();

	running = 1;
	while(running)
	{
		adjust();

		logger();

		if(reload)
		{
			read_cfg(CFG_FILE);
			scan_sensors();
			reload = 0;
		}

		sleep(5);
	}

	// close pid file and delete it

	if(lock_fd != -1)
	{
		close(lock_fd);
		unlink(PID_FILE);
	}

	deallocate_sensors();

	printf("Exiting.\n");

	return 0;
}

//-----------------------------------------------------------------------------

