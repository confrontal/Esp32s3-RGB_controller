#ifndef GLOBAL_SETTINGS_H
#define GLOBAL_SETTINGS_H

#include <stdio.h>
#include "ws28xx/ws28xx.h"


typedef struct g_Settings{
    CRGB *rgb_buffer;
    int length_rgb_buffer;
    int max_length_rgb_buffer;
    bool apply_all;

}g_Settings;



#endif