# join_the_pipe
Join The Pipe . org Amsterdam (code for custom controller Concertgebouw Amsterdam)

This is the code written for a custom project, used in "The Concertgebouw"  in Amsterdam.
It runs on a Arduino Clone with rugged IO : A Controllino.
Settings of the trained timers are stored inside the eeprom of the controller so it can be installed by someone else, trained and will work forever after that...  

https://www.controllino.biz/product/controllino-mini/

The system had 2 valves and 2 buttons to dispense water (one flat, one with bubbles)

The teach in mode allowed the timers to be set on the spot.

Pressing the 2 buttons for 2 .. 10 seconds puts the sytem in Auto-dispensing mode.

Pressing the 2 buttons for 10 .. 30 seconds puts it into teach-in mode (training the times for dispensing)

The code was created by me, Edwin van den Oetelaar, oktober 2016, for a contract, but maybe someone can learn from it now.

If you need special project code developed, give me a message ( edwin @ oetelaar dot com )

Also important, this code is MIT license now, no warranties whatsoever, it may set your house on fire or get your girlfriend pregnant, use it at your own risk or not use it at all.

Some technical details :

Inputs must be pulled to HIGH to activate. (buttons on pins 4 and 5)
Output flash signal light is on pin 6 (high is active)
Output buzzer is on pin 7 (high is active)
Output to activate valve 1 is on pin 14 (active high)
Output to activate valve 2 is on pin 15 (active high)

This code was written at night from a friday..saturday, it had to be installed on the next monday.
I had to save someone elses lack of planning, I could not let the project fail ... so there we go again.
