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
#include <mpd/client.h>
#include <mpd/song.h>
#include <mpd/connection.h>
#include <mpd/status.h>

#include <X11/Xlib.h>

// refresh rate in seconds
#define REFRESH_RATE 20
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

// from https://github.com/bbenne10/dwmstatus/blob/master/dwmstatus.c
char *
getMpd() {
  struct mpd_song * song = NULL;
  const char * title = NULL;
  const char * artist = NULL;
  char * retstr = NULL;

  struct mpd_connection * conn ;
  if (!(conn = mpd_connection_new("localhost", 0, 30000)) || mpd_connection_get_error(conn)){
    fprintf(stderr, "Could not connect to MPD");
    return smprintf("");
  }

  mpd_command_list_begin(conn, true);
  mpd_send_status(conn);
  mpd_send_current_song(conn);
  mpd_command_list_end(conn);

  struct mpd_status* theStatus = mpd_recv_status(conn);

  if ((theStatus) && (mpd_status_get_state(theStatus) == MPD_STATE_PLAY)) {
    mpd_response_next(conn);
    song = mpd_recv_song(conn);
    title = smprintf("%s",mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
    artist = smprintf("%s",mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));

    mpd_song_free(song);
    retstr = smprintf("<span color='#b8bb26'>&#xf025;</span> %s - %s  ", artist, title);
    free((char*)title);
    free((char*)artist);
  } else {
    retstr = smprintf("");
  }

  mpd_status_free(theStatus);
  mpd_response_finish(conn);
  mpd_connection_free(conn);
  return retstr;
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

	return smprintf("%.0f%%%c ||", ((float)remcap / (float)descap) * 100, status);
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
	char *status,
	     *avgs,
	     *time_str,
	     *t0, *t1, *t2,
	     *freespace_root, *freespace_home,
	     *freespace_str, *temperature_str,
         *mpd_status;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	atexit(cleanup);

	for (;;sleep(REFRESH_RATE)) {

        /* MPD */
        mpd_status = getMpd();

        /* TIME */
        time_str = mktimes("%a %d %b %H:%M", tzitaly);

        /* FREE SPACE */
        freespace_root = get_freespace("/", "root");
        freespace_home = get_freespace("/home", "home");

        freespace_str = smprintf(
                "%s | %s",
                freespace_root, 
                freespace_home
                );
        free(freespace_root);
        free(freespace_home);
        free(freespace_exfat);

        /* TEMPERATURES */
        t0 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp1_input");
        t1 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp2_input");
        t2 = gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp3_input");

        temperature_str = smprintf("Temps:%s|%s|%s", t0, t1, t2);

        free(t0);
        free(t1);
        free(t2);


        /* SYSTEM LOAD */
        avgs = loadavg();

        status = smprintf(
                "%s %s || %s || Load: %s || %s",
                mpd_status,
                freespace_str, 
                temperature_str, 
                avgs, 
                time_str
                );

        setstatus(status);

        free(status);
        free(mpd_status);
        free(avgs);
        free(time_str);
        free(freespace_str);
        free(temperature_str);

	}

	XCloseDisplay(dpy);

	return 0;

}
