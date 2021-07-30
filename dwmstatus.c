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

#define MAXLEN 1024

char *tzitaly = "Europe/Rome";

static Display *dpy = NULL;

/* BUFFERS FOR STATUS STRINGS */ 
char status[MAXLEN*16],
     load_str[MAXLEN],
     time_str[MAXLEN],
     t0[MAXLEN],
     t1[MAXLEN],
     t2[MAXLEN],
     freespace_root[MAXLEN],
     freespace_home[MAXLEN],
     freespace_exfat[MAXLEN],
     freespace_str[MAXLEN*4],
     temperature_str[MAXLEN*4],
     mpd_status[MAXLEN*3],
     ram_str[MAXLEN],
     battery_pct[MAXLEN];

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
void
getMpd() {
  struct mpd_song * song = NULL;
  char title[MAXLEN], artist[MAXLEN];

  struct mpd_connection * conn ;
  if (!(conn = mpd_connection_new("localhost", 0, 30000)) || mpd_connection_get_error(conn)){
    fprintf(stderr, "Could not connect to MPD\n");
    snprintf(mpd_status, MAXLEN, "");
  }

  mpd_command_list_begin(conn, true);
  mpd_send_status(conn);
  mpd_send_current_song(conn);
  mpd_command_list_end(conn);

  struct mpd_status* theStatus = mpd_recv_status(conn);


  if ((theStatus) && (mpd_status_get_state(theStatus) == MPD_STATE_PLAY)) {
    mpd_response_next(conn);
    song = mpd_recv_song(conn);
    snprintf(title, MAXLEN, "%s",mpd_song_get_tag(song, MPD_TAG_TITLE, 0));
    snprintf(artist, MAXLEN, "%s",mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));

    mpd_song_free(song);
    snprintf(mpd_status, sizeof(mpd_status)-1, "<span color='#b8bb26'>&#xf025;</span> %s - %s  ", artist, title);
  } else {
    snprintf(mpd_status, MAXLEN, "");
  }

  mpd_status_free(theStatus);
  mpd_response_finish(conn);
  mpd_connection_free(conn);
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

void
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		snprintf(time_str, MAXLEN, "");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		snprintf(time_str, MAXLEN, "");
	}

	snprintf(time_str, MAXLEN, "%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

void
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0){
		snprintf(load_str, MAXLEN, "");
    }

	// return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
	snprintf(load_str, MAXLEN, "%.2f", avgs[0]);
}

int
readfile(char *base, char *file, char retbuf[])
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);

	fd = fopen(path, "r");
	free(path);

	if (fd == NULL){
		snprintf(retbuf, MAXLEN, "can't open %s/%s", base,file);
        return -1;
    }

	if (fgets(line, sizeof(line)-1, fd) == NULL){ 
		snprintf(retbuf, MAXLEN, "empty file %s/%s", base,file);
        return -1;
    }
	fclose(fd);

	snprintf(retbuf, MAXLEN, "%s", line);
    return 0;
}

int
readcommand(char *command, char retbuf[])
{
	char line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	fd = popen(command, "r");
	if (fd == NULL) {
		snprintf(retbuf, MAXLEN, "can't run %s", command);
        return -1;
    }

	if (fgets(line, sizeof(line)-1, fd) == NULL) { 
		snprintf(retbuf, MAXLEN, "can't run %s", command);
        return -1;
    }

	fclose(fd);

	snprintf(retbuf, MAXLEN, "%s", line);
    return 0;
}

void
get_battery() {
    readcommand("~/.local/bin/show_battery_pct", battery_pct);
}

void
get_freeram() {
    readcommand("sh ~/scripts/free_ram.sh", ram_str);
}


void
gettemperature(char *base, char *sensor, char retbuf[])
{
	char co[MAXLEN];

	int err = readfile(base, sensor, co);
	if (err) {
        char errmsg[] =  "gettemperature: readfile() failed\n";
		snprintf(retbuf, MAXLEN,errmsg);
		fputs(errmsg, stderr);
    }
	snprintf(retbuf, MAXLEN, "%02.0f°C", atof(co) / 1000);
}


void
cleanup()
{
	if (dpy != NULL) {
		XCloseDisplay(dpy);
	}
}

int
get_freespace(char *mntpt, char *briefname, char retbuf[]){
    struct statvfs data;
    double total, used = 0;

    if (briefname == NULL) {
        briefname = mntpt;
    }

    if ( (statvfs(mntpt, &data)) < 0){
		fprintf(stderr, "can't get info on disk %s.\n", mntpt);
        return -1;
    }
    total = (data.f_blocks * data.f_frsize);
    used = (data.f_blocks - data.f_bfree) * data.f_frsize ;

    float freespace = total - used;

    snprintf(retbuf, MAXLEN, "%s %.0f GiB (%.0f%%)", briefname, freespace / GiB,  (used/total*100));
    return 0;
}

int
main(void)
{

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	atexit(cleanup);

	for (;;sleep(REFRESH_RATE)) {

        /* MPD */
        getMpd();

        /* TIME */
        mktimes("%a %d %b %H:%M", tzitaly);

        /* FREE SPACE */
        get_freespace("/", "root", freespace_root);
        get_freespace("/home", "home", freespace_home);
        get_freespace("/mnt/hdd/exfat", "exfat", freespace_exfat);

        snprintf(
                freespace_str,
                sizeof(freespace_str)-1,
                "%s | %s | %s",
                freespace_root, 
                freespace_home,
                freespace_exfat
        );

        /* TEMPERATURES */
        gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp1_input", t0);
        gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp2_input", t1);
        gettemperature("/sys/devices/platform/coretemp.0/hwmon/hwmon1", "temp3_input", t2);

        snprintf(temperature_str, sizeof(temperature_str), "Temps:%s|%s|%s", t0, t1, t2);

        /* SYSTEM LOAD */
        loadavg();

        /* BATTERY */
        get_battery();

        /* RAM */
        get_freeram();

        snprintf(status, sizeof(status)-1, 
                "Battery: %s || %s %s || %s || Load: %s || RAM: %s || %s",
                battery_pct,
                mpd_status,
                freespace_str, 
                temperature_str, 
                load_str, 
                ram_str, 
                time_str
                );

        setstatus(status);

	}

	XCloseDisplay(dpy);

	return 0;

}
