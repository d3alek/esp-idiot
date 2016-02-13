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
void parseThresholdDeltaStringInt();
void parseThresholdDeltaStringFloat();
void addGpio();
void run_tests();

int main(int argc, char **argv){
  initialize_mock_arduino();
  run_tests();
}

void run_tests() {
    printf("1\n");
    buildThresholdDeltaString();
    printf("2\n");
    parseThresholdDeltaStringInt();
    printf("3\n");
    parseThresholdDeltaStringFloat();
    printf("4\n");
    addGpio();
}

void buildThresholdDeltaString() {
    char result[30];
    Action::buildThresholdDeltaString(result, 10, 3);
    if (strcmp(result, "10.0~3.0")) {
        printf("Wrong result: %s\n", result);
    }
    else {
        printf("Test passed.\n");
    }
}

void parseThresholdDeltaStringInt() {
    Action result;

    result.parseThresholdDeltaString("10~3");

    assertAction(result, 10, 3);
}


void parseThresholdDeltaStringFloat() {
    Action result;

    result.parseThresholdDeltaString("10.0~3.0");

    assertAction(result, 10, 3);
}

void addGpio() {
    Action result;

    result.addGpio(3);
    if (!(result.getGpio(0) == 3)) {
        printf("Error setting GPIO 0 to 3\n");
        return;
    }
    result.addGpio(2);
    if (!(result.getGpio(1) == 2)) {
        printf("Error setting GPIO 1 to 2\n");
        return;
    }
    result.addGpio(1);
    if (!(result.getGpio(2) == 1)) {
        printf("Error setting GPIO 2 to 1\n");
        return;
    }

    if (!(result.getGpiosSize() == 3)) {
        printf("Wrong GPIO size\n");
        return;
    }

    printf("Test passed.");
}


bool assertAction(Action action, float threshold, float delta) {
    if (action.getThreshold() != threshold) {
        printf("Wrong threshold: %.1f\n", action.getThreshold());
        return false;
    }
    else if (action.getDelta() != delta) {
        printf("Wrong delta: %.1f\n", action.getDelta());
        return false;
    }
    else {
        printf("Test passed.\n");
        return true;
    }
}
