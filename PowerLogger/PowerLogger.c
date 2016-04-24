//  main.c
//  PowerLogger
//
//  Created by Dirk Theisen on 27.03.16.
//  Copyright © 2016 Dirk Theisen. All rights reserved.

/*
 Simple solarmax logger c program written by zagibu@gmx.ch in July 2010
 This program was originally licensed under WTFPL 2 http://sam.zoy.org/wtfpl/
 It is now licensed under GPLv2 or later http://www.gnu.org/licenses/gpl2.html
 
 You need the mysql client library files installed to be able to compile it.
 
 Compile with:
 gcc -W -Wall -Wextra -Wshadow -Wlong-long -Wformat -Wpointer-arith -rdynamic -pedantic-errors -std=c99 -o sollog sollog.c -lmysqlclient
 
 Run with:
 ./sollog /home/fritz/Desktop/loggertest/sollog.conf
 ./sollog /usr/bin/sollog.conf
 Structure of the config-file:
 
 Debug=0
 Loginterval=60
 Waitinterval=200
 DBhost=localhost
 DBname=solarmax
 DBtabprefix=log
 DBuser=solaruser
 DBpass=userpassword
 Hostname=192.168.178.35
 Hostport=12345
 NumberOfInverters=1
 
 You can set DEBUG to 1 to get detailed output in a separate logfile.
 
 It is recommended to schedule the smw-logger to be started between 5:00 - 6:00 in the
 morning and stopped between 22:00 and 23:00 in the evening (compare with sunshine
 duration). The smw-logger has no built-in facility for logging, so use CRON or similar.
 
 Example CRON entries:
 00 05 * * * /usr/local/bin/logger /usr/local/etc/logger.conf
 00 23 * * * killall logger
 
 Sources:
 - http://www.linuxhowtos.org/C_C++/socket.htm
 - http://wwwuser.gwdg.de/~kboehm/ebook/21_kap15_w6.html#49329
 - http://man.cx/setbuf%283%29
 - http://allfaq.org/forums/t/169895.aspx
 - http://dev.mysql.com/tech-resources/articles/mysql-capi-tutorial.html
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
//#include <mysql/mysql.h>
#include <regex.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <assert.h>
//# define TERM_DEVICE "/dev/ttyUSB0"    /* = COM1 / USB */
# define TERM_DEVICE "/dev/tty.usbserial-A50285BI"    /* = COM1 / USB */
# define TERM_SPEED B19200        /* Bit/Sek */

int i, fd, integer, integerout,integerout2,j, PACcount;
unsigned long old_flags;
time_t log_interval;
int integerout1[8];
ssize_t length;
int Debug = 1;

struct termios term_attr;
FILE* file;
char* file_name;
//char outbuffer[512]={0};
//  char   outbuffer[512] = "KDY=B;KMT=40;KYR=B9F;KT0=1154;TNF=1388;TKK=19;PAC=5C;PRL=1;IL1=54;IDC=1A;UL1=910;UDC=A2C;SYS=FFFF|";
//char text[256];
char inbuffer[1];
char* mode = "a";
char* message = "{FB;01;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|1199}";
//  time_t timestamp;
//time_t t;
//struct tm *ts;


FILE* error_file = NULL;
//char* error_file_name = "/var/log/solarmax-error.log";
char* error_file_name = "/tmp/solarmax-error.log";
char* error_mode = "w";
FILE* debug_file = NULL;
//char* debug_file_name = "/var/log/solarmax-debug.log";
char* debug_file_name = "/tmp/solarmax-debug.log";
char* debug_mode = "w";
FILE* config_file = NULL;
char* config_file_name;
char* config_mode = "r";
int sockfd, portno, n, result, counter, wait_interval, active_max, nr_of_maxes;
int lost_connection = 0;
//struct sockaddr_in serv_addr;
//struct hostent* server;
//char dbhost[512];
//char dbname[512];
//char dbtabprefix[512];
//char dbuser[512];
//char dbpass[512];
//char hostaddr[512];
char line[512];
char* expression = "...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*);...=([0-9A-F]*)";
long kdy, kmt, kyr, kt0, tnf, tkk, pac, prl, il1, idc, ul1, udc, sys;
char buffer2[512];
// char    buffer [512] = {"KDY=B;KMT=40;KYR=B9F;KT0=1154;TNF=1388;TKK=19;PAC=5C;PRL=1;IL1=54;IDC=1A;UL1=910;UDC=A2C;SYS=FFFF;"};
char buffer[512];
char query[512];
char* temp;
regex_t rx;
regmatch_t* matches;
//MYSQL* connection = NULL;

void error_exit(const char* msg) {
    perror(msg);
    if (error_file != NULL)
        fclose(error_file);
    if (debug_file != NULL)
        fclose(debug_file);
    exit(0);
}

void debug_entry(char* msg) {
    time_t timestamp = time(NULL);
    char debug_msg[512];
    char *time_now = ctime(&timestamp);
    time_now[strlen(time_now)-1]=0;
    if (debug_file == NULL)
        error_exit("ERROR writing to debug.log file");
    sprintf(debug_msg, "%s %s", time_now, msg);
    fprintf(debug_file, "%s\n", debug_msg);
}

void error_retry(char* msg) {
    time_t timestamp = time(NULL);
    char error_msg[512];
    char *time_now = ctime(&timestamp);
    time_now[strlen(time_now)-1]=0;
    if (error_file == NULL)
        error_exit("ERROR writing to error.log file");
    sprintf(error_msg, "%s %s", time_now, msg);
    fprintf(error_file, "%s\n", error_msg);
}

void set_nonblock(int sock) {
    int flags;
    flags = fcntl(sock,F_GETFL,0);
    if (flags == -1)
        error_exit("ERROR no valid flags on socket");
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    // Check commandline arguments
//    if (argc < 2)
//        error_exit("ERROR program needs config-file as parameter");
//    
//    // Try to open error log file
    if ((error_file = fopen(error_file_name, error_mode)) == NULL)
        error_exit("ERROR opening error.log file");
    
    // Make file unbuffered
    setbuf(error_file, NULL);
    
//    //Read Config File
//    config_file_name = argv[1];
//    FILE *fp = fopen(config_file_name, config_mode);
//    
//    // Read variables
//    if (fp) {
//        while (fgets(line, sizeof(line), fp)) {
//            sscanf(line, "Debug=%d[^\n]", &DEBUG);
//            sscanf(line, "Loginterval=%d[^\n]", &log_interval);
//            sscanf(line, "Waitinterval=%d[^\n]", &wait_interval);
//            sscanf(line, "DBhost=%[^\n]", dbhost);
//            sscanf(line, "DBname=%[^\n]", dbname);
//            sscanf(line, "DBtabprefix=%[^\n]", dbtabprefix);
//            sscanf(line, "DBuser=%[^\n]", dbuser);
//            sscanf(line, "DBpass=%[^\n]", dbpass);
//            sscanf(line, "Hostname=%[^\n]", hostaddr);
//            sscanf(line, "Hostport=%d[^\n]", &portno);
//            sscanf(line, "NumberOfInverters=%d[^\n]", &nr_of_maxes);
//        }
//    }
//    fclose(fp);
    
    // Try to open debug log file, if necessary
    if (DEBUG) {
        if((debug_file = fopen(debug_file_name, debug_mode)) == NULL)
            error_exit("ERROR opening debug.log file");
        
        // Make file unbuffered
        setbuf(debug_file, NULL);
    }
    
    // Try to compile regular expression
    result = regcomp(&rx, expression, REG_EXTENDED);
    if (result != 0) {
        regerror(result, &rx, expression, sizeof(expression));
        regfree(&rx);
        sprintf(buffer, "ERROR invalid regular expression: %s", expression);
        error_exit(buffer);
    }
    
    // Try to reserve memory for matches
    matches = (regmatch_t *) malloc((rx.re_nsub + 1) * sizeof(regmatch_t));
    if (!matches)
        error_exit("Out of memory");
    
//    // Connect to database
//    connection = mysql_init(NULL);
//    if (!mysql_real_connect(connection, dbhost, dbuser, dbpass, dbname, 0, NULL, 0))
//        error_exit(mysql_error(connection));
//    
//    if (DEBUG) {
//        sprintf(buffer, "Connected to database %s on host %s", dbname, dbhost);
//        debug_entry(buffer);
//    }
    
    
    if ((fd = open(TERM_DEVICE, O_RDWR)) == -1)
    {
        perror("terminal: Can't open device " TERM_DEVICE);
        return(1);
    }
    /* RS232 konfigurieren */
    
    if (tcgetattr(fd, &term_attr) != 0)
    {
        perror("terminal: tcgetattr() failed");
        return(1);
    }
    term_attr.c_cflag = TERM_SPEED | CS8 | CLOCAL | CREAD;
    term_attr.c_iflag = 0;
    term_attr.c_oflag = OPOST | ONLCR;
    term_attr.c_lflag = 0;
    if (tcsetattr(fd, TCSAFLUSH, &term_attr) != 0)
        perror("terminal: tcsetattr() failed");
    
    /* Std.-Eingabe anpassen */
    
    tcgetattr(STDIN_FILENO, &term_attr) ;
    
    
    /* alte Einst. sichern */
    
    old_flags = term_attr.c_lflag;
    term_attr.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_attr) ;
    
    /* */
    
    
    while (1) {
        
        // set variable to default value or it will keep trying to reconnect
        lost_connection = 0;
        
//        // Check if connection to db-server must be re-established
//        if (mysql_ping(connection)) {
//            
//            // Connect to database
//            if (!mysql_real_connect(connection, dbhost, dbuser, dbpass, dbname, 0, NULL, 0))
//                error_exit(mysql_error(connection));
//            
//            if (DEBUG) {
//                sprintf(buffer, "Connected to database %s on host %s", dbname, dbhost);
//                debug_entry(buffer);
//            }
//        }
        
        // Start sending the data requests and logging the answers
        while (1) {
            /*
             // We have to get out of this while-loop to reestablish the connection to the inverter
             if (lost_connection){
             debug_entry("Looks like we lost our connection to solarmax, reconnecting...");
             break;
             }
             */
            // Get the current time
            time_t start_time = time(NULL);
            
            for(active_max = 1; active_max <= nr_of_maxes; active_max++){
                
                // Generate message according to device address of solarmax:
                
                char message[100];
                char line[100];
                sprintf(message, "FB;0%d;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|", active_max);
                uint16_t checksum = 0;
                char* m = message;
                while (m) checksum += *(m++);
                sprintf(line, "{%s%x", message, checksum);
                
                
                // Could be something like this:
                //sprintf(message, "{FB;0%d;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|%x}", active_max, 16_bit_checksum
                // For further information on the protocol refer to: http://blog.dest-unreach.be/2009/04/15/solarmax-maxtalk-protocol-reverse-engineered
                // Solution: http://www.photovoltaikforum.com/datenlogger-f5/solarmax-protokoll-und-crc-berechnung-t22253.html
                // Until someone comes up with a nice solution to calculate the checksum, lets stick to a few precalculated message strings (tested only for 2 maxes!):
                char* oldMessage;
                
                if (active_max == 1) {
                    oldMessage = "{FB;01;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|1199}";
                }
                else if (active_max == 2) {
                    oldMessage = "{FB;02;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|119A}";
                }
                else if (active_max == 3) {
                    oldMessage = "{FB;03;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|119B}";
                }
                else if (active_max == 4) {
                    oldMessage = "{FB;04;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|119C}";
                }
                else if (active_max == 5) {
                    oldMessage = "{FB;05;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|119D}";
                }
                else if (active_max == 6) {
                    oldMessage = "{FB;06;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|119E}";
                }
                else if (active_max == 7) {
                    oldMessage = "{FB;07;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|119F}";
                }
                else if (active_max == 8) {
                    oldMessage = "{FB;08;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|11A0}";
                }
                else if (active_max == 9) {
                    oldMessage = "{FB;09;46|64:KDY;KMT;KYR;KT0;TNF;TKK;PAC;PRL;IL1;IDC;UL1;UDC;SYS|11A1}";
                }
                else {
                    error_exit("ERROR invalid hardware address; currently works only for 9 maxes");
                }
                
                assert(strcmp(message, oldMessage)==0);
                
                if (DEBUG) {
                    sprintf(buffer, "Sending message: %s", message);
                    debug_entry(buffer);
                }

                
                
                // Send message 1
                length = write(fd,message,strlen(message));
                if (length < 0)
                    break;
                if (DEBUG) {
                    printf("WR= message gesendet: %s\n", message);
                }
                
                // Read answer
                
                bzero(inbuffer, 1);
                bzero(buffer, 512);
                for (i=0; i < 512; i++)
                {
                    length = read(fd, inbuffer,1);
                    buffer[i]= inbuffer[0];
                    if (inbuffer[0] == '}')
                    {
                        buffer[i+1] = '\0';
                        break;
                    }
                }
                if (length < 0)
                    break;
                buffer[i] = '\0';
                buffer[i-1] = '\0';
                buffer[i-2] = '\0';
                buffer[i-3] = '\0';
                buffer[i-4] = '\0';
                buffer[i-5] = '\0';
                
                strcat(buffer, ";SYS=FFFF;");
                
                if (DEBUG)
                    printf("Received answer: %s\n", buffer);
                
                //  char    buffer [512] = "KDY=B;KMT=40;KYR=B9F;KT0=1154;TNF=1388;TKK=19;PAC=5C;PRL=1;IL1=54;IDC=1A;UL1=910;UDC=A2C;SYS=FFFF;";
                if (DEBUG) {
                    sprintf(buffer2, "Received answer: %s", buffer);
                    debug_entry(buffer2);
                }
                
                // Extract the data fields from answer
                result = regexec(&rx, buffer, rx.re_nsub + 1, matches, 0);
                if (result) {
                    regerror(result, &rx, buffer, sizeof(buffer));
                    //error_exit("ERROR no regexp match");
                    error_retry("ERROR no regexp match");
                    lost_connection = 1;
                    break;
                }
                
                // Convert the extracted data fields to integer values
                temp = strndup(buffer + matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
                kdy = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);
                kmt = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so);
                kyr = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[4].rm_so, matches[4].rm_eo - matches[4].rm_so);
                kt0 = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[5].rm_so, matches[5].rm_eo - matches[5].rm_so);
                tnf = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[6].rm_so, matches[6].rm_eo - matches[6].rm_so);
                tkk = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[7].rm_so, matches[7].rm_eo - matches[7].rm_so);
                pac = strtol(temp, NULL, 16) /2 ;
                free(temp);
                temp = strndup(buffer + matches[8].rm_so, matches[8].rm_eo - matches[8].rm_so);
                prl = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[9].rm_so, matches[9].rm_eo - matches[9].rm_so);
                il1 = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[10].rm_so, matches[10].rm_eo - matches[10].rm_so);
                idc = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[11].rm_so, matches[11].rm_eo - matches[11].rm_so);
                ul1 = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[12].rm_so, matches[12].rm_eo - matches[12].rm_so);
                udc = strtol(temp, NULL, 16);
                free(temp);
                temp = strndup(buffer + matches[13].rm_so, matches[13].rm_eo - matches[13].rm_so);
                sys = strtol(temp, NULL, 16);
                free(temp);
                
                // Construct the query according to active solarmax
                //sprintf(query, "INSERT INTO %s%d (kdy, kmt, kyr, kt0, tnf, tkk, pac, prl, il1, idc, ul1, udc, sys) VALUES (%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d);", dbtabprefix, active_max, kdy, kmt, kyr, kt0, tnf, tkk, pac, prl, il1, idc, ul1, udc, sys);
                
                if (DEBUG) {
                    sprintf(buffer, "Executing query: %s", query);
                    debug_entry(buffer);
                }
                
//                // Execute the query to write the data into db
//                mysql_query(connection, query);
//                if (mysql_errno(connection))
//                    error_exit(mysql_error(connection));
            }
            
            // Get the current time
            time_t stop_time = time(NULL);
            
            // Wait for the specified number of seconds - calc duration - 1
            if (DEBUG)
                debug_entry("Waiting for about 1 minute ...");
            sleep((unsigned int) (log_interval + start_time - stop_time -1));
            
            // Add a busy-loop for the last second to make sure we are perfectly accurate
            while (time(NULL) < start_time + log_interval) usleep(99999);
        }
    }
    return 0;
}


