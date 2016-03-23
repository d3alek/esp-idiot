/*
  DSM2_tx implements the serial communication protocol used for operating
  the RF modules that can be found in many DSM2-compatible transmitters.

  Copyrigt (C) 2012  Erik Elmore <erik@ironsavior.net>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.  
*/

#include "mock_arduino.h"
#include "Action.h"
#include "GpioState.h"

using namespace std;

bool assertAction(Action, float, float);
void buildThresholdDeltaString();
void parseThresholdDeltaString();
void parseNullThresholdDeltaString();
void buildHighSenseAndGpioString();
void buildLowSenseAndGpioString();
void parseHighSenseAndGpioString();
void parseLowSenseAndGpioString();
void gpioState();
void run_tests();

int main(int argc, char **argv){
  initialize_mock_arduino();
  run_tests();
}

void run_tests() {
    printf("1\n");
    buildThresholdDeltaString();
    printf("2\n");
    parseThresholdDeltaString();
    printf("3\n");
    parseNullThresholdDeltaString();
    printf("4\n");
    buildLowSenseAndGpioString();
    printf("5\n");
    buildHighSenseAndGpioString();
    printf("6\n");
    parseLowSenseAndGpioString();
    printf("7\n");
    parseHighSenseAndGpioString();
    printf("8\n");
    gpioState();
}

void buildThresholdDeltaString() {
    char result[30];
    Action::buildThresholdDeltaString(result, 10, 3);
    if (strcmp(result, "10~3")) {
        printf("Wrong result: %s\n", result);
    }
    else {
        printf("Test passed.\n");
    }
}

void parseThresholdDeltaString() {
    Action result;

    result.parseThresholdDeltaString("10~3");

    assertAction(result, 10, 3);
}

void parseNullThresholdDeltaString() {
    Action result;

    result.parseThresholdDeltaString(NULL);

    assertAction(result, -1, -1);
}

void buildHighSenseAndGpioString() {
    char result[30];
    Action action("sense");
    action.setGpio(10);
    action.setAboveThresholdGpioState(HIGH);
    action.buildSenseAndGpioString(result);

    if (strcmp(result, "A|sense|10H")) {
        printf("Wrong sense and gpio string: %s\n", result);
    }
    else {
        printf("Test passed.\n");
    }
}

void buildLowSenseAndGpioString() {
    char result[30];
    Action action("sense");
    action.setGpio(10);
    action.setAboveThresholdGpioState(LOW);
    action.buildSenseAndGpioString(result);

    if (strcmp(result, "A|sense|10L")) {
        printf("Wrong sense and gpio string: %s\n", result);
    }
    else {
        printf("Test passed.\n");
    }
}

void parseLowSenseAndGpioString() {
    Action action;
    const char* senseAndGpioString = "A|sense|12L";
    action.parseSenseAndGpio(senseAndGpioString);

    if (strcmp(action.getSense(), "sense")) {
        printf("Wrong sense parsed: %s\n", action.getSense());
    }
    else if (action.getGpio() != 12) {
        printf("Wrong gpio parsed: %d\n", action.getGpio());
    }
    else if (action.getAboveThresholdGpioState() != LOW) {
        printf("Wrong above-threshold-gpio-state parsed: %d\n", action.getAboveThresholdGpioState());
    }
    else {
        printf("Test passed.\n");
    }
}

void parseHighSenseAndGpioString() {
    Action action;
    const char* senseAndGpioString = "A|sense|12H";
    action.parseSenseAndGpio(senseAndGpioString);

    if (strcmp(action.getSense(), "sense")) {
        printf("Wrong sense parsed: %s\n", action.getSense());
    }
    else if (action.getGpio() != 12) {
        printf("Wrong gpio parsed: %d\n", action.getGpio());
    }
    else if (action.getAboveThresholdGpioState() != HIGH) {
        printf("Wrong above-threshold-gpio-state parsed: %d\n", action.getAboveThresholdGpioState());
    }
    else {
        printf("Test passed.\n");
    }
}



void gpioState() {
    GpioState.clear();

    GpioState.set(1, HIGH); 

    if (GpioState.getSize() != 1 || GpioState.getGpio(0) != 1 || GpioState.getState(0) != 1) {
        printf("Wrong GpioState after first set: %d %d %d\n", GpioState.getSize(), GpioState.getGpio(0), GpioState.getState(0));
        return;
    }
    
    GpioState.set(2, LOW); 

    if (GpioState.getSize() != 2 || GpioState.getGpio(1) != 2 || GpioState.getState(1) != 0) {
        printf("Wrong GpioState after second set: %d %d %d\n", GpioState.getSize(), GpioState.getGpio(1), GpioState.getState(1));
        return;
    }

    GpioState.set(1, LOW); 

    if (GpioState.getSize() != 2 || GpioState.getGpio(0) != 1 || GpioState.getState(0) != 0) {
        printf("Wrong GpioState after third set: %d %d %d\n", GpioState.getSize(), GpioState.getGpio(0), GpioState.getState(0));
        return;
    }

    GpioState.set(3, HIGH); 

    if (GpioState.getSize() != 3 || GpioState.getGpio(2) != 3 || GpioState.getState(2) != 1) {
        printf("Wrong GpioState after fourth set: %d %d %d\n", GpioState.getSize(), GpioState.getGpio(2), GpioState.getState(2));
        return;
    }

    printf("Test passed.\n");
}

bool assertAction(Action action, float threshold, float delta) {
    if (action.getThreshold() != threshold) {
        printf("Wrong threshold: %d\n", action.getThreshold());
        return false;
    }
    else if (action.getDelta() != delta) {
        printf("Wrong delta: %d\n", action.getDelta());
        return false;
    }
    else {
        printf("Test passed.\n");
        return true;
    }
}
