/**
 * Snímá teplotu + odesílá na cloud
 * @author Martin Nakládal <nakladal@intravps.cz>
 */
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


int data[5] = {0, 0, 0, 0, 0};

void sendHTTPS(float h, float c, float f);
int create_tcp_socket();
char *get_ip(char *host);
char *build_get_query(char *host, char *page);

#define PORT 80
#define USERAGENT "RAP 2D"

#define MAX_TIMINGS	85
#define DHT_PIN		3	/* GPIO-22 */

int main(void) {
    printf("Raspberry Pi DHT11/DHT22 temperature/humidity and send to SERVER CLOUD\n");

    if (wiringPiSetup() == -1)
        exit(1);

    while (1) {
        read_dht_data();
        delay(2000); /* wait 2 seconds before next read */
    }

    return (0);
}

int read_dht_data() {
    uint8_t laststate = HIGH;
    uint8_t counter = 0;
    uint8_t j = 0, i;

    data[0] = data[1] = data[2] = data[3] = data[4] = 0;

    /* pull pin down for 18 milliseconds */
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, LOW);
    delay(18);

    /* prepare to read the pin */
    pinMode(DHT_PIN, INPUT);

    /* detect change and read data */
    for (i = 0; i < MAX_TIMINGS; i++) {
        counter = 0;
        while (digitalRead(DHT_PIN) == laststate) {
            counter++;
            delayMicroseconds(1);
            if (counter == 255) {
                break;
            }
        }
        laststate = digitalRead(DHT_PIN);

        if (counter == 255)
            break;

        /* ignore first 3 transitions */
        if ((i >= 4) && (i % 2 == 0)) {
            /* shove each bit into the storage bytes */
            data[j / 8] <<= 1;
            if (counter > 16)
                data[j / 8] |= 1;
            j++;
        }
    }

    /*
     * check we read 40 bits (8bit x 5 ) + verify checksum in the last byte
     * print it out if data is good
     */
    if ((j >= 40) &&
            (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))) {
        float h = (float) ((data[0] << 8) + data[1]) / 10;
        if (h > 100) {
            h = data[0]; // for DHT11
        }
        float c = (float) (((data[2] & 0x7F) << 8) + data[3]) / 10;
        if (c > 125) {
            c = data[2]; // for DHT11
        }
        if (data[2] & 0x80) {
            c = -c;
        }
        float f = c * 1.8f + 32;

        printf("Humidity = %.1f %% Temperature = %.1f *C (%.1f *F)\n", h, c, f);

        sendHTTPS(h, c, f);
    } else {
        printf("Data not good, skip\n");
    }
    return 1;
}

void sendHTTPS(float h, float c, float f) {
    struct sockaddr_in *remote;
    int sock;
    int tmpres;
    char *ip;
    char *get;
    char buf[BUFSIZ + 1];
    char *host;
    char *url;
    char *page;
    char *pagetpl;
    FILE *temperatureFile;
    float T;
    /*
        printf("CPU CHECK\n");
        temperatureFile = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
        if (temperatureFile == NULL)
            ; //print some message
        fscanf(temperatureFile, "%lf", &T); 
        T = T/1000;
        printf("The temperature CPU is %6.3f C.\n", T);
     */

    printf("Start send\n");
    host = "api.thingspeak.com";
    pagetpl = "/update?api_key=111111111111&field1=%.1f&field2=%.1f&field3=%.1f";
    sprintf(page, pagetpl, h, c, f);
    printf("bind ok \n");
    sock = create_tcp_socket();
    ip = get_ip(host);
    fprintf(stderr, "IP is %s\n", ip);
    remote = (struct sockaddr_in *) malloc(sizeof (struct sockaddr_in *));
    remote->sin_family = AF_INET;
    tmpres = inet_pton(AF_INET, ip, (void *) (&(remote->sin_addr.s_addr)));
    if (tmpres < 0) {
        perror("Can't set remote->sin_addr.s_addr");
        exit(1);
    } else if (tmpres == 0) {
        fprintf(stderr, "%s is not a valid IP address\n", ip);
        exit(1);
    }
    remote->sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *) remote, sizeof (struct sockaddr)) < 0) {
        perror("Could not connect");
        exit(1);
    }
    printf("Build ok \n");
    get = build_get_query(host, page);
    /*
      fprintf(stderr, "Query is:\n<<START>>\n%s<<END>>\n", get);
     */
    fprintf(stderr, "Query send...\n", get);

    //Send the query to the server
    int sent = 0;
    while (sent < strlen(get)) {
        tmpres = send(sock, get + sent, strlen(get) - sent, 0);
        if (tmpres == -1) {
            perror("Can't send query");
            exit(1);
        }
        sent += tmpres;
    }
    //now it is time to receive the page
    memset(buf, 0, sizeof (buf));
    int htmlstart = 0;
    char * htmlcontent;
    while ((tmpres = recv(sock, buf, BUFSIZ, 0)) > 0) {
        if (htmlstart == 0) {
            /* Under certain conditions this will not work.
             * If the \r\n\r\n part is splitted into two messages
             * it will fail to detect the beginning of HTML content
             */
            htmlcontent = strstr(buf, "\r\n\r\n");
            if (htmlcontent != NULL) {
                htmlstart = 1;
                htmlcontent += 4;
            }
        } else {
            htmlcontent = buf;
        }
        if (htmlstart) {
            fprintf(stdout, htmlcontent);
        }

        memset(buf, 0, tmpres);
    }
    if (tmpres < 0) {
        perror("Error receiving data");
    }
    free(get);
    free(remote);
    free(ip);
    close(sock);
    printf("Send...\n");

}

int create_tcp_socket() {
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Can't create TCP socket");
        exit(1);
    }
    return sock;
}

char *get_ip(char *host) {
    struct hostent *hent;
    int iplen = 15; //XXX.XXX.XXX.XXX
    char *ip = (char *) malloc(iplen + 1);
    memset(ip, 0, iplen + 1);
    if ((hent = gethostbyname(host)) == NULL) {
        herror("Can't get IP");
        exit(1);
    }
    if (inet_ntop(AF_INET, (void *) hent->h_addr_list[0], ip, iplen) == NULL) {
        perror("Can't resolve host");
        exit(1);
    }
    return ip;
}

char *build_get_query(char *host, char *page) {
    char *query;
    char *getpage = page;
    char *tpl = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
    if (getpage[0] == '/') {
        getpage = getpage + 1;
        fprintf(stderr, "Removing leading \"/\", converting %s to %s\n", page, getpage);
    }
    // -5 is to consider the %s %s %s in tpl and the ending \0
    query = (char *) malloc(strlen(host) + strlen(getpage) + strlen(USERAGENT) + strlen(tpl) - 5);
    sprintf(query, tpl, getpage, host, USERAGENT);
    return query;
}
