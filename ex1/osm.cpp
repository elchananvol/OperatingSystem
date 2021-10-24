//
// Created by שימי on 01/04/2021.
//
#include <iostream>
#include <sys/time.h>
#include <tgmath.h>
#include "osm.h"

struct timeval t1{}, t2{};

/*
 * an empty function.
 */
void empty_func(){}

/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time(unsigned int iterations)
{
    if (iterations == 0)
    {
        return -1;
    }

    gettimeofday(&t1, nullptr);
    int some_num = 0 , x = 4, y = 6;
    for (unsigned int i = 0; i < iterations; i += 30)
    {
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;
        some_num = x + y;

    }
    gettimeofday(&t2, nullptr);
    return
            double(((t2.tv_sec * pow(10, 9)) + (t2.tv_usec * pow(10, 3))) -
                   ((t1.tv_sec * pow(10, 9)) + (t1.tv_usec * pow(10, 3)))) / double(iterations);
}

/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time(unsigned int iterations)
{
    if (iterations == 0)
    {
        return -1;
    }
    gettimeofday(&t1, nullptr);
    for (unsigned int i = 0; i < iterations; i += 30)
    {
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();

    }
    gettimeofday(&t2, nullptr);
    return
            double(((t2.tv_sec * pow(10, 9)) + (t2.tv_usec * pow(10, 3))) -
                   ((t1.tv_sec * pow(10, 9)) + (t1.tv_usec * pow(10, 3)))) / double(iterations);

}

/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time(unsigned int iterations)
{

    if (iterations == 0)
    {
        return -1;
    }
    gettimeofday(&t1, nullptr);
    for (unsigned int i = 0; i < iterations; i += 30)
    {
        OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL;
        OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL;
        OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL;
        OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL;
        OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL;
        OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL; OSM_NULLSYSCALL;
    }
    gettimeofday(&t2, nullptr);
    return
            double(((t2.tv_sec * pow(10, 9)) + (t2.tv_usec * pow(10, 3))) -
                   ((t1.tv_sec * pow(10, 9)) + (t1.tv_usec * pow(10, 3)))) /
            double(iterations);
}