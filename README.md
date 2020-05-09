# Digital Rain Guage

Simple rain guages can be found anywhere on the interwebs, but a good prepackaged solution came from the following link.

https://rayshobby.net/reverse-engineer-wireless-temperature-humidity-rain-sensors-part-3/

It was a great hack of an Accurite 899 rainguage, which can be found online through Amazon or other retailers, usually for less than $40 US. With a bit of clever coding, you can snoop on the specific data updates from the sensor, and do some simple math to get inches/centimeters of rain fall.

The original project used an Arduino, but to allow for easy online, I modified it a bit to use an ESP8266. The benefit here is that 8266 is 5V tolerant, which means you don't have to level shift the receiver to get the data. In testing with 3v3 micros, I had a lot of trouble getting data when using level shifting.
