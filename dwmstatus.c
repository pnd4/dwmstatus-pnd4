#define _DEFAULT_SOURCE
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

#include <X11/Xlib.h>

// VARIABLES
static Display *dpy;

// Timezones
char *tzutc = "UTC";
char *tzsocal = "America/Los_Angeles";

// Sensors.. use full paths.
//char *sensor0 = "/sys/class/hwmon/hwmon0/device/temp1_input";
char *sensor0 = "/sys/class/hwmon/hwmon1/temp1_input";
char *sensor1 = "/sys/class/hwmon/hwmon1/temp2_input";

// SUCKLESS' PRINTF()
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

// TIME
//
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

	memset(buf, 0, sizeof(buf));
	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}

// X11 STUFF
//
void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

// LOADAVG
//
char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

// PROCESSOR TEMPERATURES
//
char *
gettemperature(char *sensor)
{
    FILE *fp;
    char str[6];
    
    // basic readfile() functionality
    fp = fopen(sensor, "r");
    fgets(str, 6, fp);
    fclose(fp);
    
    if (str == NULL)
        return smprintf("");
    return smprintf("%02.0f°c", atof(str) / 1000);
}

// NETUSAGE
//
int
parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
    char buf[255];
    char *datastart;
    static int bufsize;
    int rval;
    FILE *devfd;
    unsigned long long int receivedacc, sentacc;

    bufsize = 255;
    devfd = fopen("/proc/net/dev", "r");
    rval = 1;

    // Ignore the first two lines of the file
    fgets(buf, bufsize, devfd);
    fgets(buf, bufsize, devfd);

    while (fgets(buf, bufsize, devfd)) {
        if ((datastart = strstr(buf, "lo:")) == NULL) {
        datastart = strstr(buf, ":");

        // With thanks to the conky project at http://conky.sourceforge.net/
        sscanf(datastart + 1, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
               &receivedacc, &sentacc);
        *receivedabs += receivedacc;
        *sentabs += sentacc;
        rval = 0;
        }
    }

    fclose(devfd);
    return rval;
}

void
calculate_speed(char *speedstr, unsigned long long int newval, unsigned long long int oldval)
{
    double speed;
    speed = (newval - oldval) / 1024.0;
    if (speed > 1024.0) {
        speed /= 1024.0;
        sprintf(speedstr, "%.2f M", speed);
    } else {
        sprintf(speedstr, "%.2f K", speed);
    }
}

char *
get_netusage(unsigned long long int *rec, unsigned long long int *sent)
{
    unsigned long long int newrec, newsent;
    newrec = newsent = 0;
    char downspeedstr[15], upspeedstr[15];
    static char retstr[42];
    int retval;

    retval = parse_netdev(&newrec, &newsent);
    if (retval) {
        fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
        exit(1);
    }

    calculate_speed(downspeedstr, newrec, *rec);
    calculate_speed(upspeedstr, newsent, *sent);

    sprintf(retstr, "d[%s] u[%s]", downspeedstr, upspeedstr);

    *rec = newrec;
    *sent = newsent;
    return retstr;
}

// VOLUME
//
char*
runcmd(char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (fp == NULL) return NULL;
    char ln[30];
    fgets(ln, sizeof(ln)-1, fp);
    pclose(fp);
    ln[strlen(ln)-1]='\0';
    return smprintf("%s", ln);
}

int
getvolume() {
    int volume;
        sscanf(runcmd("amixer get Master | awk -F'[]%[]' '/%/ { print $2 }'"), "%i%%", &volume);
    return volume;
}

int runevery(time_t *ltime, int sec){
    /* return 1 if `sec` elapsed since last run
     * else return 0 
    */
    time_t now = time(NULL);
    if ( difftime(now, *ltime ) >= sec)
    {
        *ltime = now;
        return(1);
    }
    else 
        return(0);
}

// MAIN: PUT IT ALL TOGETHER
//
int
main(void)
{
	char *status=NULL;
	char *avgs=NULL;
	char *tmutc=NULL;
	char *tmsocal=NULL;
    char *temp0=NULL;
    char *temp1=NULL;
    int vol;
    char *netstats=NULL;
    static unsigned long long int rec, sent;
    time_t count60 = 0;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

    parse_netdev(&rec, &sent);
	for (;;sleep(1)) {
        // Update these every minute
        if (runevery(&count60, 60)) {
                free(tmutc);
                free(tmsocal);
		        tmutc = mktimes("%H:%M", tzutc);
		        tmsocal = mktimes("%a %b %d %H:%M", tzsocal);
        }
        // Update the rest every second
		avgs = loadavg();
        temp0 = gettemperature(sensor0);
        temp1 = gettemperature(sensor1);
        vol = getvolume();
        netstats = get_netusage(&rec, &sent);

		//status = smprintf("\x04\u00B3\x01 %s \x04\u00B1\x01 %s \x04\u00A4\x01 %s \x04\u00B6\x01 %d \x04UTC\x01 %s \x04\u00B7\x01 %s \x04\u00A1 \u00A2 \u00A3 \u00A4 \u00A5 \u00A6 \u00A7 \u00A8 \u00A9 \u00B0 \u00B1 \u00B2 \u00B3 \u00B4 \u00B5 \u00B6 \u00B7 \u00B8 \u00B9 \u00C1 \x01",
		status = smprintf("\x04\u00B3\x01 %s \x04\u00B1\x01 %s %s \x04\u00A4\x01 %s \x04\u00B6\x01 %d \x04UTC\x01 %s \x04\u00B7\x01 %s ",
				avgs, temp0, temp1, netstats, vol, tmutc, tmsocal);
		setstatus(status);
		free(avgs);
        free(temp0);
        free(temp1);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

