/*
 *  control.c -  Fan control daemon for MacBook
 *
 *  Copyright(C) 2010  Mikael Strom <mikael@sesamiq.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <assert.h>
#include <unistd.h>
#include "config.h"

//------------------------------------------------------------------------------

#define HWMON_DIR		"/sys/class/hwmon"
#define APPLESMC_ID		"applesmc"

struct
{
	char *key;
	char *desc;
}

sensor_desc[] =
{
	{"TA0P", "Ambient Air Temp"},
	{"TB0T", "Battery TS_MAX Temp"},
	{"TB1T", "Battery TS1 Temp"},
	{"TB2T", "Battery TS2 Temp"},
	{"TB3T", "Battery Temp"},
	{"TC0D", "CPU 0 Die Temp"},
	{"TC0P", "CPU 0 Proximity Temp"},
	{"TG0D", "GPU Die - Digital"},
	{"TG0P", "GPU 0 Proximity Temp"},
	{"TG0T", "GPU 0 Die - Analog Temp"},
	{"TG0H", "Left Heat Pipe/Fin Stack Proximity Temp"},
	{"TG1H", "Left Heat Pipe/Fin Stack Proximity Temp"},
	{"TN0P", "MCP Proximity"},
	{"TN0D", "MCP Die"},
	{"Th0H", "Temperature NB/CPU/GPU HeatPipe 0 Proximity"},
	{"Th1H", "Temperature NB/CPU/GPU HeatPipe 1 Proximity"},
	{"Th2H", "Right Fin Stack Proximity Temp"},
	{"TM0P", "Temperature FBDIMM Riser A incoming air Temp"},
	{"Tm0P", "Battery Charger Proximity Temp"},
	{"Ts0P", "Left Palm Rest Temp"},
	{"Ts1P", "Right Palm Rest Temp"}
};
#define N_DESC			(sizeof(sensor_desc) / sizeof(sensor_desc[0]))

#define SENSKEY_MAXLEN	16

struct sensor
{
	int id;
	int excluded;
	char name[SENSKEY_MAXLEN];
	char fname[PATH_MAX];
	float value;
};

//------------------------------------------------------------------------------

char base_path[PATH_MAX];
char fan1_min[PATH_MAX];
char fan2_min[PATH_MAX];
char fan1_man[PATH_MAX];
char fan2_man[PATH_MAX];

int sensor_count = 0;
int fan_count = 0;
float temp_avg = 0;
int fan_speed;
float temp_max = 0;

struct sensor *sensors = NULL;
struct sensor *sensor_TC0P = NULL;
struct sensor *sensor_TG0P = NULL;

#define CTL_NONE	0	// sensor control fan flags
#define CTL_AVG		1
#define CTL_TC0P	2
#define CTL_TG0P	3
#define CTL_MAX		4

int fan_ctl = 0;		// which sensor controls fan

//------------------------------------------------------------------------------

void find_applesmc()
{
	DIR *fd_dir;
	int ret;

	base_path[0] = 0;

	// find and verify applesmc path in /sys/devices

	fd_dir = opendir(HWMON_DIR);

	if(fd_dir != NULL)
	{
		struct dirent *dir_entry;

		while((dir_entry = readdir(fd_dir)) != NULL && base_path[0] == 0)
		{
			if(dir_entry->d_name[0] != '.')
			{
				char name_path[PATH_MAX];
				int fd_name;

				sprintf(name_path, "%s/%s/device/name", HWMON_DIR, dir_entry->d_name);

				fd_name = open(name_path, O_RDONLY);

				if(fd_name > -1)
				{
					char name[sizeof(APPLESMC_ID)];

					ret = read(fd_name, name, sizeof(APPLESMC_ID) - 1);

					close(fd_name);

					if(ret == sizeof(APPLESMC_ID) - 1)
					{
						if(strncmp(name, APPLESMC_ID, sizeof(APPLESMC_ID) - 1) == 0)
						{
							char *dev_path;
							char *last_slash = strrchr(name_path, '/');

							if(last_slash != NULL)
							{
								*last_slash = 0;

								dev_path = realpath(name_path, NULL);

								if(dev_path != NULL)
								{
									strncpy(base_path, dev_path, sizeof(base_path) - 1);
									base_path[sizeof(base_path) - 1] = 0;
									free(dev_path);
								}
							}
						}
					}
				}
			}
		}
		closedir(fd_dir);
	}

	// create paths to fan and sensor

	if(base_path[0] == 0)
	{
		printf("Error: Can't find a applesmc device\n");
		exit(-1);
	}

	sprintf(fan1_min, "%s/fan1_min", base_path);
	sprintf(fan2_min, "%s/fan2_min", base_path);
	sprintf(fan1_man, "%s/fan1_manual", base_path);
	sprintf(fan2_man, "%s/fan2_manual", base_path);

	printf("Found applesmc at %s\n", base_path);
}

//------------------------------------------------------------------------------

void read_sensors()
{
	int i;
	for(i = 0; i < sensor_count; ++i)
	{
		if(! sensors[i].excluded)
		{
			// read temp value

			int fd = open(sensors[i].fname, O_RDONLY);
			if(fd < 0)
			{
				printf("Error: Can't open %s\n", sensors[i].fname);
				fflush(stdout);
			}
			else
			{
				char val_buf[16];
				int n = read(fd, val_buf, sizeof(val_buf));
				if(n < 1)
				{
					printf("Error: Can't read  %s\n", sensors[i].fname);
				}
				else
				{
					sensors[i].value = (float)atoi(val_buf) / 1000.0;
				}
				close(fd);
			}
		}
	}

	// calc average and max

	temp_max = 0.0;
	temp_avg = 0.0;
	int active_sensors = 0;

	for(i = 0; i < sensor_count; ++i)
	{
		if(! sensors[i].excluded)
		{
			temp_avg += sensors[i].value;
			if(sensors[i].value > temp_max)
				temp_max = sensors[i].value;
			++active_sensors;
		}
	}

	temp_avg = temp_avg / active_sensors;
}

//------------------------------------------------------------------------------

void calc_fan()
{
	fan_speed = fan_min;
	fan_ctl = CTL_NONE;

	// calc fan speed on average

	float fan_window = fan_max - fan_min;
	float temp_avg_window = temp_avg_ceiling - temp_avg_floor;
	float normalized_temp =(temp_avg - temp_avg_floor) / temp_avg_window;
	float fan_avg_speed =fan_min + (normalized_temp * fan_window);
	if(fan_avg_speed > fan_speed)
	{
		fan_speed = fan_avg_speed;
		fan_ctl = CTL_AVG;
	}

	// calc fan speed for max

	float temp_max_fan_window = fan_max - temp_max_fan_min;
	float temp_max_window = temp_max_ceiling - temp_max_floor;
	normalized_temp =(temp_max - temp_max_floor) / temp_max_window;
	float fan_max_speed =temp_max_fan_min + (normalized_temp * temp_max_fan_window);
	if(fan_max_speed > fan_speed)
	{
		fan_speed = fan_avg_speed;
		fan_ctl = CTL_MAX;
	}

	// calc fan speed for TC0P

	if(sensor_TC0P != NULL)
	{
		float temp_window = temp_TC0P_ceiling - temp_TC0P_floor;
		float normalized_temp =(sensor_TC0P->value - temp_TC0P_floor) / temp_window;
		float fan_TC0P_speed =fan_min + (normalized_temp * fan_window);
		if(fan_TC0P_speed > fan_speed)
		{
			fan_speed = fan_TC0P_speed;
			fan_ctl = CTL_TC0P;
		}
	}

	// calc fan speed for TG0P

	if(sensor_TG0P != NULL)
	{
		float temp_window = temp_TG0P_ceiling - temp_TG0P_floor;
		float normalized_temp =(sensor_TG0P->value - temp_TG0P_floor) / temp_window;
		float fan_TG0P_speed =fan_min + (normalized_temp * fan_window);
		if(fan_TG0P_speed > fan_speed)
		{
			fan_speed = fan_TG0P_speed;
			fan_ctl = CTL_TG0P;
		}
	}

	// finally clamp

	fan_speed = min(fan_max, fan_speed);
}

//------------------------------------------------------------------------------

void set_fan()
{
	char buf[16];

	// update fan 1

	int fd = open(fan1_min, O_WRONLY);
	if(fd < 0)
	{
		printf("Error: Can't open %s\n", fan1_min);
	}
	else
	{
		sprintf(buf, "%d", fan_speed);
		write(fd, buf, strlen(buf));
		close(fd);
	}

	// set fan 1 manual to zero

	fd = open(fan1_man, O_WRONLY);
	if(fd < 0)
	{
		printf("Error: Can't open %s\n", fan1_man);
	}
	else
	{
		strcpy(buf, "0");
		write(fd, buf, strlen(buf));
		close(fd);
	}

	// update fan 2

	if(fan_count > 1)
	{
		fd = open(fan2_min, O_WRONLY);
		if(fd < 0)
		{
			printf("Error: Can't open %s\n", fan2_min);
		}
		else
		{
			sprintf(buf, "%d", fan_speed);
			write(fd, buf, strlen(buf));
			close(fd);
		}

		// set fan 2 manual to zero

		fd = open(fan2_man, O_WRONLY);
		if(fd < 0)
		{
			printf("Error: Can't open %s\n", fan2_man);
		}
		else
		{
			strcpy(buf, "0");
			write(fd, buf, strlen(buf));
			close(fd);
		}
	}

	fflush(stdout);
}

//------------------------------------------------------------------------------

void adjust()
{
	read_sensors();
	calc_fan();
	set_fan();
}

//------------------------------------------------------------------------------

void scan_sensors()
{
	int i;
	int j;
	struct stat buf;
	int result;

	sensor_TC0P = NULL;
	sensor_TG0P = NULL;

	// get number of fans

	result = stat(fan1_min, &buf);
	if(result != 0)
	{
		printf("No fans detected, terminating!\n");
		exit(-1);
	}
	else
	{
		fan_count = 1;
	}

	result = stat(fan2_min, &buf);
	if(result != 0)
	{
		printf("Found 1 fan.\n");
	}
	else
	{
		fan_count = 2;
		printf("Found 2 fans.\n");
	}

	// count number of sensors

	int count = 0;
	while(count < 100)	// more than 100 sensors is an error!
	{
		char fname[512];

		// sensor numbering start at 1
		sprintf(fname, "%s/temp%d_input", base_path, count + 1);
		result = stat(fname, &buf);

		if(result == 0)
		{
			++count;
		}
		else
		{
			break;		// done
		}
	}

	sensor_count = count;

	if(sensor_count > 0)
	{
		// Get sensor id, labels and descriptions, check exclude list

		if(sensors != NULL)
		{
			free(sensors);
		}

		sensors = malloc(sizeof(struct sensor) * sensor_count);
		assert(sensors != NULL);

		printf("Found %d sensors:\n", sensor_count);

		for(i = 0; i < sensor_count; ++i)
		{
			char fname[512];

			// set id, check exclude list and save file name
			sensors[i].id = i + 1;
			sensors[i].excluded = 0;
			sprintf(sensors[i].fname, "%s/temp%d_input", base_path, sensors[i].id);

			for(j = 0; j < MAX_EXCLUDE && exclude[j] != 0; ++j)
			{
				if(exclude[j] == sensors[i].id)
				{
					sensors[i].excluded = 1;
					break;
				}
			}

			// read label
			sprintf(fname, "%s/temp%d_label", base_path, sensors[i].id);

			sensors[i].name[0] = 0; // set zero length

			FILE *fp = fopen(fname, "r");
			if(fp == NULL)
			{
				printf("Error: Can't open %s\n", fname);
			}
			else
			{
				char key_buf[SENSKEY_MAXLEN];
				memset(key_buf, 0, SENSKEY_MAXLEN);

				int n = fread(key_buf, 1, SENSKEY_MAXLEN - 1, fp);
				if(n < 1)
				{
					printf("Error: Can't read  %s\n", fname);
				}
				else
				{
					char *p_endl = strrchr(key_buf, '\n');
					if(p_endl)
					{
						*p_endl = 0; 	// remove '\n'
					}
					strncpy(sensors[i].name, key_buf, SENSKEY_MAXLEN);
				}
				fclose(fp);
			}
		}

		for(i = 0; i < sensor_count; ++i)		// for each label found
		{
			if(! sensors[i].excluded)
			{
				// try to find TC0P and TG0P
				// if found, assign sensor_TC0P and sensor_TG0P for later use

				if(strcmp(sensors[i].name, "TC0P") == 0)
				{
					sensor_TC0P = &sensors[i];
				}
				else if(strcmp(sensors[i].name, "TG0P") == 0)
				{
					sensor_TG0P = &sensors[i];
				}
			}

			// print out sensor information.

			printf("\t%2d: ", sensors[i].id);

			int found = 0;
			for(j = 0; j < N_DESC && ! found; ++j)		// find in descriptions table
			{
				if(strcmp(sensors[i].name, sensor_desc[j].key) == 0)
				{
					found = 1;
					printf("%s - %s", sensor_desc[j].key, sensor_desc[j].desc);
				}
			}
			if(! found)
			{
				printf("%s - ?", sensors[i].name);
			}

			printf(" %s\n", sensors[i].excluded ? "   ***EXCLUDED***" : "");
		}
	}
	else
	{
		printf("No sensors detected, terminating!\n");
		exit(-1);
	}

	fflush(stdout);
}

//------------------------------------------------------------------------------

void logger()
{
	int i;

	if(log_level > 0)
	{
		printf("Speed: %d, %sAVG: %.1fC, %sMAX: %.1fC" ,
					 fan_speed,
					 fan_ctl == CTL_AVG ? "*" : " ",
					 temp_avg,
					 fan_ctl == CTL_MAX ? "*" : " ",
					 temp_max);

		if(sensor_TC0P != NULL)
		{
			printf(", %sTC0P: %.1fC" ,
				   fan_ctl == CTL_TC0P ? "*" : " ",
				   sensor_TC0P->value);
		}

		if(sensor_TG0P != NULL)
		{
			printf(", %sTG0P: %.1fC" ,
				   fan_ctl == CTL_TG0P ? "*" : " ",
				   sensor_TG0P->value);
		}

		if(log_level > 1)
		{
			printf(", Sensors: ");
			for(i = 0; i < sensor_count; ++i)
			{
				if(! sensors[i].excluded)
				{
					printf("%s:%.0f ", sensors[i].name, sensors[i].value);
				}
			}
		}

		printf("\n");
		fflush(stdout);
	}
}

//------------------------------------------------------------------------------

