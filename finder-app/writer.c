// File: writer.c
// Author: Ben Tait

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

// 2 arguments should be passed - file path and string to write
#define MIN_ARGS 2

int main(int argc, char *argv[])
{
	// Start syslog logging
	openlog(NULL, LOG_PID | LOG_CONS | LOG_PERROR, LOG_USER);

	// Ensure correct args passed
	if(argc < MIN_ARGS) {
		syslog(LOG_ERR, "2 args were not provided! Need a file path and string to write.");
		closelog();
		return 1;
	}

	// Open user-specified file
	FILE *file = fopen(argv[1], "w");

	// Ensure could get file
	if(NULL == file) {
		syslog(LOG_ERR, "Could not fopen() file specified!");
		closelog();
		return 1;
	}

	// Write string to file and syslog it
	syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
	fprintf(file, "%s", argv[2]);

	// Close file and syslog
	fclose(file);
	closelog();

	return 0;
}