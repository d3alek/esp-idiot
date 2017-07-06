#!/idiot/esp-idiot/venv/bin/python

from __future__ import print_function
import serial

import time

import logging
from logging.handlers import TimedRotatingFileHandler

import subprocess
from threading import Timer

import sys

from datetime import datetime
import json

VERSION = "d1"
DIR = "/idiot/esp-idiot/esp_logger/"
logging.basicConfig(level=logging.INFO)

def get_handler(name):
    handler = TimedRotatingFileHandler(DIR + 'logs/' + name + '.txt', when='midnight', interval=1, utc=True)
    handler.setFormatter(logging.Formatter("%(asctime)-15s:%(message)s"))
    return handler

def log_serial():
    ser = serial.Serial('/dev/ttyUSB0', 115200)
    ser.flushInput()
    ser.flushOutput()

    logger = logging.getLogger('log_serial')
    if len(logger.handlers) == 0:
        logger.addHandler(get_handler('esp_log'))
    logger.info("Starting to read serial...")
    message = ''
    try:
        while True:
            bytesToRead = ser.inWaiting()
            bytes = ser.read(bytesToRead)
            print(bytes, end='')
            message += bytes
            split = message.split('\n')
            if len(message) > 0 and len(split) > 1:
                for line in split[:-1]:
                    logger.info(line)
                message = split[-1]

            time.sleep(0.5)
    finally: 
        ser.close()
        logger.info("Stopped reading serial")

def pretty_json(d):
    return json.dumps(d, sort_keys=True, indent=4, separators=(',', ': '), ensure_ascii=False)

def timestamp(time):
    time = time.replace(microsecond=0)
    return time.isoformat(sep=' ')

def state():
    return pretty_json({"state": {"boot_utc": timestamp(boot_utc), "version": VERSION}, "timestamp_utc": timestamp(datetime.utcnow())})

def continuously_upload_log():
    logger = logging.getLogger('continuously_upload_log')
    if len(logger.handlers) == 0:
        logger.addHandler(get_handler('upload_log'))

    try:
        reported_file = open(DIR + '/'.join(['logs', 'reported.json']), 'w')
        reported_file.write(state())
        reported_file.close()

        should_enchant = open(DIR + '/'.join(['logs', '.should-enchant.flag']), 'w')
        should_enchant.write("flag")
        should_enchant.close()
        
        logger.info("Starting upload...")
        subprocess.call(["rsync", "-az", "--rsh=ssh -p8902 -i " + DIR + "otselo_id_rsa", DIR + 'logs/', "otselo@otselo.eu:/www/zelenik/db/esp_logger"])
        logger.info("Finished upload")
    except Exception, e:
        logger.exception(e)
    finally:
        t = Timer(30, continuously_upload_log)
        t.start()

if __name__ == '__main__':
    boot_utc = datetime.utcnow()
    continuously_upload_log()
    logger = logging.getLogger('main')
    logger.addHandler(get_handler('main'))
    while True:
        try:
            log_serial()
        except Exception, e:
            logger.exception(e)
            time.sleep(1)
