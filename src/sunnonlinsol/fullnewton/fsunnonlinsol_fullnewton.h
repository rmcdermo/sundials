/* -----------------------------------------------------------------------------
 * Programmer(s): David J. Gardner @ LLNL
 * -----------------------------------------------------------------------------
 * LLNS Copyright Start
 * Copyright (c) 2014, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 * -----------------------------------------------------------------------------
 * This file contains the definitions needed for initialization of the
 * SUNNonlinearSolver Full Newton moudule operations in Fortran.
 * ---------------------------------------------------------------------------*/

#ifndef _FSUNNONLINSOL_FULLNEWTON_H
#define _FSUNNONLINSOL_FULLNEWTON_H

#include <sundials/sundials_fnvector.h>       /* FCMIX_* solver IDs */
#include <sunnonlinsol/sunnonlinsol_fullnewton.h>

#ifdef __cplusplus  /* wrapper to enable C++ usage */
extern "C" {
#endif

#if defined(SUNDIALS_F77_FUNC)
#define FSUNFULLNEWTON_INIT        SUNDIALS_F77_FUNC(fsunfullnewtoninit,        FSUNFULLNEWTONINIT)
#define FSUNFULLNEWTON_SETMAXITERS SUNDIALS_F77_FUNC(fsunfullnewtonsetmaxiters, FSUNFULLNEWTONSETMAXITERS)
#else
#define FSUNNEWTON_INIT        fsunfullnewtoninit_
#define FSUNNEWTON_SETMAXITERS fsunfullnewtonsetmaxiters_
#endif

/* Declarations of global variables */

extern SUNNonlinearSolver F2C_CVODE_nonlinsol;
extern SUNNonlinearSolver F2C_IDA_nonlinsol;
extern SUNNonlinearSolver F2C_ARKODE_nonlinsol;

/* -----------------------------------------------------------------------------
 * Prototypes of exported functions 
 *
 * FSUNNEWTON_INIT - initializes Newton nonlinear solver for main problem
 * FSUNNEWTON_SETMAXITERS - sets the maximum number of nonlinear iterations
 * ---------------------------------------------------------------------------*/

void FSUNFULLNEWTON_INIT(int *code, int *ier);
void FSUNFULLNEWTON_SETMAXITERS(int *code, int *maxiters, int *ier);

#ifdef __cplusplus
}
#endif

#endif