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

using namespace std;

bool assertAction(Action, float, float);
void buildThresholdDeltaString();
void parseThresholdDeltaString();
void buildSenseAndGpioString();
void parseSenseAndGpioString();
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
    buildSenseAndGpioString();
    printf("4\n");
    parseSenseAndGpioString();
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

void buildSenseAndGpioString() {
    char result[30];
    Action action("sense");
    action.setGpio(10);
    action.buildSenseAndGpioString(result);

    if (strcmp(result, "A|sense|10")) {
        printf("Wrong sense and gpio string: %s\n", result);
    }
    else {
        printf("Test passed.\n");
    }
}

void parseSenseAndGpioString() {
    Action action;
    const char* senseAndGpioString = "A|sense|12";
    action.parseSenseAndGpio(senseAndGpioString);

    if (strcmp(action.getSense(), "sense")) {
        printf("Wrong sense parsed: %s\n", action.getSense());
    }
    else if (action.getGpio() != 12) {
        printf("Wrong gpio parsed: %d\n", action.getGpio());
    }
    else {
        printf("Test passed.\n");
    }
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
