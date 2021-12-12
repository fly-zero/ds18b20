#!/bin/python

import atexit
import RPi.GPIO as GPIO
import time

def on_exit() :
    GPIO.output(12, 0)

def main() :
    GPIO.setmode(GPIO.BOARD)
    GPIO.setwarnings(False)
    GPIO.setup(12, GPIO.OUT)
    val = 0
    atexit.register(on_exit)
    while True :
        val = 1 ^ val
        GPIO.output(12, val)
        try :
            time.sleep(0.5)
        except KeyboardInterrupt :
            break

if __name__ == "__main__" :
    main()
