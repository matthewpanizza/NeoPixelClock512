# NeoPixelClock512

A program built for Particle devices for making a digital clock with live weather data using a 32x16 neopixel matrix. Successor to the argonclock project, with double the height, faster execution, and much higher modularity.

## Core Features

- High-brightness digital display using neopixels
- Night-mode for not lighting up the room when you're sleeping
- Live weather data using the OpenWeatherMap API and a webhook
    + Outdoor temperature
    + Outdoor Humidity
    + Weather Conditions (Sunny, Cloudy, Rainy, etc.)
- Animated transitions between temperature, humidity, conditions
- Button-operated menu for changing color, time zone, elements.

## Core Functions

- printScreen(): Pushes updates to the neopixel array from the software array
- snum(), num(): Bitmaps for numerical digits, fed into encode8Cond()
- letter(): Displays ASCII characters on the software array. printScreen() must be called to update harware
- strDisp(): Displays string of ASCII characters on the software array. printScreen() must be called to update harware
- encode8Cond(), encode32Cond(), encode64Cond(): Display 8, 32 or 64 bit bitmaps on the software array. Used by letter(), num(), snum().
- displayCondition(): Display a weather condition on the software array
- displayClock(): Display clock digits with a colon on the software array
