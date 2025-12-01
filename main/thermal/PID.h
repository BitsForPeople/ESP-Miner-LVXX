#ifndef PID_H
#define PID_H

#include <stdbool.h>

#define AUTOMATIC 1
#define MANUAL 0

// The floating-point type to use:
typedef float pid_fp_t;

typedef enum {
    PID_DIRECT = 0,
    PID_REVERSE = 1
} PIDDirection;

typedef enum {
    PID_P_ON_M = 0, 
    PID_P_ON_E = 1
} PIDProportionalMode;

typedef struct {
    pid_fp_t dispKp;
    pid_fp_t dispKi;
    pid_fp_t dispKd;

    pid_fp_t kp;
    pid_fp_t ki;
    pid_fp_t kd;

    PIDDirection controllerDirection;
    PIDProportionalMode pOn;
    bool pOnE;

    pid_fp_t *input;
    pid_fp_t *output;
    pid_fp_t *setpoint;

    unsigned long lastTime;
    unsigned long sampleTime;
    pid_fp_t outMin;
    pid_fp_t outMax;
    bool inAuto;

    pid_fp_t outputSum;
    pid_fp_t lastInput;
} PIDController;

void pid_init(PIDController *pid, pid_fp_t *input, pid_fp_t *output, pid_fp_t *setpoint,
              pid_fp_t Kp, pid_fp_t Ki, pid_fp_t Kd, PIDProportionalMode POn, PIDDirection ControllerDirection);

void pid_set_mode(PIDController *pid, int mode);
bool pid_compute(PIDController *pid);
void pid_set_output_limits(PIDController *pid, pid_fp_t min, pid_fp_t max);
void pid_set_tunings(PIDController *pid, pid_fp_t Kp, pid_fp_t Ki, pid_fp_t Kd);
void pid_set_tunings_adv(PIDController *pid, pid_fp_t Kp, pid_fp_t Ki, pid_fp_t Kd, PIDProportionalMode POn);
void pid_set_sample_time(PIDController *pid, int newSampleTime);
void pid_set_controller_direction(PIDController *pid, PIDDirection direction);
void pid_initialize(PIDController *pid);

// Getter functions
pid_fp_t pid_get_kp(PIDController *pid);
pid_fp_t pid_get_ki(PIDController *pid);
pid_fp_t pid_get_kd(PIDController *pid);
pid_fp_t pid_get_ti(PIDController *pid);
pid_fp_t pid_get_td(PIDController *pid);
int pid_get_mode(PIDController *pid);
PIDDirection pid_get_direction(PIDController *pid);

#endif
