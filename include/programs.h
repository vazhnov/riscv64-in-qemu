#ifndef _PROGRAMS_H_
#define _PROGRAMS_H_

typedef struct user_program_s {
    void *entry_point;
    char *name;
} user_program_t;

// defined in proc_test.c:
extern user_program_t userland_programs[MAX_USERLAND_PROGS];
extern int num_userland_progs;

user_program_t* find_user_program(char const *name);

#endif // ifndef _PROGRAMS_H_
