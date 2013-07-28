/*
 *  config.c -  Fan control daemon for MacBook
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

//-----------------------------------------------------------------------------

static FILE *fp;

float temp_avg_floor = 40;		// default values if no config file is found
float temp_avg_ceiling = 50;

float temp_max_ceiling = 255;
float temp_max_floor = 254;
int temp_max_fan_min = 4000;

float temp_TC0P_floor = 40;
float temp_TC0P_ceiling = 50;

float temp_TG0P_floor = 40;
float temp_TG0P_ceiling = 50;

float fan_min = 0;
float fan_max = 6200;			// fixed max value

int log_level = 0;

int exclude[MAX_EXCLUDE];		// array of sensors to exclude

//-----------------------------------------------------------------------------

int match(char* name, char* buf)
{
	char* start = buf;
	char* end = buf;

	if(strlen(buf) < 1)
	{
		return 0;
	}

	// skip preceeding ws
	while(*start && isblank(*start))
	{
		++start;
	}

	// skip to end
	while(*end)
	{
		++end;
	}

	// delete ws backwards from end
	while(end > start && isblank(*end))
	{
		*end = 0;
		--end;
	}

	// compare strings

	return strcmp(name, start) == 0;
}

//-----------------------------------------------------------------------------

int get_val(char *buf)
{
	while(isblank(*buf))
	{
		++buf;
	}

	if(! *buf)
	{
		return -1;
	}

	return atoi(buf);
}

//-----------------------------------------------------------------------------
// format is: name : integer

int read_param(char* name, int min_val, int max_val, int def)
{
	fseek(fp, 0, SEEK_SET);

	while(1)
	{
		char buf[64];
		char *s = fgets(buf, sizeof(buf), fp);

		if(s == NULL)
		{
			break;						// exit when no more to read
		}

		if(buf[0] == '#' || buf[0] == '\n')
		{
			continue;					// skip comments
		}

		char *colon = strchr(buf, ':');	// find colon
		if(colon == NULL)
		{
			printf("Ill formed line in config file: %s\n", buf);
			continue;
		}

		*colon = 0;						// terminate string at colon

		if(match(name, buf))
		{
			int val = get_val(colon + 1);	// get value

			if(val < 0)
			{
				printf("Ill formed line in config file: %s\n", buf);
				continue;
			}
			else
			{
				val = min(max_val, val);// clamp
				val = max(min_val, val);
				return val;				// success
			}
		}
	}

	return def;
}

//-----------------------------------------------------------------------------
// format is: exclude : integer {integer}

void read_exclude_list()
{
	fseek(fp, 0, SEEK_SET);

	while(1)
	{
		char buf[256];
		char *s = fgets(buf, sizeof(buf), fp);

		if(s == NULL)
		{
			break;						// exit when no more to read
		}

		if(buf[0] == '#' || buf[0] == '\n')
		{
			continue;					// skip comments
		}

		char *colon = strchr(buf, ':');	// find colon
		if(colon == NULL)
		{
			printf("Ill formed line in config file: %s\n", buf);
			continue;
		}

		*colon = 0;						// terminate string at colon

		if(match("exclude", buf))
		{
			int i;
			char* values = colon + 1;
		
			// get values

			for(i = 0; i < MAX_EXCLUDE; ++i)
			{
				while(isspace(*values) || *values == ',')
				{
					++values;
				}
				
				if(isdigit(*values))
				{
					int val = get_val(values);
					
					if(val < 0)
					{
						printf("Ill formed line in config file: %s\n", buf);
						return;
					}
					
					exclude[i] = val;
					
					while(isdigit(*values))
					{
						++values;
					}
				}
				else if(*values == 0)
				{
					return;		// done
				}
				else
				{
					printf("Malformed line in config file: %s\n", buf);
					return;
				}
			}
		}
	}
}
 
//-----------------------------------------------------------------------------

void read_cfg(char* name)
{
	memset(exclude, 0, sizeof(exclude));

	fp = fopen(name, "r");

	if(fp != NULL)
	{
		temp_avg_ceiling = read_param("temp_avg_ceiling",	0, 90, 50);
		temp_avg_floor = read_param("temp_avg_floor", 		0, temp_avg_ceiling - 1, 40);

		temp_TC0P_ceiling = read_param("temp_TC0P_ceiling",	0, 90, 65);
		temp_TC0P_floor = read_param("temp_TC0P_floor",		0, temp_TC0P_ceiling - 1, 50);

		temp_TG0P_ceiling = read_param("temp_TG0P_ceiling",	0, 90, 80);
		temp_TG0P_floor = read_param("temp_TG0P_floor",		0, temp_TG0P_ceiling - 1, 65);

		temp_max_ceiling = read_param("temp_max_ceiling", 0, 90, 80);
		temp_max_floor = read_param("temp_max_floor", 0, temp_max_ceiling - 1, 65);

		fan_min = read_param("fan_min", 0, fan_max, 0);
		temp_max_fan_min = read_param("temp_max_fan_min", fan_min, fan_max, 4000);

		log_level = read_param("log_level", 0, 2, 0);
		
		read_exclude_list();

		fclose(fp);
	}
	else
	{
		printf("Could not open config file %s\n", name);
	}

	printf("Using parameters:\n");

	printf("\ttemp_avg_floor: %.0f\n", temp_avg_floor);
	printf("\ttemp_avg_ceiling: %.0f\n", temp_avg_ceiling);

	printf("\ttemp_max_ceiling: %.0f\n", temp_max_ceiling);
	printf("\ttemp_max_floor: %.0f\n", temp_max_floor);
	printf("\ttemp_max_fan_min: %d\n", temp_max_fan_min);

	printf("\ttemp_TC0P_floor: %.0f\n", temp_TC0P_floor);
	printf("\ttemp_TC0P_ceiling: %.0f\n", temp_TC0P_ceiling);

	printf("\ttemp_TG0P_floor: %.0f\n", temp_TG0P_floor);
	printf("\ttemp_TG0P_ceiling: %.0f\n", temp_TG0P_ceiling);

	printf("\tfan_min: %.0f\n", fan_min);

	if(exclude[0] != 0)
	{
		int i;

		printf("\texclude: ");

		for(i = 0; i < MAX_EXCLUDE && exclude[i] != 0; ++i)
		{
			printf("temp%d_input ", exclude[i]);
		}
		printf("\n");
	}
	
	printf("\tlog_level: %d\n", log_level);
}

//-----------------------------------------------------------------------------
