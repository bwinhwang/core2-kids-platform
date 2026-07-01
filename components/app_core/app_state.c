#include "app_state.h"

const char *app_state_name(app_state_t s)
{
    switch (s) {
        case APP_INIT:    return "INIT";
        case APP_WELCOME: return "WELCOME";
        case APP_IDLE:    return "IDLE";
        case APP_ACTIVE:  return "ACTIVE";
        case APP_DIM:     return "DIM";
        case APP_LOW_BAT: return "LOW_BAT";
        default:          return "?";
    }
}
