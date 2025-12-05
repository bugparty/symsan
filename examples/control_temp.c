#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// Simple atoi that works with tainted data
static int my_atoi(const char *s) {
    int result = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

int main(int argc, char *argv[]) {
    // Read input from stdin: "mode temp userLevel emergency"
    char input[256];
    ssize_t n = read(0, input, sizeof(input) - 1);
    if (n <= 0) {
        fprintf(stderr, "Failed to read input from stdin\n");
        return 1;
    }
    input[n] = '\0';

    // Parse input manually: mode temp userLevel emergency
    char *p = input;
    char *tokens[4];
    int ti = 0;
    
    // Skip leading whitespace
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    
    // Tokenize by whitespace
    while (*p && ti < 4) {
        tokens[ti++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) *p++ = '\0';
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    }
    
    if (ti < 4) {
        fprintf(stderr, "Invalid input format. Expected: mode temp userLevel emergency\n");
        return 1;
    }

    int mode = my_atoi(tokens[0]);
    int temp = my_atoi(tokens[1]);
    int userLevel = my_atoi(tokens[2]);
    bool emergency = (my_atoi(tokens[3]) != 0);

    // Control function logic inlined
    bool open = false;
    bool locked = false;
    bool sensorOk = true; // Inlined check_sensor - returns fixed value

    printf("\n=== Control Function Debug ===\n");
    printf("Input: mode=%d, temp=%d, userLevel=%d, emergency=%d\n",
           mode, temp, userLevel, emergency);
    printf("Sensor status: %s\n", sensorOk ? "OK" : "FAIL");

    if (mode == 1) {
        printf("Mode 1: Temperature-based control\n");
        if (temp > 30 && sensorOk) {
            open = true;
        } else {
            open = false;
        }

    } else if (mode == 2) {
        printf("Mode 2: User level control\n");
        locked = true;
        if (userLevel >= 5) {
            open = true;
            locked = false;
        } else {
            // Do nothing
        }

    } else {
        printf("Mode 3 (default): Normal operation\n");
        if (!sensorOk) {
            // Inlined log_error
            fprintf(stderr, "[ERROR] Bad sensor\n");
            open = false;
        } else {
            if (temp >= 18 && temp <= 26) {
                open = true;
            } else {
                open = false;
            }
        }
    }

    if (emergency) {
        printf("Emergency mode activated!\n");
        if (userLevel >= 10) {
            open = true;
            locked = false;
        } else {
            open = false;
        }
    }

    if (locked) {
        printf("System is LOCKED\n");
        open = false;
    }

    printf("Final state: open=%d, locked=%d\n", open, locked);
    printf("==============================\n\n");

    if (open) {
        // Inlined ok function
        printf("[OK] Door is OPEN\n");
    } else {
        printf("[INFO] Door remains CLOSED\n");
    }

    return 0;
}
