#ifndef BOWLING_MODEL_H
#define BOWLING_MODEL_H
#include <stdbool.h>
#define BOWL_FEATURES 13
#define BOWL_PATH_MAX 1600
#define BOWL_HALF_WIDTH 1.6f
#define BOWL_LENGTH 7.5f
#define BOWL_BALL_RADIUS 0.16f
#define BOWL_PIN_RADIUS 0.13f
typedef struct {
    unsigned count;
    float peak, low[3], high[3], integral[3], sum_a[3], previous_g[3], previous_t;
} BowlFeatures;
typedef struct { int label; float power, hook, certainty, peak, features[13]; } BowlThrow;
typedef struct {
    float start_x, aim_deg, speed_gain, hook_gain, friction, hook_start, randomness;
} BowlTuning;
typedef struct { float x,y,t; } BowlPoint;
typedef struct {
    BowlPoint points[BOWL_PATH_MAX];
    unsigned count;
    int pin; // first pin index, -1 if none; no pin-to-pin physics yet
    bool gutter, stopped;
    float speed, hook_accel;
} BowlPath;
extern const char *bowl_labels[5];
void bowl_features_reset(BowlFeatures *f);
void bowl_features_add(BowlFeatures *f, const float a[3], const float g[3], float t);
bool bowl_classify(const BowlFeatures *f, BowlThrow *result);
BowlTuning bowl_defaults(void);
bool bowl_set(BowlTuning *t, const char *key, float value);
void bowl_pin(int index, float *x, float *y);
void bowl_simulate(const BowlThrow *shot, const BowlTuning *tuning, unsigned seed, BowlPath *path);
#endif
