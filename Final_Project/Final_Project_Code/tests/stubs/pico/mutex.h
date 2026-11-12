typedef struct { int unused; } mutex_t;
void mutex_init(mutex_t *m);
void mutex_enter_blocking(mutex_t *m);
void mutex_exit(mutex_t *m);
