/*
 * Copy me if you can.
 * by 20h
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

#include <X11/Xlib.h>

// refresh rate in seconds
#define REFRESH_RATE 1
#define GiB (1<<30)

char *tzitaly = "Europe/Rome";

static Display *dpy = NULL;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	// return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
	return smprintf("%.2f", avgs[0]);
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *
getbattery(char *base)
{
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
	} else {
		status = '?';
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

char *
gettemperature(char *base, char *sensor)
{
	char *co, *ret;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	ret = smprintf("%02.0f°C", atof(co) / 1000);
	free(co);
	return ret;
}


void
cleanup()
{
	if (dpy != NULL) {
		XCloseDisplay(dpy);
	}
}


char *get_freespace(char *mntpt, char *briefname){
    struct statvfs data;
    double total, used = 0;

    if (briefname == NULL) {
        briefname = mntpt;
    }

    if ( (statvfs(mntpt, &data)) < 0){
		fprintf(stderr, "can't get info on disk.\n");
		return("?");
    }
    total = (data.f_blocks * data.f_frsize);
    used = (data.f_blocks - data.f_bfree) * data.f_frsize ;

    float freespace = total - used;

    return(smprintf("%s %.0f GiB (%.0f%%)", briefname, freespace / GiB,  (used/total*100)));
}

int
main(void)
{
	char *status;
	char *avgs;
	char *time_str;
	char *t0, *t1, *t2;
	char *freespace_root, *freespace_home, *freespace_exfat;
	char *freespace_str, *temperature_str;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	atexit(cleanup);

	for (;;sleep(REFRESH_RATE)) {
		avgs = loadavg();
		time_str = mktimes("%W %a %d %b %H:%M %Y", tzitaly);
		t0 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp1_input");
		t1 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp2_input");
		t2 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp3_input");

        freespace_root = get_freespace("/", "Root");
        freespace_home = get_freespace("/home", "Home");
        freespace_exfat = get_freespace("/mnt/hdd/exfat", "Exfat");

		freespace_str = smprintf(
                "Free space: %s | %s | %s",
				freespace_root, 
                freespace_home,
                freespace_exfat
        );

        temperature_str = smprintf("Temps:%s|%s|%s", t0, t1, t2);

		status = smprintf(
                "%s || %s || Load:%s || %s",
				freespace_str, 
                temperature_str, 
                avgs, 
                time_str
        );

		setstatus(status);

		free(t0);
		free(t1);
		free(t2);
		free(avgs);
		free(time_str);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

