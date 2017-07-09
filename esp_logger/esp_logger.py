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

import re

VERSION = "d1"
DIR = "/idiot/esp-idiot/esp_logger/"
THING_NAME_PATTERN = re.compile(r'^publish to things/([a-zA-Z0-9-]+)/update')

logging.basicConfig(level=logging.INFO)

def get_handler(name):
    handler = TimedRotatingFileHandler(DIR + 'logs/' + name + '.txt', when='midnight', interval=1, utc=True)
    handler.setFormatter(logging.Formatter("%(asctime)-15s:%(message)s"))
    return handler

def get_logger(func, thing):
    if thing == None:
        thing = 'no_name'

    logger_name = func + '.' + thing
    logger = logging.getLogger(logger_name)
    for handler in logger.handlers:
        logger.removeHandler(handler)

    logger.addHandler(get_handler(logger_name))

    return logger


def log_serial():
    global thing
    ser = serial.Serial('/dev/ttyUSB0', 115200)
    ser.flushInput()
    ser.flushOutput()

    logger = get_logger('log_serial', thing)
    logger.info("Searching for thing name in serial logs...")
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
                    m = THING_NAME_PATTERN.match(line)
                    if m:
                        new_thing = m.group(1)
                        if thing == None or new_thing != new_thing:
                            logger.info('Changing thing %s->%s' % (thing, new_thing))
                            thing = new_thing
                            logger = get_logger('log_serial', thing)

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

def act_like_a_thing():
    reported_file = open(DIR + '/'.join(['logs', 'reported.json']), 'w')
    reported_file.write(state())
    reported_file.close()

    should_enchant = open(DIR + '/'.join(['logs', '.should-enchant.flag']), 'w')
    should_enchant.write("flag")
    should_enchant.close()

def continuously_upload_log():
    global thing

    logger = get_logger('continuously_upload_log', thing)

    try:
        act_like_a_thing()
        
        logger.info("Starting upload...")
        logger.info(" ".join(["rsync", "-az", "--rsh=ssh -p8902 -i " + DIR + "otselo_id_rsa", DIR + 'logs/', "otselo@otselo.eu:/www/zelenik/db/esp_logger"]))
        subprocess.call(["rsync", "-az", "--rsh=ssh -p8902 -i " + DIR + "otselo_id_rsa", DIR + 'logs/', "otselo@otselo.eu:/www/zelenik/db/esp_logger"])
        logger.info("Finished upload")
    except Exception, e:
        logger.exception(e)
    finally:
        t = Timer(30, continuously_upload_log)
        t.start()

if __name__ == '__main__':
    global thing
    thing = None
    boot_utc = datetime.utcnow()
    continuously_upload_log()
    logger = get_logger('main', thing)
    while True:
        try:
            log_serial()
        except Exception, e:
            logger.exception(e)
            time.sleep(1)
