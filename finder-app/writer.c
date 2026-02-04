#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    const char *writefile;
    const char *writestr;
    FILE *fp;

    openlog("writer", LOG_PID, LOG_USER);

    // Check argument count
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments");
        closelog();
        return 1;
    }

    writefile = argv[1];
    writestr = argv[2];

    // Open file for writing (overwrite if exists)
    fp = fopen(writefile, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Failed to open file %s: %s",
               writefile, strerror(errno));
        closelog();
        return 1;
    }

    // Write string to file
    if (fprintf(fp, "%s", writestr) < 0) {
        syslog(LOG_ERR, "Failed to write to file %s",
               writefile);
        fclose(fp);
        closelog();
        return 1;
    }

    fclose(fp);

    // Log success
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    closelog();
    return 0;
}
