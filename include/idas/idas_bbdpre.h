/*
 * -----------------------------------------------------------------
 * $Revision: 1.4 $
 * $Date: 2007-04-11 22:34:09 $
 * ----------------------------------------------------------------- 
 * Programmer(s): Alan C. Hindmarsh, Radu Serban and
 *                Aaron Collier @ LLNL
 * -----------------------------------------------------------------
 * Copyright (c) 2002, The Regents of the University of California.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * -----------------------------------------------------------------
 * This is the header file for the IDABBDPRE module, for a
 * band-block-diagonal preconditioner, i.e. a block-diagonal
 * matrix with banded blocks, for use with IDAS and
 * IDASpgmr/IDASpbcg/IDASptfqmr.
 */

#ifndef _IDASBBDPRE_H
#define _IDASBBDPRE_H

#ifdef __cplusplus     /* wrapper to enable C++ usage */
extern "C" {
#endif

#include <sundials/sundials_nvector.h>

/*
 * =================================================================
 *             I D A S B B D P R E     C O N S T A N T S
 * =================================================================
 */

/* IDABBDPRE return values */

#define IDABBDPRE_SUCCESS           0
#define IDABBDPRE_PDATA_NULL       -11
#define IDABBDPRE_FUNC_UNRECVR     -12

#define IDABBDPRE_ADJMEM_NULL      -111
#define IDABBDPRE_PDATAB_NULL      -112
#define IDABBDPRE_MEM_FAIL         -113

/* 
 * =================================================================
 * PART I - forward problems
 * =================================================================
 */

/*
 * -----------------------------------------------------------------
 *
 * SUMMARY
 *
 * These routines provide a preconditioner matrix that is
 * block-diagonal with banded blocks. The blocking corresponds
 * to the distribution of the dependent variable vector y among
 * the processors. Each preconditioner block is generated from
 * the Jacobian of the local part (on the current processor) of a
 * given function G(t,y,y') approximating F(t,y,y'). The blocks
 * are generated by a difference quotient scheme on each processor
 * independently. This scheme utilizes an assumed banded structure
 * with given half-bandwidths, mudq and mldq. However, the banded
 * Jacobian block kept by the scheme has half-bandwiths mukeep and
 * mlkeep, which may be smaller.
 *
 * The user's calling program should have the following form:
 *
 *   #include <idas/idas_bbdpre.h>
 *   #include <nvector_parallel.h>
 *   ...
 *   void *p_data;
 *   ...
 *   y0  = N_VNew_Parallel(...);
 *   yp0 = N_VNew_Parallel(...);
 *   ...
 *   ida_mem = IDACreate(...);
 *   ier = IDAMalloc(...);
 *   ...
 *   p_data = IDABBDPrecAlloc(ida_mem, Nlocal, mudq, mldq,
 *                            mukeep, mlkeep, dq_rel_yy, Gres, Gcomm);
 *   flag = IDABBDSptfqmr(ida_mem, maxl, p_data);
 *       -or-
 *   flag = IDABBDSpgmr(ida_mem, maxl, p_data);
 *       -or-
 *   flag = IDABBDSpbcg(ida_mem, maxl, p_data);
 *   ...
 *   ier = IDASolve(...);
 *   ...
 *   IDABBDFree(&p_data);
 *   ...
 *   IDAFree(...);
 *
 *   N_VDestroy(y0);
 *   N_VDestroy(yp0);
 *
 * The user-supplied routines required are:
 *
 *   res  is the function F(t,y,y') defining the DAE system to
 *   be solved:  F(t,y,y') = 0.
 *
 *   Gres  is the function defining a local approximation
 *   G(t,y,y') to F, for the purposes of the preconditioner.
 *
 *   Gcomm  is the function performing communication needed
 *   for Glocal.
 *
 * Notes:
 *
 * 1) This header file is included by the user for the definition
 *    of the IBBDPrecData type and for needed function prototypes.
 *
 * 2) The IDABBDPrecAlloc call includes half-bandwidths mudq and
 *    mldq to be used in the approximate Jacobian. They need
 *    not be the true half-bandwidths of the Jacobian of the
 *    local block of G, when smaller values may provide a greater
 *    efficiency. Similarly, mukeep and mlkeep, specifying the
 *    bandwidth kept for the approximate Jacobian, need not be
 *    the true half-bandwidths. Also, mukeep, mlkeep, mudq, and
 *    mldq need not be the same on every processor.
 *
 * 3) The actual name of the user's res function is passed to
 *    IDAMalloc, and the names of the user's Gres and Gcomm
 *    functions are passed to IDABBDPrecAlloc.        
 *
 * 4) The pointer to the user-defined data block res_data, which
 *    is set through IDASetRdata is also available to the user
 *    in glocal and gcomm.
 *
 * 5) Optional outputs specific to this module are available by
 *    way of routines listed below. These include work space sizes
 *    and the cumulative number of glocal calls. The costs
 *    associated with this module also include nsetups banded LU
 *    factorizations, nsetups gcomm calls, and nps banded
 *    backsolve calls, where nsetups and nps are integrator
 *    optional outputs.
 * -----------------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------
 * Type : IDABBDLocalFn
 * -----------------------------------------------------------------
 * The user must supply a function G(t,y,y') which approximates
 * the function F for the system F(t,y,y') = 0, and which is
 * computed locally (without interprocess communication).
 * (The case where G is mathematically identical to F is allowed.)
 * The implementation of this function must have type IDABBDLocalFn.
 *
 * This function takes as input the independent variable value tt,
 * the current solution vector yy, the current solution
 * derivative vector yp, and a pointer to the user-defined data
 * block res_data. It is to compute the local part of G(t,y,y')
 * and store it in the vector gval. (Providing memory for yy and
 * gval is handled within this preconditioner module.) It is
 * expected that this routine will save communicated data in work
 * space defined by the user, and made available to the
 * preconditioner function for the problem. The res_data
 * parameter is the same as that passed by the user to the
 * IDAMalloc routine.
 *
 * An IDABBDLocalFn Gres is to return an int, defined in the same
 * way as for the residual function: 0 (success), +1 or -1 (fail).
 * -----------------------------------------------------------------
 */

typedef int (*IDABBDLocalFn)(int Nlocal, realtype tt,
			     N_Vector yy, N_Vector yp, N_Vector gval,
			     void *res_data);

/*
 * -----------------------------------------------------------------
 * Type : IDABBDCommFn
 * -----------------------------------------------------------------
 * The user may supply a function of type IDABBDCommFn which
 * performs all interprocess communication necessary to
 * evaluate the approximate system function described above.
 *
 * This function takes as input the solution vectors yy and yp,
 * and a pointer to the user-defined data block res_data. The
 * res_data parameter is the same as that passed by the user to
 * the IDAMalloc routine.
 *
 * The IDABBDCommFn Gcomm is expected to save communicated data in
 * space defined with the structure *res_data.
 *
 * A IDABBDCommFn Gcomm returns an int value equal to 0 (success),
 * > 0 (recoverable error), or < 0 (unrecoverable error).
 *
 * Each call to the IDABBDCommFn is preceded by a call to the system
 * function res with the same vectors yy and yp. Thus the
 * IDABBDCommFn gcomm can omit any communications done by res if
 * relevant to the evaluation of the local function glocal.
 * A NULL communication function can be passed to IDABBDPrecAlloc
 * if all necessary communication was done by res.
 * -----------------------------------------------------------------
 */

typedef int (*IDABBDCommFn)(int Nlocal, realtype tt,
			    N_Vector yy, N_Vector yp,
			    void *res_data);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDPrecAlloc
 * -----------------------------------------------------------------
 * IDABBDPrecAlloc allocates and initializes an IBBDPrecData
 * structure to be passed to IDASp* (and used by IDABBDPrecSetup
 * and IDABBDPrecSol).
 *
 * The parameters of IDABBDPrecAlloc are as follows:
 *
 * ida_mem  is a pointer to the memory blockreturned by IDACreate.
 *
 * Nlocal  is the length of the local block of the vectors yy etc.
 *         on the current processor.
 *
 * mudq, mldq  are the upper and lower half-bandwidths to be used
 *             in the computation of the local Jacobian blocks.
 *
 * mukeep, mlkeep are the upper and lower half-bandwidths to be
 *                used in saving the Jacobian elements in the local
 *                block of the preconditioner matrix PP.
 *
 * dq_rel_yy is an optional input. It is the relative increment
 *           to be used in the difference quotient routine for
 *           Jacobian calculation in the preconditioner. The
 *           default is sqrt(unit roundoff), and specified by
 *           passing dq_rel_yy = 0.
 *
 * Gres    is the name of the user-supplied function G(t,y,y')
 *         that approximates F and whose local Jacobian blocks
 *         are to form the preconditioner.
 *
 * Gcomm   is the name of the user-defined function that performs
 *         necessary interprocess communication for the
 *         execution of glocal.
 *
 * IDABBDPrecAlloc returns the storage allocated (type *void),
 * or NULL if the request for storage cannot be satisfied.
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT void *IDABBDPrecAlloc(void *ida_mem, int Nlocal,
				      int mudq, int mldq,
				      int mukeep, int mlkeep,
				      realtype dq_rel_yy,
				      IDABBDLocalFn Gres, IDABBDCommFn Gcomm);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDSptfqmr
 * -----------------------------------------------------------------
 * IDABBDSptfqmr links the IDABBDPRE preconditioner to the IDASPTFQMR
 * linear solver. It performs the following actions:
 *  1) Calls the IDASPTFQMR specification routine and attaches the
 *     IDASPTFQMR linear solver to the IDA solver;
 *  2) Sets the preconditioner data structure for IDASPTFQMR
 *  3) Sets the preconditioner setup routine for IDASPTFQMR
 *  4) Sets the preconditioner solve routine for IDASPTFQMR
 *
 * Its first 2 arguments are the same as for IDASptfqmr (see
 * idasptfqmr.h). The last argument is the pointer to the IDABBDPRE
 * memory block returned by IDABBDPrecAlloc. Note that the user need
 * not call IDASptfqmr anymore.
 *
 * Possible return values are:
 *    IDASPTFQMR_SUCCESS    if successful
 *    IDASPTFQMR_MEM_NULL   if the IDAS memory was NULL
 *    IDASPTFQMR_MEM_FAIL   if there was a memory allocation failure
 *    IDASPTFQMR_ILL_INPUT  if there was illegal input
 *    IDABBDPRE_PDATA_NULL  if bbd_data was NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDSptfqmr(void *ida_mem, int maxl, void *bbd_data);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDSpbcg
 * -----------------------------------------------------------------
 * IDABBDSpbcg links the IDABBDPRE preconditioner to the IDASPBCG
 * linear solver. It performs the following actions:
 *  1) Calls the IDASPBCG specification routine and attaches the
 *     IDASPBCG linear solver to the IDA solver;
 *  2) Sets the preconditioner data structure for IDASPBCG
 *  3) Sets the preconditioner setup routine for IDASPBCG
 *  4) Sets the preconditioner solve routine for IDASPBCG
 *
 * Its first 2 arguments are the same as for IDASpbcg (see
 * idaspbcg.h). The last argument is the pointer to the IDABBDPRE
 * memory block returned by IDABBDPrecAlloc. Note that the user need
 * not call IDASpbcg anymore.
 *
 * Possible return values are:
 *    IDASPBCG_SUCCESS      if successful
 *    IDASPBCG_MEM_NULL     if the IDAS memory was NULL
 *    IDASPBCG_MEM_FAIL     if there was a memory allocation failure
 *    IDASPBCG_ILL_INPUT    if there was illegal input
 *    IDABBDPRE_PDATA_NULL  if bbd_data was NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDSpbcg(void *ida_mem, int maxl, void *bbd_data);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDSpgmr
 * -----------------------------------------------------------------
 * IDABBDSpgmr links the IDABBDPRE preconditioner to the IDASPGMR
 * linear solver. It performs the following actions:
 *  1) Calls the IDASPGMR specification routine and attaches the
 *     IDASPGMR linear solver to the IDA solver;
 *  2) Sets the preconditioner data structure for IDASPGMR
 *  3) Sets the preconditioner setup routine for IDASPGMR
 *  4) Sets the preconditioner solve routine for IDASPGMR
 *
 * Its first 2 arguments are the same as for IDASpgmr (see
 * idaspgmr.h). The last argument is the pointer to the IDABBDPRE
 * memory block returned by IDABBDPrecAlloc. Note that the user need
 * not call IDASpgmr anymore.
 *
 * Possible return values are:
 *    IDASPGMR_SUCCESS      if successful
 *    IDASPGMR_MEM_NULL     if the IDAS memory was NULL
 *    IDASPGMR_MEM_FAIL     if there was a memory allocation failure
 *    IDASPGMR_ILL_INPUT    if there was illegal input
 *    IDABBDPRE_PDATA_NULL  if bbd_data was NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDSpgmr(void *ida_mem, int maxl, void *bbd_data);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDPrecReInit
 * -----------------------------------------------------------------
 * IDABBDPrecReInit reinitializes the IDABBDPRE module when
 * solving a sequence of problems of the same size with
 * IDASPGMR/IDABBDPRE, IDASPBCG/IDABBDPRE, or IDASPTFQMR/IDABBDPRE
 * provided there is no change in Nlocal, mukeep, or mlkeep. After
 * solving one problem, and after calling IDAReInit to reinitialize
 * the integrator for a subsequent problem, call IDABBDPrecReInit.
 *
 * The first argument to IDABBDPrecReInit must be the pointer
 * bbd_data that was returned by IDABBDPrecAlloc. All other
 * arguments have the same names and meanings as those of
 * IDABBDPrecAlloc.
 *
 * The return value of IDABBDPrecReInit is IDABBDPRE_SUCCESS, indicating
 * success, or IDABBDPRE_PDATA_NULL if bbd_data was NULL.
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDPrecReInit(void *bbd_data,
				     int mudq, int mldq,
				     realtype dq_rel_yy);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDPrecFree
 * -----------------------------------------------------------------
 * IDABBDPrecFree frees the memory block bbd_data allocated by the
 * call to IDABBDPrecAlloc.
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT void IDABBDPrecFree(void **bbd_data);

/*
 * -----------------------------------------------------------------
 * Optional outputs for IDABBDPRE
 * -----------------------------------------------------------------
 * IDABBDPrecGetWorkSpace returns the real and integer work space
 *                        for IDABBDPRE.
 * IDABBDPrecGetNumGfnEvals returns the number of calls to the
 *                          user Gres function.
 * 
 * The return value of IDABBDPrecGet* is one of:
 *    IDABBDPRE_SUCCESS    if successful
 *    IDABBDPRE_PDATA_NULL if the bbd_data memory was NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDPrecGetWorkSpace(void *bbd_data, long int *lenrwBBDP, long int *leniwBBDP);
SUNDIALS_EXPORT int IDABBDPrecGetNumGfnEvals(void *bbd_data, long int *ngevalsBBDP);

/*
 * -----------------------------------------------------------------
 * The following function returns the name of the constant 
 * associated with an IDABBDPRE return flag
 * -----------------------------------------------------------------
 */
  
SUNDIALS_EXPORT char *IDABBDPrecGetReturnFlagName(int flag);

/* 
 * =================================================================
 * PART II - backward problems
 * =================================================================
 */

/*
 * -----------------------------------------------------------------
 * Types: IDALocalFnB and IDACommFnB
 * -----------------------------------------------------------------
 * Local approximation function and inter-process communication
 * function for the BBD preconditioner on the backward phase.
 * -----------------------------------------------------------------
 */
typedef int (*IDABBDLocalFnB)(int NlocalB, realtype tt,
			      N_Vector yy, N_Vector yp, 
			      N_Vector yyB, N_Vector ypB, N_Vector gvalB,
			      void *res_dataB);

typedef int (*IDABBDCommFnB)(int NlocalB, realtype tt,
			     N_Vector yy, N_Vector yp,
			     N_Vector yyB, N_Vector ypB,
			     void *res_dataB);

/*
 * -----------------------------------------------------------------
 * Functions: IDABBDPrecAllocB, IDABBDSp*B, IDABBDPrecReInit
 * -----------------------------------------------------------------
 * Interface functions for the IDABBDPRE preconditioner to be used on
 * the backward phase.
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDPrecAllocB(void *idaadj_mem, int NlocalB,
				     int mudqB, int mldqB,
				     int mukeepB, int mlkeepB,
				     realtype dq_rel_yyB,
				     IDABBDLocalFnB GresB, IDABBDCommFnB GcommB);

SUNDIALS_EXPORT int IDABBDSptfqmrB(void *idaadj_mem, int maxlB);
SUNDIALS_EXPORT int IDABBDSpbcgB(void *idaadj_mem, int maxlB);
SUNDIALS_EXPORT int IDABBDSpgmrB(void *idaadj_mem, int maxlB);
  
SUNDIALS_EXPORT int IDABBDPrecReInitB(void *idaadj_mem, int mudqB, int mldqB,
				      realtype dq_rel_yyB);

SUNDIALS_EXPORT void IDABBDPrecFreeB(void *idaadj_mem);

#ifdef __cplusplus
}
#endif

#endif
