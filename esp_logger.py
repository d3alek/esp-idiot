#!/idiot/esp-idiot/venv/bin/python

from __future__ import print_function
import serial

import time

import logging
from logging.handlers import TimedRotatingFileHandler

import subprocess
from threading import Timer

import sys

DIR = "/idiot/esp-idiot/"
logging.basicConfig(level=logging.INFO)

def log_serial():
    ser = serial.Serial('/dev/ttyUSB0', 115200)
    ser.flushInput()
    ser.flushOutput()

    logger = logging.getLogger('log_serial')
    if len(logger.handlers) == 0:
        handler = TimedRotatingFileHandler(DIR + 'logs/esp_logger.txt', when='midnight', interval=1, utc=True)
        logger.addHandler(handler)
    
    logger.info("Starting to read serial...")
    message = ''
    try:
        while True:
            bytesToRead = ser.inWaiting()
            bytes = ser.read(bytesToRead)
            print(bytes, end='')
            message += bytes
            if len(message) > 0 and message[-1] == '\n':
                logger.info(message)
                message = ''

            time.sleep(0.5)
    finally: 
        ser.close()
        logger.info("Stopped reading serial")

def continuously_upload_log():
    logger = logging.getLogger('continuously_upload_log')
    logger.info("Starting upload...")
    subprocess.call(["rsync", "-az", "--rsh=ssh -p8902 -i /home/alek/.ssh/otselo_id_rsa", DIR + 'logs/', "otselo@otselo.eu:/www/zelenik/db/esp_logger"])
    logger.info("Finished upload")
    t = Timer(30, continuously_upload_log)
    t.start()

if __name__ == '__main__':
    continuously_upload_log()
    logger = logging.getLogger('main')
    while True:
        try:
            log_serial()
        except Exception, e:
            logger.exception(e)
            time.sleep(1)
