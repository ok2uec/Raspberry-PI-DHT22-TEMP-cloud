# Raspberry-PI + DHT22 send to thingspeak.com
==========


## Build main.c
gcc -o dht dht2.c -lwiringPi -lwiringPiDev


## RUN

sudo ./dht


## CRON

*/5 *    * * *   root    /home/pi/dht
