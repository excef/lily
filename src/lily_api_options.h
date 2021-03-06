#ifndef LILY_API_OPTIONS_H
# define LILY_API_OPTIONS_H

typedef void (*lily_html_sender)(char *, void *);
typedef struct lily_options_ lily_options;

lily_options *lily_new_options(void);
void lily_free_options(lily_options *);

void lily_op_allow_sys(lily_options *, int);
void lily_op_argv(lily_options *, int, char **);
void lily_op_data(lily_options *, void *);
void lily_op_freeze(lily_options *);
void lily_op_gc_start(lily_options *, int);
void lily_op_gc_multiplier(lily_options *, int);
void lily_op_html_sender(lily_options *, lily_html_sender);

int lily_op_get_allow_sys(lily_options *);
char **lily_op_get_argv(lily_options *, int *);
void *lily_op_get_data(lily_options *);
int lily_op_get_gc_start(lily_options *);
int lily_op_get_gc_multiplier(lily_options *);
lily_html_sender lily_op_get_html_sender(lily_options *);

#endif
