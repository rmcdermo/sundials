/*******************************************************************
 *                                                                 *
 * File          : cvodes.c                                        *
 * Programmers   : Scott D. Cohen, Alan C. Hindmarsh, Radu Serban  *
 *                 and Dan Shumaker @ LLNL                         *
 * Version of    : 07 July 2003                                    *
 *-----------------------------------------------------------------*
 * Copyright (c) 2002, The Regents of the University of California * 
 * Produced at the Lawrence Livermore National Laboratory          *
 * All rights reserved                                             *
 * For details, see sundials/cvodes/LICENSE                        *
 *-----------------------------------------------------------------*
 * This is the implementation file for the main CVODES integrator  *
 * with sensitivity analysis capabilities.                         *
 * It is independent of the CVODES linear solver in use.           *
 *                                                                 *
 *******************************************************************/


/*=================================================================*/
/*BEGIN        Import Header Files                                 */
/*=================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include "cvodes.h"
#include "sundialstypes.h"
#include "nvector.h"
#include "sundialsmath.h"

/*=================================================================*/
/*END          Import Header Files                                 */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        Macros                                              */
/*=================================================================*/

/* Macro: loop */
#define loop for(;;)

/*=================================================================*/
/*END          Macros                                              */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        CVODES Private Constants                            */
/*=================================================================*/

#define FOURTH RCONST(0.25)    /* real 0.25    */
#define THREE  RCONST(3.0)     /* real 3.0     */
#define FOUR   RCONST(4.0)     /* real 4.0     */
#define HUN    RCONST(100.0)   /* real 100.0   */
#define TINY   RCONST(1.0e-10) /* small number */
#define HALF   RCONST(0.5)     /* real 0.5     */
#define ZERO   RCONST(0.0)     /* real 0.0     */
#define ONE    RCONST(1.0)     /* real 1.0     */
#define TWO    RCONST(2.0)     /* real 2.0     */
#define TWELVE RCONST(12.0)    /* real 12.0    */

/*=================================================================*/
/*END          CVODES Private Constants                            */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        CVODES Default Constants                            */
/*=================================================================*/

#define HMIN_DEFAULT     ZERO    /* hmin default value     */
#define HMAX_INV_DEFAULT ZERO    /* hmax_inv default value */
#define MXHNIL_DEFAULT   10      /* mxhnil default value   */
#define MXSTEP_DEFAULT   500     /* mxstep default value   */

/*=================================================================*/
/*END        CVODES Default Constants                            */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        CVODES Routine-Specific Constants                   */
/*=================================================================*/

/* CVodeGetDky and CVStep */

#define FUZZ_FACTOR RCONST(100.0)

/* CVHin */

#define HLB_FACTOR RCONST(100.0)
#define HUB_FACTOR RCONST(0.1)
#define H_BIAS     HALF
#define MAX_ITERS  4

/* CVSet */

#define CORTES RCONST(0.1)

/* CVStep return values */

#define SUCCESS_STEP      0
#define REP_ERR_FAIL     -1
#define REP_CONV_FAIL    -2
#define SETUP_FAILED     -3
#define SOLVE_FAILED     -4

/* CVStep control constants */

#define PREDICT_AGAIN    -5
#define DO_ERROR_TEST     1

#define STG_OKAY          2

/* CVStep */

#define THRESH RCONST(1.5)
#define ETAMX1 RCONST(10000.0) 
#define ETAMX2 RCONST(10.0)
#define ETAMX3 RCONST(10.0)
#define ETAMXF RCONST(0.2)
#define ETAMIN RCONST(0.1)
#define ETACF  RCONST(0.25)
#define ADDON  RCONST(0.000001)
#define BIAS1  RCONST(6.0)
#define BIAS2  RCONST(6.0)
#define BIAS3  RCONST(10.0)
#define ONEPSM RCONST(1.000001)

#define SMALL_NST    10   /* nst > SMALL_NST => use ETAMX3          */
#define MXNCF        10   /* max no. of convergence failures during
                             one step try                           */
#define MXNEF         7   /* max no. of error test failures during
                             one step try                           */
#define MXNEF1        3   /* max no. of error test failures before
                             forcing a reduction of order           */
#define SMALL_NEF     2   /* if an error failure occurs and
                             SMALL_NEF <= nef <= MXNEF1, then
                             reset eta =  MIN(eta, ETAMXF)          */
#define LONG_WAIT    10   /* number of steps to wait before
                             considering an order change when
                             q==1 and MXNEF1 error test failures
                             have occurred                          */

/* CVNls return values */

#define SOLVED            0
#define CONV_FAIL        -1 
#define SETUP_FAIL_UNREC -2
#define SOLVE_FAIL_UNREC -3

/* CVNls input flags */

#define FIRST_CALL      0
#define PREV_CONV_FAIL -1
#define PREV_ERR_FAIL  -2

/* CVNls other constants */

#define NLS_MAXCOR 3       /* maximum no. of corrector iterations for the
                              nonlinear solver                             */
#define CRDOWN RCONST(0.3) /* constant used in the estimation of the
                              convergence rate (crate) of the
                              iterates for the nonlinear equation          */
#define DGMAX  RCONST(0.3) /* iter == NEWTON, |gamma/gammap-1| > DGMAX
                              => call lsetup                               */

#define RDIV      TWO      /* declare divergence if ratio del/delp > RDIV  */
#define MSBP       20      /* max no. of steps between lsetup calls        */

#define TRY_AGAIN  99      /* control constant for CVNlsNewton - should be
                              distinct from CVNls return values            */

/* CVSensRhs1DQ finite difference methods */

#define CENTERED1  0
#define CENTERED2  1
#define FORWARD1   2
#define FORWARD2   3

/*=================================================================*/
/*END          CVODES Routine-Specific Constants                   */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        CVODES Error Messages                               */
/*=================================================================*/

/* CvodeCreate Error Messages             */

#define CVC                "CVodeCreate-- "

#define MSG_BAD_LMM1       CVC "lmm=%d illegal.\n"
#define MSG_BAD_LMM2       "The legal values are ADAMS=%d and BDF=%d.\n\n"
#define MSG_BAD_LMM        MSG_BAD_LMM1 MSG_BAD_LMM2

#define MSG_BAD_ITER1      CVC "iter=%d illegal.\n"
#define MSG_BAD_ITER2      "The legal values are FUNCTIONAL=%d "
#define MSG_BAD_ITER3      "and NEWTON=%d.\n\n"
#define MSG_BAD_ITER       MSG_BAD_ITER1 MSG_BAD_ITER2 MSG_BAD_ITER3

#define MSG_CVMEM_FAIL      CVC "Allocation of cv_mem failed. \n\n"

/* CVodeSet* Error Messages               */

#define MSG_CVS_NO_MEM      "cvode_mem=NULL in a CVodeSet routine illegal. \n\n"

#define MSG_CVS_BAD_ITER1    "CVodeResetIterType-- iter=%d illegal.\n"
#define MSG_CVS_BAD_ITER2    "The legal values are FUNCTIONAL=%d "
#define MSG_CVS_BAD_ITER3    "and NEWTON=%d.\n\n"
#define MSG_CVS_BAD_ITER      MSG_CVS_BAD_ITER1 MSG_CVS_BAD_ITER2 MSG_CVS_BAD_ITER3

#define MSG_CVS_NEG_MAXORD  "CVodeSetMaxOrd-- maxord<=0 illegal. \n\n"

#define MSG_CVS_BAD_MAXORD1 "CVodeSetMaxOrd-- Illegal attempt to increase "
#define MSG_CVS_BAD_MAXORD2 "maximum method order from %d to %d.\n\n"
#define MSG_CVS_BAD_MAXORD  MSG_CVS_BAD_MAXORD1 MSG_CVS_BAD_MAXORD2 

#define MSG_CVS_NEG_MXSTEPS "CVodeSetMaxNumSteps-- mxsteps<=0 illegal. \n\n"

#define MSG_CVS_SLDET1      "CVodeSetStabLimDet-- Attempt to use stability "
#define MSG_CVS_SLDET2      "limit detection with the ADAMS method illegal. \n\n"
#define MSG_CVS_SLDET       MSG_CVS_SLDET1 MSG_CVS_SLDET2

#define MSG_CVS_NEG_HMIN    "CVodeSetMinStep-- hmin<=0 illegal. \n\n"

#define MSG_CVS_NEG_HMAX    "CVodeSetMaxStep-- hmax<=0 illegal. \n\n"

#define MSG_CVS_BAD_HMM1    "CVodeSetMinStep/CVodeSetMaxStep-- Inconsistent \n"
#define MSG_CVS_BAD_HMM2    "step size limits: hmin=%g > hmax=%g.\n\n"
#define MSG_CVS_BAD_HMIN_HMAX  MSG_CVS_BAD_HMM1 MSG_CVS_BAD_HMM2

#define MSG_CVS_BAD_ECONQ1  "CVodeSetQuadErrCon-- errconQ=%d illegal.\n"
#define MSG_CVS_BAD_ECONQ2  "The legal values are FULL=%d and PARTIAL=%d.\n\n"
#define MSG_CVS_BAD_ECONQ   MSG_CVS_BAD_ECONQ1 MSG_CVS_BAD_ECONQ2

#define MSG_CVS_BAD_ECON1   "CVodeSetSensErrCon-- errcon=%d illegal.\n"
#define MSG_CVS_BAD_ECON2   "The legal values are FULL=%d and PARTIAL=%d.\n\n"
#define MSG_CVS_BAD_ECON    MSG_CVS_BAD_ECON1 MSG_CVS_BAD_ECON2

/* CVodeMalloc/CVodeReInit Error Messages */

#define CVM                 "CVodeMalloc/CVodeReInit-- "

#define MSG_CVM_NO_MEM      CVM "cvode_mem = NULL illegal.\n\n"

#define MSG_Y0_NULL         CVM "y0=NULL illegal.\n\n"

#define MSG_BAD_ITOL1       CVM "itol=%d illegal.\n"
#define MSG_BAD_ITOL2       "The legal values are SS=%d and SV=%d.\n\n"
#define MSG_BAD_ITOL        MSG_BAD_ITOL1 MSG_BAD_ITOL2

#define MSG_F_NULL          CVM "f=NULL illegal.\n\n"

#define MSG_RELTOL_NULL     CVM "reltol=NULL illegal.\n\n"
 
#define MSG_BAD_RELTOL      CVM "*reltol=%g < 0 illegal.\n\n"

#define MSG_ABSTOL_NULL     CVM "abstol=NULL illegal.\n\n"

#define MSG_BAD_ABSTOL      CVM "Some abstol component < 0.0 illegal.\n\n"

#define MSG_MEM_FAIL        CVM "A memory request failed.\n\n"

#define MSG_BAD_EWT         CVM "Some initial ewt component = 0.0 illegal.\n\n"

#define MSG_CVREI_NO_MALLOC "CVodeReInit-- Attempt to call before CVodeMalloc. \n\n"

/* CVodeQuadMalloc/ CVodeQuadReInit error messages */

#define QCVM              "CVodeQuadMalloc/CVodeQuadReInit-- "

#define MSG_QCVM_NO_MEM   QCVM "cvode_mem=NULL illegal.\n\n"

#define MSG_QCVM_MEM_FAIL QCVM "A memory request failed.\n\n"

#define MSG_BAD_ITOLQ1    QCVM "itolQ=%d illegal.\n"
#define MSG_BAD_ITOLQ2    "The legal values are SS=%d and SV=%d.\n\n"
#define MSG_BAD_ITOLQ     MSG_BAD_ITOLQ1 MSG_BAD_ITOLQ2

#define MSG_BAD_RELTOLQ   QCVM "*reltolQ=%g < 0.0 illegal.\n\n"

#define MSG_BAD_ABSTOLQ   QCVM "Some abstolQ component < 0.0 illegal.\n\n"  

#define MSG_BAD_EWTQ      QCVM "Some initial ewtQ component = 0.0 illegal.\n\n"

#define MSG_QREI_QUAD1    "CVodeQuadReInit-- Illegal attempt to call before "
#define MSG_QREI_QUAD2    "calling CVodeQuadMalloc.\n\n"
#define MSG_QREI_NO_QUAD  MSG_QREI_QUAD1 MSG_QREI_QUAD2

/* CVodeSensMalloc/ CVodeSensReInit error messages */

#define SCVM            "CVodeSensMalloc/CVodeSensReInit-- "

#define MSG_SCVM_NO_MEM SCVM "cvode_mem=NULL illegal.\n\n"

#define MSG_SCVM_MEM_FAIL SCVM "A memory request failed.\n\n"

#define MSG_BAD_NS      SCVM "NS=%d<0 illegal.\n\n"

#define MSG_P_NULL      SCVM "p=NULL illegal.\n\n"

#define MSG_PBAR_NULL   SCVM "pbar is needed, but pbar=NULL illegal.\n\n"

#define MSG_YS0_NULL    SCVM "yS0=NULL illegal.\n\n"

#define MSG_BAD_RELTOLS SCVM "*reltolS=%g < 0.0 illegal.\n\n"

#define MSG_BAD_ABSTOLS SCVM "Some abstolS component < 0.0 illegal.\n\n"  

#define MSG_BAD_ISM1    SCVM "ism=%d illegal.\n"
#define MSG_BAD_ISM2    "The legal values are: "
#define MSG_BAD_ISM3    "SIMULTANEOUS=%d, STAGGERED=%d and STAGGERED1=%d.\n\n"
#define MSG_BAD_ISM     MSG_BAD_ISM1 MSG_BAD_ISM2 MSG_BAD_ISM3

#define MSG_BAD_ISM_IFS SCVM "Illegal combination ism=STAGGERED1 and ifS=ALLSENS.\n\n"

#define MSG_BAD_PBAR    SCVM "Some pbar component = 0.0 illegal.\n\n"

#define MSG_BAD_EWTS    SCVM "Some initial ewtS component = 0.0 illegal.\n\n"

#define MSG_SREI_SENSI1 "CVodeSensReInit-- Illegal attempt to call before "
#define MSG_SREI_SENSI2 "calling CVodeSensMalloc.\n\n"
#define MSG_SREI_NO_SENSI  MSG_SREI_SENSI1 MSG_SREI_SENSI2

/* CVode error messages */

#define CVODE               "CVode-- "

#define NO_MEM              "cvode_mem=NULL illegal.\n\n"

#define MSG_CVODE_NO_MEM    CVODE NO_MEM

#define MSG_CVODE_NO_MALLOC CVODE "CVodeMalloc has not been called yet.\n\n"
 
#define MSG_LINIT_NULL      CVODE "The linear solver's init routine is NULL.\n\n"

#define MSG_LSETUP_NULL     CVODE "The linear solver's setup routine is NULL.\n\n"

#define MSG_LSOLVE_NULL     CVODE "The linear solver's solve routine is NULL.\n\n"

#define MSG_LFREE_NULL      CVODE "The linear solver's free routine is NULL.\n\n"

#define MSG_LINIT_FAIL      CVODE "The linear solver's init routine failed.\n\n"

#define MSG_YOUT_NULL       CVODE "yout=NULL illegal.\n\n"

#define MSG_T_NULL          CVODE "t=NULL illegal.\n\n"

#define MSG_BAD_ITASK       CVODE "itask=%d illegal.\n"

#define MSG_NO_TSTOP1       CVODE "itask = NORMAL_TSTOP or itask = ONE_STEP_TSTOP "
#define MSG_NO_TSTOP2       "but tstop was not set.\n\n"
#define MSG_NO_TSTOP        MSG_NO_TSTOP1 MSG_NO_TSTOP2

#define MSG_BAD_H0          CVODE "h0=%g and tout-t0=%g inconsistent.\n\n"

#define MSG_BAD_TOUT_1      CVODE "Trouble interpolating at tout = %g.\n"
#define MSG_BAD_TOUT_2      "tout too far back in direction of integration.\n\n"
#define MSG_BAD_TOUT        MSG_BAD_TOUT_1 MSG_BAD_TOUT_2

#define MSG_MAX_STEPS_1     CVODE "At t=%g, mxstep=%d steps taken on "
#define MSG_MAX_STEPS_2     "this call before\nreaching tout=%g.\n\n"
#define MSG_MAX_STEPS       MSG_MAX_STEPS_1 MSG_MAX_STEPS_2

#define MSG_EWT_NOW_BAD_1   CVODE "At t=%g, "
#define MSG_EWT_NOW_BAD_2   "some ewt component has become <= 0.0.\n\n"
#define MSG_EWT_NOW_BAD     MSG_EWT_NOW_BAD_1 MSG_EWT_NOW_BAD_2

#define MSG_EWTS_NOW_BAD_1   CVODE "At t=%g, "
#define MSG_EWTS_NOW_BAD_2   "some ewtS component has become <= 0.0.\n\n"
#define MSG_EWTS_NOW_BAD     MSG_EWTS_NOW_BAD_1 MSG_EWTS_NOW_BAD_2

#define MSG_EWTQ_NOW_BAD_1  CVODE "At t=%g, "
#define MSG_EWTQ_NOW_BAD_2  "some ewtQ component has become <= 0.0.\n\n"
#define MSG_EWTQ_NOW_BAD    MSG_EWTQ_NOW_BAD_1 MSG_EWTQ_NOW_BAD_2

#define MSG_TOO_MUCH_ACC    CVODE "At t=%g, too much accuracy requested.\n\n"

#define MSG_HNIL_1          CVODE "Warning.. internal t=%g and step size h=%g\n"
#define MSG_HNIL_2          "are such that t + h == t on the next step.\n"
#define MSG_HNIL_3          "The solver will continue anyway.\n\n"
#define MSG_HNIL            MSG_HNIL_1 MSG_HNIL_2 MSG_HNIL_3

#define MSG_HNIL_DONE_1     CVODE "The above warning has been issued %d times "
#define MSG_HNIL_DONE_2     "and will not be\nissued again for this problem.\n\n"
#define MSG_HNIL_DONE       MSG_HNIL_DONE_1 MSG_HNIL_DONE_2

#define MSG_ERR_FAILS_1     CVODE "At t=%g and step size h=%g, the error test\n"
#define MSG_ERR_FAILS_2     "failed repeatedly or with |h| = hmin.\n\n"
#define MSG_ERR_FAILS       MSG_ERR_FAILS_1 MSG_ERR_FAILS_2

#define MSG_CONV_FAILS_1    CVODE "At t=%g and step size h=%g, the corrector\n"
#define MSG_CONV_FAILS_2    "convergence failed repeatedly or "
#define MSG_CONV_FAILS_3    "with |h| = hmin.\n\n"
#define MSG_CONV_FAILS      MSG_CONV_FAILS_1 MSG_CONV_FAILS_2 MSG_CONV_FAILS_3

#define MSG_SETUP_FAILED_1  CVODE "At t=%g, the setup routine failed in an "
#define MSG_SETUP_FAILED_2  "unrecoverable manner.\n\n"
#define MSG_SETUP_FAILED    MSG_SETUP_FAILED_1 MSG_SETUP_FAILED_2

#define MSG_SOLVE_FAILED_1  CVODE "At t=%g, the solve routine failed in an "
#define MSG_SOLVE_FAILED_2  "unrecoverable manner.\n\n"
#define MSG_SOLVE_FAILED    MSG_SOLVE_FAILED_1 MSG_SOLVE_FAILED_2

#define MSG_TOO_CLOSE_1     CVODE "tout=%g too close to t0=%g to start"
#define MSG_TOO_CLOSE_2     " integration.\n\n"
#define MSG_TOO_CLOSE       MSG_TOO_CLOSE_1 MSG_TOO_CLOSE_2

#define MSG_BAD_TSTOP_1    CVODE "tstop = %g is behind  current t = %g \n"
#define MSG_BAD_TSTOP_2    "in the direction of integration.\n\n"
#define MSG_BAD_TSTOP      MSG_BAD_TSTOP_1 MSG_BAD_TSTOP_2

/* CVodeGetDky Error Messages */

#define DKY                 "CVodeGetDky-- "

#define MSG_DKY_NO_MEM      DKY NO_MEM

#define MSG_BAD_K           DKY "k=%d illegal.\n\n"

#define MSG_BAD_T1          DKY "t=%g illegal.\n"
#define MSG_BAD_T2          "t not in interval tcur-hu=%g to tcur=%g.\n\n"
#define MSG_BAD_T           MSG_BAD_T1 MSG_BAD_T2

#define MSG_BAD_DKY         DKY "dky=NULL illegal.\n\n"


/* CVodeGetSens, CVodeGetSensDkyAll, CVodeGetSensDky Error Messages */

#define SDKY                "CVodeGetSens/CVodeGetSensDkyAll/CVodeGetSensDky-- "

#define MSG_SDKY_NO_MEM     SDKY NO_MEM

#define MSG_SDKY_SENSI_1    "Illegal attempt to call before "
#define MSG_SDKY_SENSI_2    "calling CVodeSensMalloc.\n\n"
#define MSG_SDKY_NO_SENSI   SDKY MSG_SDKY_SENSI_1 MSG_SDKY_SENSI_2

#define MSG_SBAD_IS         SDKY "is=%d illegal.\n\n"

#define MSG_SBAD_K          SDKY "k=%d illegal.\n\n"

#define MSG_SBAD_T_1        SDKY "t=%g illegal.\n"
#define MSG_SBAD_T_2        "t not in interval tcur-hu=%g to tcur=%g.\n\n"
#define MSG_SBAD_T          MSG_SBAD_T_1 MSG_SBAD_T_2

#define MSG_SBAD_DKYA       SDKY "dkyA=NULL illegal.\n\n"
#define MSG_SBAD_DKY        SDKY "dky=NULL illegal.\n\n"

/* CVodeGetQuad, CVodeGetQuadDky Error Messages */

#define QDKY                "CVodeGetQuad/CVodeGetQuadDky-- "

#define MSG_QDKY_NO_MEM     QDKY NO_MEM

#define MSG_QDKY_QUAD_1     "Illegal attempt to call before "
#define MSG_QDKY_QUAD_2     "calling CVodeQuadMalloc.\n\n"
#define MSG_QDKY_NO_QUAD    QDKY MSG_QDKY_QUAD_1 MSG_QDKY_QUAD_2

#define MSG_QBAD_DKY        QDKY "dky=NULL illegal.\n\n"

#define MSG_QBAD_K          QDKY "k=%d illegal.\n\n"

#define MSG_QBAD_T_1        QDKY "t=%g illegal.\n"
#define MSG_QBAD_T_2        "t not in interval tcur-hu=%g to tcur=%g.\n\n"
#define MSG_QBAD_T          MSG_QBAD_T_1 MSG_QBAD_T_2

/* CVodeGet* Error Messages               */

#define MSG_CVG_NO_MEM      "cvode_mem=NULL in a CVodeGet routine illegal. \n\n"

#define MSG_CVG_NO_SLDET1   "CVodeGetNumStabLimOrderReds-- Illegal attempt "
#define MSG_CVG_NO_SLDET2   "to call without enabling SLDET.\n\n"
#define MSG_CVG_NO_SLDET    MSG_CVG_NO_SLDET1 MSG_CVG_NO_SLDET2

#define MSG_CVG_NO_QUAD1    "CVodeGetQuad*-- Illegal attempt to call before "
#define MSG_CVG_NO_QUAD2    "calling CVodeQuadMalloc.\n\n"
#define MSG_CVG_NO_QUAD     MSG_CVG_NO_QUAD1 MSG_CVG_NO_QUAD2

#define MSG_CVG_NO_SENSI1   "CVodeGetSens*-- Illegal attempt to call before "
#define MSG_CVG_NO_SENSI2   "calling CVodeSensMalloc.\n\n"
#define MSG_CVG_NO_SENSI    MSG_CVG_NO_SENSI1 MSG_CVG_NO_SENSI2

/*=================================================================*/
/*END          CVODES Error Messages                               */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        Private Helper Functions Prototypes                 */
/*=================================================================*/

static booleantype CVAllocVectors(CVodeMem cv_mem);
static void  CVFreeVectors(CVodeMem cv_mem);

static booleantype CVEwtSet(CVodeMem cv_mem, N_Vector ycur);
static booleantype CVEwtSetSS(CVodeMem cv_mem, N_Vector ycur);
static booleantype CVEwtSetSV(CVodeMem cv_mem, N_Vector ycur);

static booleantype CVHin(CVodeMem cv_mem, realtype tout);
static realtype  CVUpperBoundH0(CVodeMem cv_mem, realtype tdist);
static realtype  CVYddNorm(CVodeMem cv_mem, realtype hg);

static int   CVStep(CVodeMem cv_mem);

static int   CVsldet(CVodeMem cv_mem);

static void  CVAdjustParams(CVodeMem cv_mem);
static void  CVAdjustOrder(CVodeMem cv_mem, int deltaq);
static void  CVAdjustAdams(CVodeMem cv_mem, int deltaq);
static void  CVAdjustBDF(CVodeMem cv_mem, int deltaq);
static void  CVIncreaseBDF(CVodeMem cv_mem);
static void  CVDecreaseBDF(CVodeMem cv_mem);

static void  CVRescale(CVodeMem cv_mem);

static void  CVPredict(CVodeMem cv_mem);

static void  CVSet(CVodeMem cv_mem);
static void  CVSetAdams(CVodeMem cv_mem);
static realtype  CVAdamsStart(CVodeMem cv_mem, realtype m[]);
static void  CVAdamsFinish(CVodeMem cv_mem, realtype m[], realtype M[], realtype hsum);
static realtype  CVAltSum(int iend, realtype a[], int k);
static void  CVSetBDF(CVodeMem cv_mem);
static void  CVSetTqBDF(CVodeMem cv_mem, realtype hsum, realtype alpha0,
                        realtype alpha0_hat, realtype xi_inv, realtype xistar_inv);

static int   CVNls(CVodeMem cv_mem, int nflag);
static int   CVNlsFunctional(CVodeMem cv_mem);
static int   CVNlsNewton(CVodeMem cv_mem, int nflag);
static int   CVNewtonIteration(CVodeMem cv_mem);

static int   CVHandleNFlag(CVodeMem cv_mem, int *nflagPtr, realtype saved_t,
                           int *ncfPtr, int *ncfnPtr);

static void  CVRestore(CVodeMem cv_mem, realtype saved_t);

static booleantype CVDoErrorTest(CVodeMem cv_mem, int *nflagPtr, int *kflagPtr,
                                 realtype saved_t, int *nefPtr, realtype *dsmPtr);

static void  CVCompleteStep(CVodeMem cv_mem);

static void  CVPrepareNextStep(CVodeMem cv_mem, realtype dsm);
static void  CVSetEta(CVodeMem cv_mem);
static realtype  CVComputeEtaqm1(CVodeMem cv_mem);
static realtype  CVComputeEtaqp1(CVodeMem cv_mem);
static void  CVChooseEta(CVodeMem cv_mem);
static void  CVBDFStab(CVodeMem cv_mem);

static int   CVHandleFailure(CVodeMem cv_mem,int kflag);


/*----------------*/

static booleantype CVQuadAllocVectors(CVodeMem cv_mem);
static booleantype CVQuadEwtSet(CVodeMem cv_mem, N_Vector qcur);
static booleantype CVQuadEwtSetSS(CVodeMem cv_mem, N_Vector qcur);
static booleantype CVQuadEwtSetSV(CVodeMem cv_mem, N_Vector qcur);
static void CVQuadFreeVectors(CVodeMem cv_mem);

/*----------------*/

static booleantype CVQuadDoErrorTest(CVodeMem cv_mem, int *nflagPtr, 
                                     int *kflagPtr, realtype saved_t, 
                                     int *nefQPtr, realtype *dsmQPtr);

/*----------------*/

static realtype CVQuadUpdateNorm(CVodeMem cv_mem, realtype old_nrm,
                                 N_Vector xQ, N_Vector wQ);
static realtype  CVQuadUpdateDsm(CVodeMem cv_mem, realtype old_dsm, 
                                 realtype dsmQ);

/*----------------*/

static booleantype CVSensTestAtol(CVodeMem cv_mem, void *atolS);

static booleantype CVSensAllocAtol(CVodeMem cv_mem, void **atolSPtr);
static void  CVSensFreeAtol(CVodeMem cv_mem, void *atolS);

static booleantype CVSensSetAtol(CVodeMem cv_mem, void *atolS);
static booleantype CVSensSetAtolSS(CVodeMem cv_mem, realtype *atolS);
static booleantype CVSensSetAtolSV(CVodeMem cv_mem, N_Vector *atolS);

/*----------------*/

static booleantype CVSensAllocVectors(CVodeMem cv_mem);
static void  CVSensFreeVectors(CVodeMem cv_mem);

/*----------------*/

static booleantype CVSensEwtSet(CVodeMem cv_mem, N_Vector *yScur);
static booleantype CVSensEwtSetSS(CVodeMem cv_mem, N_Vector *yScur);
static booleantype CVSensEwtSetSV(CVodeMem cv_mem, N_Vector *yScur);

/*----------------*/

static int   CVStgrNls(CVodeMem cv_mem);
static int   CVStgrNlsFunctional(CVodeMem cv_mem);
static int   CVStgrNlsNewton(CVodeMem cv_mem);
static int   CVStgrNewtonIteration(CVodeMem cv_mem);
static int   CVStgr1Nls(CVodeMem cv_mem, int is);
static int   CVStgr1NlsFunctional(CVodeMem cv_mem, int is);
static int   CVStgr1NlsNewton(CVodeMem cv_mem, int is);
static int   CVStgr1NewtonIteration(CVodeMem cv_mem, int is);
static booleantype CVStgrDoErrorTest(CVodeMem cv_mem, int *nflagPtr, int *kflagPtr, 
                                     realtype saved_t, int *nefSPtr, realtype *dsmSPtr);

/*----------------*/

static realtype CVSensNorm(CVodeMem cv_mem, N_Vector *xS, N_Vector *wS);
static realtype CVSensUpdateNorm(CVodeMem cv_mem, realtype old_nrm,
                                 N_Vector *xS, N_Vector *wS);
static realtype  CVStgrUpdateDsm(CVodeMem cv_mem, realtype old_dsm, 
                                 realtype dsmS);

/*----------------*/

static void CVSensRhs(CVodeMem cv_mem, realtype time, 
                      N_Vector ycur, N_Vector fcur, 
                      N_Vector *yScur, N_Vector *fScur,
                      N_Vector temp1, N_Vector temp2);

static void CVSensRhs1(CVodeMem cv_mem, realtype time, 
                       N_Vector ycur, N_Vector fcur, 
                       int is, N_Vector yScur, N_Vector fScur,
                       N_Vector temp1, N_Vector temp2);

static void CVSensRhsDQ(int Ns, realtype t, 
                        N_Vector y, N_Vector ydot, 
                        N_Vector *yS, N_Vector *ySdot, 
                        void *fS_data,  
                        N_Vector tempv, N_Vector ftemp);

static void CVSensRhs1DQ(int Ns, realtype t, 
                         N_Vector y, N_Vector ydot, 
                         int is, N_Vector yS, N_Vector ySdot, 
                         void *fS_data,
                         N_Vector tempv, N_Vector ftemp);

/*=================================================================*/
/*END          Private Helper Functions Prototypes                 */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        EXPORTED FUNCTIONS IMPLEMENTATION                   */
/*=================================================================*/

/*------------------     CVodeCreate     --------------------------*/
/* 
   CVodeCreate creates an internal memory block for a problem to 
   be solved by CVODES.
   If successful, CVodeCreate returns a pointer to the problem memory. 
   This pointer should be passed to CVodeMalloc.  
   If an initialization error occurs, CVodeCreate prints an error 
   message to standard err and returns NULL. 
*/
/*-----------------------------------------------------------------*/

void *CVodeCreate(int lmm, int iter)
{
  int maxord;
  CVodeMem cv_mem;

  /* Test inputs */

  if ((lmm != ADAMS) && (lmm != BDF)) {
    fprintf(stdout, MSG_BAD_LMM, lmm, ADAMS, BDF);
    return (NULL);
  }
  
  if ((iter != FUNCTIONAL) && (iter != NEWTON)) {
    fprintf(stdout, MSG_BAD_ITER, iter, FUNCTIONAL, NEWTON);
    return (NULL);
  }

  cv_mem = (CVodeMem) malloc(sizeof(struct CVodeMemRec));
  if (cv_mem == NULL) {
    fprintf(stdout, MSG_CVMEM_FAIL);
    return (NULL);
  }

  maxord = (lmm == ADAMS) ? ADAMS_Q_MAX : BDF_Q_MAX;

  /* copy input parameters into cv_mem */
  cv_mem->cv_lmm    = lmm;
  cv_mem->cv_iter   = iter;

  /* Set uround */
  cv_mem->cv_uround = UnitRoundoff();

  /* Set default values for integrator optional inputs */
  cv_mem->cv_f_data   = NULL;
  cv_mem->cv_errfp    = stdout;
  cv_mem->cv_qmax     = maxord;
  cv_mem->cv_mxstep   = MXSTEP_DEFAULT;
  cv_mem->cv_mxhnil   = MXHNIL_DEFAULT;
  cv_mem->cv_sldeton  = FALSE;
  cv_mem->cv_hin      = ZERO;
  cv_mem->cv_hmin     = HMIN_DEFAULT;
  cv_mem->cv_hmax_inv = HMAX_INV_DEFAULT;
  cv_mem->cv_tstopset = FALSE;
  cv_mem->cv_maxcor   = NLS_MAXCOR;
  cv_mem->cv_maxnef   = MXNEF;
  cv_mem->cv_maxncf   = MXNCF;
  cv_mem->cv_nlscoef  = CORTES;

  /* Set default values for quad. optional inputs */
  cv_mem->cv_fQ_data  = NULL;
  cv_mem->cv_errconQ  = FULL;

  /* Set defaull values for sensi. optional inputs */
  cv_mem->cv_fS_data = (void *)cv_mem;
  cv_mem->cv_fS      = CVSensRhsDQ;
  cv_mem->cv_fS1     = CVSensRhs1DQ;
  cv_mem->cv_fSDQ    = TRUE;
  cv_mem->cv_ifS     = ONESENS;
  cv_mem->cv_errcon  = FULL;
  cv_mem->cv_rhomax  = ZERO;
  cv_mem->cv_pbar    = NULL;
  cv_mem->cv_reltolS = NULL;
  cv_mem->cv_abstolS = NULL;
  cv_mem->cv_maxcorS = NLS_MAXCOR;

  /* No mallocs have been done yet */
  cv_mem->cv_MallocDone     = FALSE;
  cv_mem->cv_quadMallocDone = FALSE;
  cv_mem->cv_sensMallocDone = FALSE;

  /* Return pointer to CVODES memory block */
  return((void *)cv_mem);
}

#define lmm    (cv_mem->cv_lmm) 

/*=================================================================*/
/*BEGIN        INTEGRATOR OPTIONAL INPUT FUNCTIONS                 */
/*=================================================================*/

int CVodeResetIterType(void *cvode_mem, int iter)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if ((iter != FUNCTIONAL) && (iter != NEWTON)) {
    fprintf(stdout, MSG_CVS_BAD_ITER, iter, FUNCTIONAL, NEWTON);
    return (CVS_ILL_INPUT);
  }

  cv_mem->cv_iter = iter;

  return(SUCCESS);
}

#define iter   (cv_mem->cv_iter)        

/*-----------------------------------------------------------------*/

int CVodeSetFdata(void *cvode_mem, void *f_data)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_f_data = f_data;

  return(SUCCESS);
}

#define f_data (cv_mem->cv_f_data)    

/*-----------------------------------------------------------------*/

int CVodeSetErrFile(void *cvode_mem, FILE *errfp)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_errfp = errfp;

  return(SUCCESS);
}

#define errfp (cv_mem->cv_errfp)

/*-----------------------------------------------------------------*/

int CVodeSetMaxOrd(void *cvode_mem, int maxord)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (maxord <= 0) {
    fprintf(errfp, MSG_CVS_NEG_MAXORD);
    return(CVS_ILL_INPUT);
  }
  
  if (maxord > cv_mem->cv_qmax) {
    fprintf(errfp, MSG_CVS_BAD_MAXORD, cv_mem->cv_qmax, maxord);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_qmax = maxord;

  return(SUCCESS);
}

#define qmax (cv_mem->cv_qmax) 

/*-----------------------------------------------------------------*/

int CVodeSetMaxNumSteps(void *cvode_mem, int mxsteps)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (mxsteps<=0) {
    fprintf(errfp, MSG_CVS_NEG_MXSTEPS);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_mxstep = mxsteps;

  return(SUCCESS);
}

#define mxstep (cv_mem->cv_mxstep)

/*-----------------------------------------------------------------*/

int CVodeSetMaxHnilWarns(void *cvode_mem, int mxhnil)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_mxhnil = mxhnil;

  return(SUCCESS);
}

#define mxhnil (cv_mem->cv_mxhnil)

/*-----------------------------------------------------------------*/

int CVodeSetStabLimDet(void *cvode_mem, booleantype sldet)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if(cv_mem->cv_lmm != BDF) {
    fprintf(errfp, MSG_CVS_SLDET);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_sldeton = sldet;

  return(SUCCESS);
}

#define sldeton (cv_mem->cv_sldeton)

/*-----------------------------------------------------------------*/

int CVodeSetInitStep(void *cvode_mem, realtype hin)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_hin = hin;

  return(SUCCESS);
}

#define hin (cv_mem->cv_hin)

/*-----------------------------------------------------------------*/

int CVodeSetMinStep(void *cvode_mem, realtype hmin)
{
  realtype hmax;
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (hmin<=0) {
    fprintf(errfp, MSG_CVS_NEG_HMIN);
    return(CVS_ILL_INPUT);
  }

  if (hmin * cv_mem->cv_hmax_inv > ONE) {
    hmax = ONE/cv_mem->cv_hmax_inv;
    fprintf(errfp, MSG_CVS_BAD_HMIN_HMAX, hmin, hmax);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_hmin = hmin;

  return(SUCCESS);
}

#define hmin (cv_mem->cv_hmin)

/*-----------------------------------------------------------------*/

int CVodeSetMaxStep(void *cvode_mem, realtype hmax)
{
  realtype hmax_inv;
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return (CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (hmax <= 0) {
    fprintf(errfp, MSG_CVS_NEG_HMAX);
    return(CVS_ILL_INPUT);
  }

  hmax_inv = ONE/hmax;
  if (hmax_inv * cv_mem->cv_hmin > ONE) {
    fprintf(errfp, MSG_CVS_BAD_HMIN_HMAX, cv_mem->cv_hmin, hmax);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_hmax_inv = hmax_inv;

  return(SUCCESS);
}

#define hmax_inv (cv_mem->cv_hmax_inv)

/*-----------------------------------------------------------------*/

int CVodeSetStopTime(void *cvode_mem, realtype tstop)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return (CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_tstop = tstop;
  cv_mem->cv_tstopset = TRUE;

  return(SUCCESS);
}

#define tstop    (cv_mem->cv_tstop)
#define tstopset (cv_mem->cv_tstopset)

/*------------------  CVodeSetMaxErrTestFails   -------------------*/
/* 
   Specifies the maximum number of error test failures during one
   step try.
*/
/*-----------------------------------------------------------------*/

int CVodeSetMaxErrTestFails(void *cvode_mem, int maxnef)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return (CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_maxnef = maxnef;

  return(SUCCESS);
}

#define maxnef (cv_mem->cv_maxnef)

/*------------------  CVodeSetMaxConvFails  -----------------------*/
/* 
   Specifies the maximum number of nonlinear convergence failures 
   during one step try.
*/
/*-----------------------------------------------------------------*/

int CVodeSetMaxConvFails(void *cvode_mem, int maxncf)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return (CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_maxncf = maxncf;

  return(SUCCESS);
}

#define maxncf (cv_mem->cv_maxncf)

/*------------------  CVodeSetMaxNonlinIters  ---------------------*/
/* 
   Specifies the maximum number of nonlinear iterations during
   one solve.
*/
/*-----------------------------------------------------------------*/

int CVodeSetMaxNonlinIters(void *cvode_mem, int maxcor)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return (CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_maxcor = maxcor;

  return(SUCCESS);
}

#define maxcor (cv_mem->cv_maxcor)

/*------------------ CVodeSetNonlinConvCoef -----------------------*/
/* 
   Specifies the coeficient in the nonlinear solver convergence
   test
*/
/*-----------------------------------------------------------------*/

int CVodeSetNonlinConvCoef(void *cvode_mem, realtype nlscoef)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_nlscoef = nlscoef;

  return(SUCCESS);
}

#define nlscoef (cv_mem->cv_nlscoef)

/*=================================================================*/
/*END        INTEGRATOR OPTIONAL INPUT FUNCTIONS                   */
/*=================================================================*/

/*------------------     CVodeMalloc     --------------------------*/
/* 
   CVodeMalloc allocates and initializes memory for a problem. All 
   problem inputs are checked for errors. If any error occurs during 
   initialization, it is reported to the file whose file pointer is 
   errfp and an error flag is returned. Otherwise, it returns SUCCESS
*/
/*-----------------------------------------------------------------*/

int CVodeMalloc(void *cvode_mem, RhsFn f, realtype t0, N_Vector y0, 
                int itol, realtype *reltol, void *abstol, NV_Spec nvspec)
{
  CVodeMem cv_mem;
  booleantype allocOK, neg_abstol, ewtsetOK;
  long int lrw1, liw1;
  int i,k;
  
  /* Check cvode_mem */
  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVM_NO_MEM);
    return(CVM_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  /* Check for legal input parameters */

  if (y0==NULL) {
    fprintf(errfp, MSG_Y0_NULL);
    return(CVM_ILL_INPUT);
  }

  if ((itol != SS) && (itol != SV)) {
    fprintf(errfp, MSG_BAD_ITOL, itol, SS, SV);
    return(CVM_ILL_INPUT);
  }

  if (f == NULL) {
    fprintf(errfp, MSG_F_NULL);
    return(CVM_ILL_INPUT);
  }

  if (reltol == NULL) {
    fprintf(errfp, MSG_RELTOL_NULL);
    return(CVM_ILL_INPUT);
  }

  if (*reltol < ZERO) {
    fprintf(errfp, MSG_BAD_RELTOL, *reltol);
    return(CVM_ILL_INPUT);
  }
   
  if (abstol == NULL) {
    fprintf(errfp, MSG_ABSTOL_NULL);
    return(CVM_ILL_INPUT);
  }

  if (itol == SS) {
    neg_abstol = (*((realtype *)abstol) < ZERO);
  } else {
    neg_abstol = (N_VMin((N_Vector)abstol) < ZERO);
  }
  if (neg_abstol) {
    fprintf(errfp, MSG_BAD_ABSTOL);
    return(CVM_ILL_INPUT);
  }

  /* Set space requirements for one N_Vector */
  N_VSpace(nvspec, &lrw1, &liw1);
  cv_mem->cv_lrw1 = lrw1;
  cv_mem->cv_liw1 = liw1;

  /* Allocate the vectors */
  cv_mem->cv_nvspec = nvspec;
  allocOK = CVAllocVectors(cv_mem);
  if (!allocOK) {
    fprintf(errfp, MSG_MEM_FAIL);
    return(CVM_MEM_FAIL);
  }
 
  /* Copy tolerances into memory and set the ewt vector */
  cv_mem->cv_itol   = itol;
  cv_mem->cv_reltol = reltol;      
  cv_mem->cv_abstol = abstol;
  ewtsetOK = CVEwtSet(cv_mem, y0);
  if (!ewtsetOK) {
    fprintf(errfp, MSG_BAD_EWT);
    CVFreeVectors(cv_mem);
    return(CVM_ILL_INPUT);
  }
  
  /* All error checking is complete at this point */

  /* Copy the input parameters into CVODES state */
  cv_mem->cv_f  = f;
  cv_mem->cv_tn = t0;

  /* Set step parameters */
  cv_mem->cv_q      = 1;
  cv_mem->cv_L      = 2;
  cv_mem->cv_qwait  = cv_mem->cv_L;
  cv_mem->cv_etamax = ETAMX1;

  cv_mem->cv_qu = 0;
  cv_mem->cv_hu = ZERO;
  cv_mem->cv_tolsf = ONE;

  /* Set the linear solver addresses to NULL.
     (We check != NULL later, in CVode, if using NEWTON.) */
  cv_mem->cv_linit   = NULL;
  cv_mem->cv_lsetup  = NULL;
  cv_mem->cv_lsolve  = NULL;
  cv_mem->cv_lfree   = NULL;
  cv_mem->cv_lmem    = NULL;

  /* Set forceSetup to FALSE */
  cv_mem->cv_forceSetup = FALSE;

  /* Initialize zn[0] in the history array */
  N_VScale(ONE, y0, cv_mem->cv_zn[0]);
 
  /* Initialize all the counters */
  cv_mem->cv_nst     = 0;
  cv_mem->cv_nfe     = 0;
  cv_mem->cv_ncfn    = 0;
  cv_mem->cv_netf    = 0;
  cv_mem->cv_nni     = 0;
  cv_mem->cv_nsetups = 0;
  cv_mem->cv_nhnil   = 0;
  cv_mem->cv_nstlp   = 0;
  cv_mem->cv_nscon   = 0;

  /* Initialize Stablilty Limit Detection data */
  /* NOTE: We do this even if stab lim det was not
     turned on yet. This way, the user can turn it
     on at any time */
  cv_mem->cv_nor = 0;
  for (i = 1; i <= 5; i++)
    for (k = 1; k <= 3; k++) 
      cv_mem->cv_ssdat[i-1][k-1] = ZERO;
  
  /* Initialize quadrature data; default is NO quadratures */
  cv_mem->cv_quad  = FALSE;

  /* Initialize sensitivity data; default is NO sensitivity */
  cv_mem->cv_sensi  = FALSE;

  /* Problem has been successfully initialized */
  cv_mem->cv_MallocDone = TRUE;
  return(SUCCESS);
}

/*------------------     CVodeReInit     --------------------------*/
/*
  CVodeReInit re-initializes CVODES' memory for a problem, assuming
  it has already been allocated in a prior CVodeMalloc call.
  All problem specification inputs are checked for errors.
  If any error occurs during initialization, it is reported to the
  file whose file pointer is errfp.
  The return value is SUCCESS = 0 if no errors occurred, or
  a negative value otherwise.
*/
/*-----------------------------------------------------------------*/

int CVodeReInit(void *cvode_mem, RhsFn f, realtype t0, N_Vector y0, 
                int itol, realtype *reltol, void *abstol)
{
  CVodeMem cv_mem;
  booleantype neg_abstol, ewtsetOK;
  int i,k;
 
  /* Check cvode_mem */
  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVM_NO_MEM);
    return(CVREI_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  /* Check if cvode_mem was allocated */
  if (cv_mem->cv_MallocDone == FALSE) {
    fprintf(errfp, MSG_CVREI_NO_MALLOC);
    return(CVREI_NO_MALLOC);
  }

  /* Check for legal input parameters */

  if (y0 == NULL) {
    fprintf(errfp, MSG_Y0_NULL);
    return (CVREI_ILL_INPUT);
  }
  
  if ((itol != SS) && (itol != SV)) {
    fprintf(errfp, MSG_BAD_ITOL, itol, SS, SV);
    return (CVREI_ILL_INPUT);
  }

  if (f == NULL) {
    fprintf(errfp, MSG_F_NULL);
    return (CVREI_ILL_INPUT);
  }

  if (reltol == NULL) {
    fprintf(errfp, MSG_RELTOL_NULL);
    return (CVREI_ILL_INPUT);
  }

  if (*reltol < ZERO) {
    fprintf(errfp, MSG_BAD_RELTOL, *reltol);
    return (CVREI_ILL_INPUT);
  }
   
  if (abstol == NULL) {
    fprintf(errfp, MSG_ABSTOL_NULL);
    return (CVREI_ILL_INPUT);
  }

  if (itol == SS) {
    neg_abstol = (*((realtype *)abstol) < ZERO);
  } else {
    neg_abstol = (N_VMin((N_Vector)abstol) < ZERO);
  }
  if (neg_abstol) {
    fprintf(errfp, MSG_BAD_ABSTOL);
    return (CVREI_ILL_INPUT);
  }

   /* Copy tolerances into memory and set the ewt vector */
  cv_mem->cv_itol   = itol;
  cv_mem->cv_reltol = reltol;      
  cv_mem->cv_abstol = abstol;
  ewtsetOK = CVEwtSet(cv_mem, y0);
  if (!ewtsetOK) {
    fprintf(errfp, MSG_BAD_EWT);
    return (CVREI_ILL_INPUT);
  }
  
  /* All error checking is complete at this point */
  
  /* Copy the input parameters into CVODE state */
  cv_mem->cv_f = f;
  cv_mem->cv_tn = t0;

  /* Set step parameters */
  cv_mem->cv_q      = 1;
  cv_mem->cv_L      = 2;
  cv_mem->cv_qwait  = cv_mem->cv_L;
  cv_mem->cv_etamax = ETAMX1;

  cv_mem->cv_qu     = 0;
  cv_mem->cv_hu     = ZERO;
  cv_mem->cv_tolsf  = ONE;

  /* Set forceSetup to FALSE */
  cv_mem->cv_forceSetup = FALSE;

  /* Initialize zn[0] in the history array */
  N_VScale(ONE, y0, cv_mem->cv_zn[0]);
 
  /* Initialize all the counters */
  cv_mem->cv_nst     = 0;
  cv_mem->cv_nfe     = 0;
  cv_mem->cv_ncfn    = 0;
  cv_mem->cv_netf    = 0;
  cv_mem->cv_nni     = 0;
  cv_mem->cv_nsetups = 0;
  cv_mem->cv_nhnil   = 0;
  cv_mem->cv_nstlp   = 0;
  cv_mem->cv_nscon   = 0;

  /* Initialize Stablilty Limit Detection data */
  cv_mem->cv_nor = 0;
  for (i = 1; i <= 5; i++)
    for (k = 1; k <= 3; k++) 
      cv_mem->cv_ssdat[i-1][k-1] = ZERO;
  
  /* Re-initialize sensitivity data; default is NO sensitivity */
  cv_mem->cv_sensi  = FALSE;
  
  /* Re-initialize quadrature data; default is NO quadrature */
  cv_mem->cv_quad  = FALSE;

  /* Problem has been successfully re-initialized */
  return(SUCCESS);
}

#define f      (cv_mem->cv_f)      
#define itol   (cv_mem->cv_itol)         
#define reltol (cv_mem->cv_reltol)       
#define abstol (cv_mem->cv_abstol)     
#define nvspec (cv_mem->cv_nvspec)

/*=================================================================*/
/*BEGIN        QUADRATURE OPTIONAL INPUT FUNCTIONS                 */
/*=================================================================*/

int CVodeSetQuadFdata(void *cvode_mem, void *fQ_data)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_fQ_data = fQ_data;

  return(SUCCESS);
}

#define fQ_data (cv_mem->cv_fQ_data)

/*-----------------------------------------------------------------*/

int CVodeSetQuadErrCon(void *cvode_mem, int errconQ)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if ((errconQ!=FULL) && (errconQ!=PARTIAL)) {
    fprintf(errfp, MSG_CVS_BAD_ECONQ, errconQ, FULL, PARTIAL);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_errconQ = errconQ;

  return(SUCCESS);
}

#define errconQ (cv_mem->cv_errconQ)

/*=================================================================*/
/*END        QUADRATURE OPTIONAL INPUT FUNCTIONS                   */
/*=================================================================*/

/*------------------   CVodeQuadMalloc   --------------------------*/
/*
  CVodeQuadMalloc allocates and initializes quadrature related 
  memory for a problem. All problem specification inputs are 
  checked for errors. If any error occurs during initialization, 
  it is reported to the file whose file pointer is errfp. 
  The return value is SUCCESS = 0 if no errors occurred, or
  a negative value otherwise.
*/
/*-----------------------------------------------------------------*/

int CVodeQuadMalloc(void *cvode_mem, QuadRhsFn fQ,
                    int itolQ, realtype *reltolQ, void *abstolQ,
                    NV_Spec nvspecQ)
{
  CVodeMem cv_mem;
  booleantype neg_abstol, allocOK, ewtsetOK;
  long int lrw1Q, liw1Q;

  /* Check cvode_mem */
  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_QCVM_NO_MEM);
    return(QCVM_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  /* Check for legal input parameters */

  if (errconQ == FULL) {
    if ((itolQ != SS) && (itolQ != SV)) {
      fprintf(errfp, MSG_BAD_ITOLQ, itolQ, SS, SV);
      return(QCVM_ILL_INPUT);
    }
    if (*reltolQ < ZERO) {
      fprintf(errfp, MSG_BAD_RELTOLQ, *reltolQ);
      return(QCVM_ILL_INPUT);
    }
    if (itolQ == SS) {
      neg_abstol = (*((realtype *)abstolQ) < ZERO);
    } else {
      neg_abstol = (N_VMin((N_Vector)abstolQ) < ZERO);
    }
    if (neg_abstol) {
      fprintf(errfp, MSG_BAD_ABSTOLQ);
      return(QCVM_ILL_INPUT);
    }
  }

  /* Set space requirements for one N_Vector */
  N_VSpace(nvspecQ, &lrw1Q, &liw1Q);
  cv_mem->cv_lrw1Q = lrw1Q;
  cv_mem->cv_liw1Q = liw1Q;

  /* Allocate the vectors */
  cv_mem->cv_nvspecQ = nvspecQ;
  allocOK = CVQuadAllocVectors(cv_mem);
  if (!allocOK) {
    fprintf(errfp, MSG_QCVM_MEM_FAIL);
    return(QCVM_MEM_FAIL);
  }

  /* Initialize znQ[0] in the history array */
  N_VConst(ZERO, cv_mem->cv_znQ[0]);

  /* Copy tolerances into memory and set the ewtQ vector */
  if (errconQ == FULL) {
    cv_mem->cv_itolQ    = itolQ;
    cv_mem->cv_reltolQ  = reltolQ;
    cv_mem->cv_abstolQ  = abstolQ;
    ewtsetOK = CVQuadEwtSet(cv_mem, cv_mem->cv_znQ[0]);
    if (!ewtsetOK) {
      fprintf(errfp, MSG_BAD_EWTQ);
      CVQuadFreeVectors(cv_mem);
      return (QCVM_ILL_INPUT);
    }
  }

  /* All error checking is complete at this point */

  /* Copy the input parameters into CVODES state */
  cv_mem->cv_fQ    = fQ;

  /* Initialize counters */
  cv_mem->cv_nfQe  = 0;
  cv_mem->cv_netfQ = 0;

  /* Quadrature integration turned ON */
  cv_mem->cv_quad = TRUE;
  cv_mem->cv_quadMallocDone = TRUE;

  /* Quadrature initialization was successfull */
  return(SUCCESS);
}

/*------------------  CVodeQuadReInit    --------------------------*/
/*
  CVodeQuadReInit re-initializes CVODES' quadrature related memory 
  for a problem, assuming it has already been allocated in prior 
  calls to CVodeMalloc and CvodeQuadMalloc. 
  All problem specification inputs are checked for errors.
  If any error occurs during initialization, it is reported to the
  file whose file pointer is errfp.
  The return value is SUCCESS = 0 if no errors occurred, or
  a negative value otherwise.
*/
/*-----------------------------------------------------------------*/

int CVodeQuadReInit(void *cvode_mem, QuadRhsFn fQ,
                    int itolQ, realtype *reltolQ, void *abstolQ)
{
  CVodeMem cv_mem;
  booleantype neg_abstol, ewtsetOK;

  /* Check cvode_mem */
  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_QCVM_NO_MEM);
    return(QCVM_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  /* Ckeck if quadrature was initialized? */
  if (cv_mem->cv_quadMallocDone == FALSE) {
    fprintf(errfp, MSG_QREI_NO_QUAD);
    return(QCVREI_NO_QUAD);
  }

  /* Check if reltolQ and abstolQ are non-null and legal */
  if (errconQ == FULL) {
    if ((itolQ != SS) && (itolQ != SV)) {
      fprintf(errfp, MSG_BAD_ITOLQ, itolQ, SS, SV);
      return(QCVM_ILL_INPUT);
    }
    if (*reltolQ < ZERO) {
      fprintf(errfp, MSG_BAD_RELTOLQ, *reltolQ);
      return(QCVREI_ILL_INPUT);
    }
    if (itolQ == SS) {
      neg_abstol = (*((realtype *)abstolQ) < ZERO);
    } else {
      neg_abstol = (N_VMin((N_Vector)abstolQ) < ZERO);
    }
    if (neg_abstol) {
      fprintf(errfp, MSG_BAD_ABSTOLQ);
      return(QCVREI_ILL_INPUT);
    }
  }

  /* Initialize znQ[0] in the history array */
  N_VConst(ZERO, cv_mem->cv_znQ[0]);

  /* Copy tolerances into memory and set the ewtQ vector */
  if (errconQ == FULL) {
    cv_mem->cv_itolQ    = itolQ;
    cv_mem->cv_reltolQ  = reltolQ;
    cv_mem->cv_abstolQ  = abstolQ;
    ewtsetOK = CVQuadEwtSet(cv_mem, cv_mem->cv_znQ[0]);
    if (!ewtsetOK) {
      fprintf(errfp, MSG_BAD_EWTQ);
      CVQuadFreeVectors(cv_mem);
      return(QCVREI_ILL_INPUT);
    }
  }

  /* All error checking is complete at this point */

  /* Copy the input parameters into CVODE state */
  cv_mem->cv_fQ    = fQ;

  /* Initialize counters */
  cv_mem->cv_nfQe  = 0;
  cv_mem->cv_netfQ = 0;

  /* Quadrature integration turned ON */
  cv_mem->cv_quad = TRUE;

  /* Quadrature re-initialization was successfull */
  return(SUCCESS);
}

#define nvspecQ (cv_mem->cv_nvspecQ)
#define fQ      (cv_mem->cv_fQ)
#define itolQ   (cv_mem->cv_itolQ)
#define reltolQ (cv_mem->cv_reltolQ)
#define abstolQ (cv_mem->cv_abstolQ)

/*=================================================================*/
/*BEGIN        SENSITIVITY OPTIONAL INPUT FUNCTIONS                */
/*=================================================================*/

int CVodeSetSensRhsFn(void *cvode_mem, SensRhsFn fS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_ifS  = ALLSENS;

  if (fS != NULL) {
    cv_mem->cv_fS      = fS;
    cv_mem->cv_fSDQ    = FALSE;
  } else {
    cv_mem->cv_fS      = CVSensRhsDQ;
    cv_mem->cv_fS_data = cvode_mem;
    cv_mem->cv_fSDQ    = TRUE;
  }

  return(SUCCESS);
}

#define fS (cv_mem->cv_fS)

/*-----------------------------------------------------------------*/

int CVodeSetSensRhs1Fn(void *cvode_mem, SensRhs1Fn fS1)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;
  
  cv_mem->cv_ifS  = ONESENS;

  if(fS1 != NULL) {
    cv_mem->cv_fS1     = fS1;
    cv_mem->cv_fSDQ    = FALSE;
  } else {
    cv_mem->cv_fS1     = CVSensRhs1DQ;
    cv_mem->cv_fS_data = cvode_mem;
    cv_mem->cv_fSDQ    = TRUE;
  }

  return(SUCCESS);
}

#define fS1  (cv_mem->cv_fS1)

/*-----------------------------------------------------------------*/

int CVodeSetSensFdata(void *cvode_mem, void *fS_data)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_fS_data = fS_data;

  return(SUCCESS);
}

#define fS_data (cv_mem->cv_fS_data)

/*-----------------------------------------------------------------*/

int CVodeSetSensErrCon(void *cvode_mem, int errconS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if ((errconS!=FULL) && (errconS!=PARTIAL)) {
    fprintf(errfp, MSG_CVS_BAD_ECON, errconS, FULL, PARTIAL);
    return(CVS_ILL_INPUT);
  }

  cv_mem->cv_errcon = errconS;

  return(SUCCESS);
}

#define errcon (cv_mem->cv_errcon)

/*-----------------------------------------------------------------*/

int CVodeSetSensRho(void *cvode_mem, realtype rho)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_rhomax = rho;

  return(SUCCESS);
}

#define rhomax (cv_mem->cv_rhomax)

/*-----------------------------------------------------------------*/

int CVodeSetSensPbar(void *cvode_mem, realtype *pbar)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_pbar = pbar;

  return(SUCCESS);
}

#define pbar (cv_mem->cv_pbar)

/*-----------------------------------------------------------------*/

int CVodeSetSensReltol(void *cvode_mem, realtype *reltolS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_reltolS = reltolS;

  return(SUCCESS);
}

#define reltolS (cv_mem->cv_reltolS)

/*-----------------------------------------------------------------*/

int CVodeSetSensAbstol(void *cvode_mem, void *abstolS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return(CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_abstolS = abstolS;

  return(SUCCESS);
}

#define abstolS (cv_mem->cv_abstolS)

/*------------------  CVodeSetSensMaxNonlinIters  ------------------*/
/* 
   Specifies the maximum number of nonlinear iterations during
   one solve for sensitivity equations (staggered).
*/
/*-----------------------------------------------------------------*/

int CVodeSetSensMaxNonlinIters(void *cvode_mem, int maxcorS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVS_NO_MEM);
    return (CVS_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  cv_mem->cv_maxcorS = maxcorS;

  return(SUCCESS);
}

#define maxcorS (cv_mem->cv_maxcorS)


/*=================================================================*/
/*END        SENSITIVITY OPTIONAL INPUT FUNCTIONS                  */
/*=================================================================*/

#define ifS          (cv_mem->cv_ifS)
#define fSDQ         (cv_mem->cv_fSDQ)
#define abstolSalloc (cv_mem->cv_abstolSalloc)
#define stgr1alloc   (cv_mem->cv_stgr1alloc)
#define nniS1        (cv_mem->cv_nniS1)
#define ncfnS1       (cv_mem->cv_ncfnS1)
#define ncfS1        (cv_mem->cv_ncfS1)

/*------------------   CVodeSensMalloc   --------------------------*/
/*
  CVodeSensMalloc allocates and initializes sensitivity related 
  memory for a problem. All problem specification inputs are 
  checked for errors. If any error occurs during initialization, 
  it is reported to the file whose file pointer is errfp. 
  The return value is SUCCESS = 0 if no errors occurred, or
  a negative value otherwise.
*/
/*-----------------------------------------------------------------*/

int CVodeSensMalloc(void *cvode_mem, int Ns, int ism, 
                    realtype *p, int *plist, N_Vector *yS0)
{
  CVodeMem    cv_mem;
  booleantype neg_abstol, allocOK, tolsetOK, ewtsetOK;
  int is;
  
  /* Check cvode_mem */
  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_SCVM_NO_MEM);
    return(SCVM_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  /* Check if Ns is legal */
  if (Ns<0) {
    fprintf(errfp, MSG_BAD_NS,Ns);
    return(SCVM_ILL_INPUT);
  }
  cv_mem->cv_Ns = Ns;

  /* Check if ism is legal */
  if ((ism!=SIMULTANEOUS) && (ism!=STAGGERED) && (ism!=STAGGERED1)) {
    fprintf(errfp, MSG_BAD_ISM,ism,SIMULTANEOUS,STAGGERED,STAGGERED1);
    return(SCVM_ILL_INPUT);
  }
  cv_mem->cv_ism = ism;
  
  /* Check if p is non-null */
  if (p==NULL) {
    fprintf(errfp, MSG_P_NULL);
    return(SCVM_ILL_INPUT);
  }
  cv_mem->cv_p     = p;
  cv_mem->cv_plist = plist;
 
  /* If pbar is needed, check if it is NULL */
  if( (abstolS==NULL) || (fSDQ==TRUE) )
    if (pbar==NULL) {
      fprintf(errfp, MSG_PBAR_NULL);
      return(SCVM_ILL_INPUT);
    }
  
  /* Check if yS0 is non-null */
  if ((cv_mem->cv_yS = yS0) == NULL) {
    fprintf(errfp, MSG_YS0_NULL);
    return(SCVM_ILL_INPUT);
  }

  /* Check if ism and ifS agree */
  if ((ism==STAGGERED1) && (ifS==ALLSENS)) {
    fprintf(errfp, MSG_BAD_ISM_IFS);
    return (SCVM_ILL_INPUT);
  }    

  /* NOTE: Even if errcon=PARTIAL, tolerances 
     are still needed for the convergence test
     in the nonlinear solver!!! */

  /* Check if reltolS is non-null and legal */
  if (reltolS!=NULL) {
    if (*reltolS<ZERO) {
      fprintf(errfp, MSG_BAD_RELTOLS, *reltolS);
      return(SCVM_ILL_INPUT);
    }
  } else {
    reltolS = reltol;
  }
  
  /* Check if abstolS is non-null and legal */
  if (abstolS != NULL) {
    abstolSalloc = FALSE;
    neg_abstol = CVSensTestAtol(cv_mem, abstolS);
    if (neg_abstol) {
      fprintf(errfp, MSG_BAD_ABSTOLS);
      return(SCVM_ILL_INPUT);
    }
  } else {
    abstolSalloc = TRUE;
    allocOK = CVSensAllocAtol(cv_mem, &abstolS);
    if (!allocOK) {
      fprintf(errfp, MSG_SCVM_MEM_FAIL);
      return(SCVM_MEM_FAIL);
    }
    tolsetOK = CVSensSetAtol(cv_mem, abstolS);
    if (!tolsetOK) {
      CVSensFreeAtol(cv_mem, abstolS);
      fprintf(errfp, MSG_BAD_PBAR);
      return(SCVM_ILL_INPUT);
    }
  }
  
  /* Allocate ncfS1, ncfnS1, and nniS1 if needed */
  if (ism == STAGGERED1) {
    stgr1alloc = TRUE;
    ncfS1  = (int*)malloc(Ns*sizeof(int));
    ncfnS1 = (int*)malloc(Ns*sizeof(int));
    nniS1  = (int*)malloc(Ns*sizeof(int));
    if ( (ncfS1 == NULL) || (ncfnS1 == NULL) || (nniS1 == NULL) ) {
      if (abstolSalloc) CVSensFreeAtol(cv_mem, abstolS);
      fprintf(errfp, MSG_SCVM_MEM_FAIL);
      return(SCVM_MEM_FAIL);
    }
  } else {
    stgr1alloc = FALSE;
  }

  /* Allocate the vectors */
  allocOK = CVSensAllocVectors(cv_mem);
  if (!allocOK) {
    if (abstolSalloc) CVSensFreeAtol(cv_mem, abstolS);
    if (stgr1alloc) {
      free(ncfS1);
      free(ncfnS1);
      free(nniS1);
    }
    fprintf(errfp, MSG_SCVM_MEM_FAIL);
    return(SCVM_MEM_FAIL);
  }
  
  /* Copy tolerances into memory and set the ewtS vectors */
  cv_mem->cv_reltolS = reltolS;
  cv_mem->cv_abstolS = abstolS;
  ewtsetOK = CVSensEwtSet(cv_mem, yS0);
  if (!ewtsetOK) {
    if (abstolSalloc) CVSensFreeAtol(cv_mem, abstolS);
    if (stgr1alloc) {
      free(ncfS1);
      free(ncfnS1);
      free(nniS1);
    }
    CVSensFreeVectors(cv_mem);
    fprintf(errfp, MSG_BAD_EWTS);
    return (SCVM_ILL_INPUT);
  }
  
  /*---------------------------------------------- 
     All error checking is complete at this point 
  -----------------------------------------------*/

  /* Initialize znS[0] in the history array */
  for (is=0; is<Ns; is++) 
    N_VScale(ONE, yS0[is], cv_mem->cv_znS[0][is]);

  /* Initialize all sensitivity related counters */
  cv_mem->cv_nfSe     = 0;
  cv_mem->cv_nfeS     = 0;
  cv_mem->cv_ncfnS    = 0;
  cv_mem->cv_netfS    = 0;
  cv_mem->cv_nniS     = 0;
  cv_mem->cv_nsetupsS = 0;
  if (ism==STAGGERED1)
    for (is=0; is<Ns; is++) {
      ncfnS1[is] = 0;
      nniS1[is] = 0;
    }

  /* Sensitivities will be computed */
  cv_mem->cv_sensi = TRUE;
  cv_mem->cv_sensMallocDone = TRUE;

  /* Sensitivity initialization was successfull */
  return (SUCCESS);
}

/*------------------   CVodeSensReInit   --------------------------*/
/*
  CVodeSensReInit re-initializes CVODES' sensitivity related memory 
  for a problem, assuming it has already been allocated in prior 
  calls to CVodeMalloc and CVodeSensMalloc. 
  All problem specification inputs are checked for errors.
  The number of sensitivities Ns is assumed to be unchanged since
  the previous call to CVodeSensMalloc.
  If any error occurs during initialization, it is reported to the
  file whose file pointer is errfp.
  The return value is SUCCESS = 0 if no errors occurred, or
  a negative value otherwise.
*/ 
/*-----------------------------------------------------------------*/


int CVodeSensReInit(void *cvode_mem, int ism,
                    realtype *p, int *plist, N_Vector *yS0)
{
  CVodeMem    cv_mem;
  booleantype neg_abstol, allocOK, tolsetOK, ewtsetOK;
  int Ns, is;  

  /* Check cvode_mem */
  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_SCVM_NO_MEM);
    return(SCVREI_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;
  Ns = cv_mem->cv_Ns;

  /* Was sensitivity initialized? */
  if (cv_mem->cv_sensMallocDone == FALSE) {
    fprintf(errfp, MSG_SREI_NO_SENSI);
    return(SCVREI_NO_SENSI);
  } 

  /* Check if ism is legal */
  if ((ism!=SIMULTANEOUS) && (ism!=STAGGERED) && (ism!=STAGGERED1)) {
    fprintf(errfp, MSG_BAD_ISM,ism,SIMULTANEOUS,STAGGERED,STAGGERED1);
    return(SCVREI_ILL_INPUT);
  }
  cv_mem->cv_ism = ism;
  
  /* Check if p is non-null */
  if (p==NULL) {
    fprintf(errfp, MSG_P_NULL);
    return(SCVREI_ILL_INPUT);
  }
  cv_mem->cv_p     = p;
  cv_mem->cv_plist = plist;

  /* If pbar is needed, check if it is NULL */
  if( (abstolS==NULL) || (fSDQ==TRUE) )
    if (pbar==NULL) {
      fprintf(errfp, MSG_PBAR_NULL);
      return(SCVREI_ILL_INPUT);
    }

  /* Check if pbar is non-null */
  if (pbar==NULL) {
    fprintf(errfp, MSG_PBAR_NULL);
    return(SCVREI_ILL_INPUT);
  }

  /* Check if yS0 is non-null */
  if (yS0 == NULL) {
    fprintf(errfp, MSG_YS0_NULL);
    return(SCVREI_ILL_INPUT);
  }  

  /* Check if ism and ifS agree */
  if ((ism==STAGGERED1) && (ifS==ALLSENS)) {
    fprintf(errfp, MSG_BAD_ISM_IFS);
    return(SCVREI_ILL_INPUT);
  }

  /* Check if reltolS is non-null and legal */
  if (reltolS!=NULL) {
    if (*reltolS<ZERO) {
      fprintf(errfp, MSG_BAD_RELTOLS, *reltolS);
      return(SCVREI_ILL_INPUT);
    }
  } else {
    reltolS = reltol;
  }

  /* Check if abstolS is non-null and legal */
  if (abstolS!=NULL) {
    abstolSalloc = FALSE;
    neg_abstol = CVSensTestAtol(cv_mem, abstolS);
    if (neg_abstol) {
      fprintf(errfp, MSG_BAD_ABSTOLS);
      return(SCVREI_ILL_INPUT);
    }
  } else {
    abstolSalloc = TRUE;
    allocOK = CVSensAllocAtol(cv_mem, &abstolS);
    if (!allocOK) {
      fprintf(errfp, MSG_SCVM_MEM_FAIL);
      return(SCVREI_MEM_FAIL);
    }
    tolsetOK = CVSensSetAtol(cv_mem, abstolS);
    if (!tolsetOK) {
      CVSensFreeAtol(cv_mem, abstolS);
      fprintf(errfp, MSG_BAD_PBAR);
      return(SCVREI_ILL_INPUT);
    }
  }
  
  /* Allocate ncfS1, ncfnS1, and nniS1 if needed */
  if ( (ism==STAGGERED1) && (stgr1alloc==FALSE) ) {
    stgr1alloc = TRUE;
    ncfS1  = (int*)malloc(Ns*sizeof(int));
    ncfnS1 = (int*)malloc(Ns*sizeof(int));
    nniS1  = (int*)malloc(Ns*sizeof(int));
    if ( (ncfS1==NULL) || (ncfnS1==NULL) || (nniS1==NULL) ) {
      if (abstolSalloc) CVSensFreeAtol(cv_mem, abstolS);
      fprintf(errfp, MSG_SCVM_MEM_FAIL);
      return(SCVREI_MEM_FAIL);
    }
  }

  /* Copy the tolerances into memory and set the ewtS vectors */
  cv_mem->cv_reltolS = reltolS;
  cv_mem->cv_abstolS = abstolS;
  ewtsetOK = CVSensEwtSet(cv_mem, yS0);
  if (!ewtsetOK) {
    if (abstolSalloc) {
      CVSensFreeAtol(cv_mem, abstolS);
      abstolSalloc = FALSE;
    }
    if (stgr1alloc) {
      free(ncfS1);
      free(ncfnS1);
      free(nniS1);
      stgr1alloc = FALSE;
    }
    CVSensFreeVectors(cv_mem);
    fprintf(errfp, MSG_BAD_EWTS);
    return (SCVREI_ILL_INPUT);
  }

  /*---------------------------------------------- 
     All error checking is complete at this point 
  -----------------------------------------------*/

  /* Initialize znS[0] in the history array */
  for (is=0; is<Ns; is++) 
    N_VScale(ONE, yS0[is], cv_mem->cv_znS[0][is]);

  /* Initialize all sensitivity related counters */
  cv_mem->cv_nfSe     = 0;
  cv_mem->cv_nfeS     = 0;
  cv_mem->cv_ncfnS    = 0;
  cv_mem->cv_netfS    = 0;
  cv_mem->cv_nniS     = 0;
  cv_mem->cv_nsetupsS = 0;
  if (ism==STAGGERED1)
    for (is=0; is<Ns; is++) {
      ncfnS1[is] = 0;
      nniS1[is] = 0;
    }

  /* Problem has been successfully re-initialized */
  cv_mem->cv_sensi = TRUE;
  return (SUCCESS);
}

#define Ns      (cv_mem->cv_Ns)
#define ism     (cv_mem->cv_ism)
#define p       (cv_mem->cv_p)
#define plist   (cv_mem->cv_plist)

/*=================================================================*/
/*BEGIN        Readibility Constants                               */
/*=================================================================*/

#define uround         (cv_mem->cv_uround)  
#define zn             (cv_mem->cv_zn) 
#define ewt            (cv_mem->cv_ewt)  
#define y              (cv_mem->cv_y)
#define acor           (cv_mem->cv_acor)
#define tempv          (cv_mem->cv_tempv)
#define ftemp          (cv_mem->cv_ftemp) 
#define q              (cv_mem->cv_q)
#define qprime         (cv_mem->cv_qprime)
#define next_q         (cv_mem->cv_next_q)
#define qwait          (cv_mem->cv_qwait)
#define L              (cv_mem->cv_L)
#define h              (cv_mem->cv_h)
#define hprime         (cv_mem->cv_hprime)
#define next_h         (cv_mem->cv_next_h)
#define eta            (cv_mem->cv_eta) 
#define etaqm1         (cv_mem->cv_etaqm1) 
#define etaq           (cv_mem->cv_etaq) 
#define etaqp1         (cv_mem->cv_etaqp1) 
#define nscon          (cv_mem->cv_nscon)
#define hscale         (cv_mem->cv_hscale)
#define tn             (cv_mem->cv_tn)
#define tau            (cv_mem->cv_tau)
#define tq             (cv_mem->cv_tq)
#define l              (cv_mem->cv_l)
#define rl1            (cv_mem->cv_rl1)
#define gamma          (cv_mem->cv_gamma) 
#define gammap         (cv_mem->cv_gammap) 
#define gamrat         (cv_mem->cv_gamrat)
#define crate          (cv_mem->cv_crate)
#define acnrm          (cv_mem->cv_acnrm)
#define mnewt          (cv_mem->cv_mnewt)
#define etamax         (cv_mem->cv_etamax)
#define nst            (cv_mem->cv_nst)
#define nfe            (cv_mem->cv_nfe)
#define ncfn           (cv_mem->cv_ncfn)
#define netf           (cv_mem->cv_netf)
#define nni            (cv_mem->cv_nni)
#define nsetups        (cv_mem->cv_nsetups)
#define nhnil          (cv_mem->cv_nhnil)
#define lrw1           (cv_mem->cv_lrw1)
#define liw1           (cv_mem->cv_liw1)
#define lrw            (cv_mem->cv_lrw)
#define liw            (cv_mem->cv_liw)
#define linit          (cv_mem->cv_linit)
#define lsetup         (cv_mem->cv_lsetup)
#define lsolve         (cv_mem->cv_lsolve) 
#define lfree          (cv_mem->cv_lfree) 
#define lmem           (cv_mem->cv_lmem) 
#define qu             (cv_mem->cv_qu)          
#define nstlp          (cv_mem->cv_nstlp)  
#define h0u            (cv_mem->cv_h0u)
#define hu             (cv_mem->cv_hu)         
#define saved_tq5      (cv_mem->cv_saved_tq5)  
#define jcur           (cv_mem->cv_jcur)         
#define tolsf          (cv_mem->cv_tolsf)      
#define setupNonNull   (cv_mem->cv_setupNonNull) 
#define forceSetup     (cv_mem->cv_forceSetup)
#define nor            (cv_mem->cv_nor)
#define ssdat          (cv_mem->cv_ssdat)

#define sensi          (cv_mem->cv_sensi)
#define znS            (cv_mem->cv_znS)
#define ewtS           (cv_mem->cv_ewtS)
#define acorS          (cv_mem->cv_acorS)
#define yS             (cv_mem->cv_yS)
#define tempvS         (cv_mem->cv_tempvS)
#define ftempS         (cv_mem->cv_ftempS)
#define crateS         (cv_mem->cv_crateS)
#define acnrmS         (cv_mem->cv_acnrmS)
#define nfSe           (cv_mem->cv_nfSe)
#define nfeS           (cv_mem->cv_nfeS)
#define nniS           (cv_mem->cv_nniS)
#define ncfnS          (cv_mem->cv_ncfnS)
#define netfS          (cv_mem->cv_netfS)
#define nsetupsS       (cv_mem->cv_nsetupsS)
#define abstolSalloc   (cv_mem->cv_abstolSalloc)
#define stgr1alloc     (cv_mem->cv_stgr1alloc)

#define quad           (cv_mem->cv_quad)
#define znQ            (cv_mem->cv_znQ)
#define ewtQ           (cv_mem->cv_ewtQ)
#define acorQ          (cv_mem->cv_acorQ)
#define yQ             (cv_mem->cv_yQ)
#define tempvQ         (cv_mem->cv_tempvQ)
#define acnrmQ         (cv_mem->cv_acnrmQ)
#define nfQe           (cv_mem->cv_nfQe)
#define netfQ          (cv_mem->cv_netfQ)
#define lrw1Q          (cv_mem->cv_lrw1Q)
#define liw1Q          (cv_mem->cv_liw1Q)

/*=================================================================*/
/*END          Readibility Constants                               */
/*=================================================================*/

/*------------------       CVode         --------------------------*/
/*
  This routine is the main driver of the CVODE package. 
  
  It integrates over a time interval defined by the user, by calling
  CVStep to do internal time steps.
  
  The first time that CVode is called for a successfully initialized
  problem, it computes a tentative initial step size h.
  
  CVode supports four modes, specified by itask: NORMAL, ONE_STEP,
  NORMAL_TSTOP, and ONE_STEP_TSTOP.
  In the NORMAL mode, the solver steps until it reaches or passes tout
  and then interpolates to obtain y(tout).
  In the ONE_STEP mode, it takes one internal step and returns.
  NORMAL_TSTOP and ONE_STEP_TSTOP are similar to NORMAL and ONE_STEP,
  respectively, but the integration never proceeds past tstop (which
  must have been defined through a call to CVodeSetStopTime).
*/
/*-----------------------------------------------------------------*/

int CVode(void *cvode_mem, realtype tout, N_Vector yout, 
          realtype *t, int itask)
{
  CVodeMem cv_mem;
  N_Vector wrk1, wrk2;
  int nstloc, kflag, istate, ier, task;
  booleantype istop, hOK, ewtsetOK, ewtSsetOK, ewtQsetOK;
  int is;
  realtype troundoff, rh, nrm;

  /* Check if cvode_mem exists */
  if (cvode_mem == NULL) {
    fprintf(stdout, MSG_CVODE_NO_MEM);
    return (CVODE_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  /* Check if cvode_mem was allocated */
  if (cv_mem->cv_MallocDone == FALSE) {
    fprintf(errfp, MSG_CVODE_NO_MALLOC);
    return(CVODE_NO_MALLOC);
  }
  
  /* Check for yout != NULL */
  if ((y = yout) == NULL) {
    fprintf(errfp, MSG_YOUT_NULL);       
    return (ILL_INPUT);
  }
  
  /* Check for t != NULL */
  if (t == NULL) {
    fprintf(errfp, MSG_T_NULL);
    return (ILL_INPUT);
  }
  *t = tn;

  /* Check for valid itask */
  if ((itask != NORMAL)       && 
      (itask != ONE_STEP)     &&
      (itask != NORMAL_TSTOP) &&
      (itask != ONE_STEP_TSTOP) ) {
    fprintf(errfp, MSG_BAD_ITASK, itask);
    return (ILL_INPUT);
  }

  /* Split itask into task and istop */
  if ((itask == NORMAL_TSTOP) || (itask == ONE_STEP_TSTOP)) {
    if ( tstopset == FALSE ) {
      fprintf(errfp, MSG_NO_TSTOP);
      return(ILL_INPUT);
    }
    istop = TRUE;
  } else {
    istop = FALSE;
  }
  if ((itask == NORMAL) || (itask == NORMAL_TSTOP)) {
    task = NORMAL;
  } else {
    task = ONE_STEP;
  }

  /* Begin first call block */
  
  if (nst == 0) {

    /* On first call, check solver functions and call linit function */

    if (iter == NEWTON) {
      if (linit == NULL) {
        fprintf(errfp, MSG_LINIT_NULL);
        return (ILL_INPUT);
      }
      if (lsetup == NULL) {
        fprintf(errfp, MSG_LSETUP_NULL);
        return (ILL_INPUT);
      }
      if (lsolve == NULL) {
        fprintf(errfp, MSG_LSOLVE_NULL);
        return (ILL_INPUT);
      }
      if (lfree == NULL) {
        fprintf(errfp, MSG_LFREE_NULL);
        return (ILL_INPUT);
      }
      ier = linit(cv_mem);
      if (ier != LINIT_OK) {
        fprintf(errfp, MSG_LINIT_FAIL);
        return (ILL_INPUT);
      }
    }
    
    /* 
       On the first call, call f at (t0,y0), set zn[1] = y'(t0), 
       set initial h (from H0 or CVHin), and scale zn[1] by h   
       If computing sensitivities, call fS at (t0,y0,yS0), set
       znS[1][is] = yS'(t0), is=1,...,Ns, and scale znS[1][is] by h. 
       If computing any quadratures, call fQ at (t0,znQ[0]), set
       znQ[1] = fQ, and scale znQ[1] by h.
    */
    
    f(tn, zn[0], zn[1], f_data); 
    nfe++;

    if (sensi) {
      wrk1 = tempv;
      wrk2 = ftemp;
      CVSensRhs(cv_mem, tn, zn[0], zn[1], znS[0], znS[1], tempv, ftemp);
    }    

    if (quad) {
      fQ(tn, zn[0], znQ[1], fQ_data);
      nfQe++;
    }

    h = hin;
    if ( (h != ZERO) && ((tout-tn)*h < ZERO) ) {
      fprintf(errfp, MSG_BAD_H0, h, tout-tn);
      return (ILL_INPUT);
    }
    if (h == ZERO) {
      hOK = CVHin(cv_mem, tout);
      if (!hOK) {
        fprintf(errfp, MSG_TOO_CLOSE, tout, tn);
        return (ILL_INPUT);
      }
    }
    rh = ABS(h)*hmax_inv;
    if (rh > ONE) h /= rh;
    if (ABS(h) < hmin) h *= hmin/ABS(h);

    /* On first call, check for approach to tstop */

    if (istop) {
      if ( (tstop - tn)*h < ZERO ) {
        fprintf(errfp, MSG_BAD_TSTOP, tstop, tn);
        return(ILL_INPUT);
      }
      if ( (tn + h - tstop)*h > ZERO ) 
        h = tstop - tn;
    }

    hscale = h;
    h0u    = h;
    hprime = h;

    N_VScale(h, zn[1], zn[1]);
    
    if (sensi)
      for (is=0; is<Ns; is++) 
        N_VScale(h, znS[1][is], znS[1][is]);
    
    if (quad)
      N_VScale(h, znQ[1], znQ[1]);

  } /* end first call block */

  /* At following steps, perform stop tests */

  if (nst > 0) {

    /* Test for tn past tstop */
    if ( istop && ((tstop - tn)*h < ZERO) ) {
      fprintf(errfp, MSG_BAD_TSTOP, tstop, tn);
      return (ILL_INPUT);
    }

    /* In NORMAL mode, test if tout was reached */
    if ( (task == NORMAL) && ((tn-tout)*h >= ZERO) ) {
      *t = tout;
      ier =  CVodeGetDky(cv_mem, tout, 0, yout);
      if (ier != OKAY) {
        fprintf(errfp, MSG_BAD_TOUT, tout);
        return (ILL_INPUT);
      }
      return (SUCCESS);
    }

    /* Test for tn at tstop or near tstop */
    if ( istop ) {

      troundoff = FUZZ_FACTOR*uround*(ABS(tn) + ABS(h));
      if ( ABS(tn - tstop) <= troundoff) {
        ier =  CVodeGetDky(cv_mem, tstop, 0, yout);
        if (ier != OKAY) {
          fprintf(errfp, MSG_BAD_TSTOP, tstop, tn);
          return (ILL_INPUT);
        }
        *t = tstop;
        return (TSTOP_RETURN);
      }
      
      if ( (tn + hprime - tstop)*h > ZERO ) {
        hprime = tstop - tn;
        eta = hprime/h;
      }

    }
    
  } /* end stop tests block */

  /* Start looping for internal steps */

  nstloc = 0;
  loop {
   
    next_h = h;
    next_q = q;
    
    /* Reset and check ewt */

    if (nst > 0) {

      ewtsetOK = CVEwtSet(cv_mem, zn[0]);
 
      if (sensi)
        ewtSsetOK = CVSensEwtSet(cv_mem, znS[0]);
      else
        ewtSsetOK = TRUE;

      if (quad && (errconQ == FULL))
        ewtQsetOK = CVQuadEwtSet(cv_mem, znQ[0]);
      else
        ewtQsetOK = TRUE;
        
      if ( (!ewtsetOK) || (!ewtSsetOK) || (!ewtQsetOK) ) {

        if(!ewtsetOK)  fprintf(errfp, MSG_EWT_NOW_BAD, tn);
        if(!ewtSsetOK) fprintf(errfp, MSG_EWTS_NOW_BAD, tn);
        if(!ewtQsetOK) fprintf(errfp, MSG_EWTQ_NOW_BAD, tn);

        istate = ILL_INPUT;
        *t = tn;
        N_VScale(ONE, zn[0], yout);
        break;

      }

    }

    /* Check for too many steps */
    
    if (nstloc >= mxstep) {
      fprintf(errfp, MSG_MAX_STEPS, tn, mxstep, tout);
      istate = TOO_MUCH_WORK;
      *t = tn;
      N_VScale(ONE, zn[0], yout);
      break;
    }

    /* Check for too much accuracy requested */

    nrm = N_VWrmsNorm(zn[0], ewt);
    if (quad && (errconQ == FULL)) {
      nrm = CVQuadUpdateNorm(cv_mem, nrm, znQ[0], ewtQ); 
    }
    if (sensi && (errcon == FULL)) {
      nrm = CVSensUpdateNorm(cv_mem, nrm, znS[0], ewtS);
    }
    tolsf = uround * nrm;

    if (tolsf > ONE) {
      fprintf(errfp, MSG_TOO_MUCH_ACC, tn);
      istate = TOO_MUCH_ACC;
      *t = tn;
      N_VScale(ONE, zn[0], yout);
      tolsf *= TWO;
      break;
    }
    
    /* Check for h below roundoff level in tn */

    if (tn + hprime == tn) {
      nhnil++;
      if (nhnil <= mxhnil) fprintf(errfp, MSG_HNIL, tn, hprime);
      if (nhnil == mxhnil) fprintf(errfp, MSG_HNIL_DONE, mxhnil);
    }

    /* Call CVStep to take a step */

    kflag = CVStep(cv_mem);

    /* Process failed step cases, and exit loop */
   
    if (kflag != SUCCESS_STEP) {
      istate = CVHandleFailure(cv_mem, kflag);
      *t = tn;
      N_VScale(ONE, zn[0], yout);
      break;
    }
    
    nstloc++;

    /* Check if tn is at tstop or near tstop */

    if ( istop ) {

      troundoff = FUZZ_FACTOR*uround*(ABS(tn) + ABS(h));
      if ( ABS(tn - tstop) <= troundoff) {
        (void) CVodeGetDky(cv_mem, tstop, 0, yout);
        *t = tstop;
        istate = TSTOP_RETURN;
        break;
      }

      if ( (tn + hprime - tstop)*h > ZERO ) {
        hprime = tstop - tn;
        eta = hprime/h;
      }

    }

    /* Check if in one-step mode, and if so copy y and exit loop */
    
    if (task == ONE_STEP) {
      istate = SUCCESS;
      *t = tn;
      N_VScale(ONE, zn[0], yout);
      next_q = qprime;
      next_h = hprime;
      break;
    }

    /* Check if tout reached, and if so interpolate and exit loop */

    if ((tn-tout)*h >= ZERO) {
      istate = SUCCESS;
      *t = tout;
      (void) CVodeGetDky(cv_mem, tout, 0, yout);
      next_q = qprime;
      next_h = hprime;
      break;
    }

  } /* end looping for internal steps */
  
  /* Load optional output */

  if (sensi && (ism==STAGGERED1)) { 
    nniS  = 0;
    ncfnS = 0;
    for (is=0; is<Ns; is++) {
      nniS  += nniS1[is];
      ncfnS += ncfnS1[is];
    }
  }
  
  return (istate);

}

/*=================================================================*/
/*BEGIN        CVODES OPTIONAL OUTPUT FUNCTIONS                    */
/*=================================================================*/

/*------------------    CVodeGetDky      --------------------------*/
/*
  This routine computes the k-th derivative of the interpolating
  polynomial at the time t and stores the result in the vector dky.
  The formula is:
          q 
   dky = SUM c(j,k) * (t - tn)^(j-k) * h^(-j) * zn[j] , 
         j=k 
  where c(j,k) = j*(j-1)*...*(j-k+1), q is the current order, and
  zn[j] is the j-th column of the Nordsieck history array.

  This function is called by CVode with k = 0 and t = tout, but
  may also be called directly by the user.
*/
/*-----------------------------------------------------------------*/

int CVodeGetDky(void *cvode_mem, realtype t, int k, N_Vector dky)
{
  realtype s, c, r;
  realtype tfuzz, tp, tn1;
  int i, j;
  CVodeMem cv_mem;
  
  /* Check all inputs for legality */
 
  if (cvode_mem == NULL) {
    fprintf(stdout, MSG_DKY_NO_MEM);
    return (CVG_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;

  if (dky == NULL) {
    fprintf(errfp, MSG_BAD_DKY);
    return (BAD_DKY);
  }

  if ((k < 0) || (k > q)) {
    fprintf(errfp, MSG_BAD_K, k);
    return (BAD_K);
  }
  
  /* Allow for some slack */
  tfuzz = FUZZ_FACTOR * uround * (ABS(tn) + ABS(hu));
  if (hu < ZERO) tfuzz = -tfuzz;
  tp = tn - hu - tfuzz;
  tn1 = tn + tfuzz;
  if ((t-tp)*(t-tn1) > ZERO) {
    fprintf(errfp, MSG_BAD_T, t, tn-hu, tn);
    return (BAD_T);
  }

  /* Sum the differentiated interpolating polynomial */

  s = (t - tn) / h;
  for (j=q; j >= k; j--) {
    c = ONE;
    for (i=j; i >= j-k+1; i--) c *= i;
    if (j == q) {
      N_VScale(c, zn[q], dky);
    } else {
      N_VLinearSum(c, zn[j], s, dky, dky);
    }
  }
  if (k == 0) return (OKAY);
  r = RPowerI(h,-k);
  N_VScale(r, dky, dky);
  return (OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetIntWorkSpace(void *cvode_mem, long int *leniw)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *leniw = liw;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetRealWorkSpace(void *cvode_mem, long int *lenrw)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *lenrw = lrw;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumSteps(void *cvode_mem, int *nsteps)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nsteps = nst;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumRhsEvals(void *cvode_mem, int *nfevals)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nfevals = nfe;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumLinSolvSetups(void *cvode_mem, int *nlinsetups)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nlinsetups = nsetups;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumErrTestFails(void *cvode_mem, int *netfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *netfails = netf;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetLastOrder(void *cvode_mem, int *qlast)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *qlast = q;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetCurrentOrder(void *cvode_mem, int *qcur)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *qcur = next_q;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumStabLimOrderReds(void *cvode_mem, int *nslred)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sldeton==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SLDET);
    return(CVG_NO_SLDET);
  }

  *nslred = nor;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetActualInitStep(void *cvode_mem, realtype *hinused)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *hinused = h0u;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetLastStep(void *cvode_mem, realtype *hlast)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *hlast = h;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetCurrentStep(void *cvode_mem, realtype *hcur)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;
  
  *hcur = next_h;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetCurrentTime(void *cvode_mem, realtype *tcur)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *tcur = tn;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetTolScaleFactor(void *cvode_mem, realtype *tolsfac)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *tolsfac = tolsf;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetErrWeights(void *cvode_mem, N_Vector *eweight)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *eweight = ewt;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetEstLocalErrors(void *cvode_mem, N_Vector *ele)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *ele = acor;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetWorkSpace(void *cvode_mem, long int *leniw, long int *lenrw)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *leniw = liw;
  *lenrw = lrw;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetIntegratorStats(void *cvode_mem, int *nsteps, int *nfevals, 
                            int *nlinsetups, int *netfails, int *qlast, 
                            int *qcur, realtype *hinused, realtype *hlast, 
                            realtype *hcur, realtype *tcur)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nsteps = nst;
  *nfevals = nfe;
  *nlinsetups = nsetups;
  *netfails = netf;
  *qlast = q;
  *qcur = next_q;
  *hinused = h0u;
  *hlast = h;
  *hcur = next_h;
  *tcur = tn;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumNonlinSolvIters(void *cvode_mem, int *nniters)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nniters = nni;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumNonlinSolvConvFails(void *cvode_mem, int *nncfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nncfails = ncfn;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNonlinSolvStats(void *cvode_mem, int *nniters, int *nncfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  *nniters = nni;
  *nncfails = ncfn;

  return(OKAY);
}

/*------------------  CVodeGetQuad       --------------------------*/
/* 
   This routine extracts quadrature solution into yQout.
   This is just a wrapper that calls CvodeGEtQuadDky with k=0                    
*/
/*-----------------------------------------------------------------*/
 
int CVodeGetQuad(void *cvode_mem, realtype t, N_Vector yQout)
{
  return (CVodeGetQuadDky(cvode_mem,t,0,yQout));
}

/*------------------   CVodeGetQuadDky   --------------------------*/
/*
  CVodeQuadDky computes the kth derivative of the yQ function at
  time t, where tn-hu <= t <= tn, tn denotes the current         
  internal time reached, and hu is the last internal step size   
  successfully used by the solver. The user may request 
  k=0, 1, ..., qu, where qu is the current order. 
  The derivative vector is returned in dky. This vector 
  must be allocated by the caller. It is only legal to call this         
  function after a successful return from CVode with quadrature
  computation enabled.
*/
/*-----------------------------------------------------------------*/

int CVodeGetQuadDky(void *cvode_mem, realtype t, int k, N_Vector dky)
{ 
  realtype s, c, r;
  realtype tfuzz, tp, tn1;
  int i, j;
  CVodeMem cv_mem;
  
  /* Check all inputs for legality */
  
  if (cvode_mem == NULL) {
    fprintf(stdout, MSG_QDKY_NO_MEM);
    return (CVG_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;  

  if(quad != TRUE) {
    fprintf(errfp, MSG_QDKY_NO_QUAD);
    return (CVG_NO_QUAD);
  }

  if (dky == NULL) {
    fprintf(errfp, MSG_QBAD_DKY);
    return (BAD_DKY);
  }
  
  if ((k < 0) || (k > q)) {
    fprintf(errfp, MSG_QBAD_K, k);
    return (BAD_K);
  }
  
  /* Allow for some slack */
  tfuzz = FUZZ_FACTOR * uround * (ABS(tn) + ABS(hu));
  if (hu < ZERO) tfuzz = -tfuzz;
  tp = tn - hu - tfuzz;
  tn1 = tn + tfuzz;
  if ((t-tp)*(t-tn1) > ZERO) {
    fprintf(errfp, MSG_QBAD_T, t, tn-hu, tn);
    return (BAD_T);
  }
  
  /* Sum the differentiated interpolating polynomial */
  
  s = (t - tn) / h;
  for (j=q; j >= k; j--) {
    c = ONE;
    for (i=j; i >= j-k+1; i--) c *= i;
    if (j == q) {
      N_VScale(c, znQ[q], dky);
    } else {
      N_VLinearSum(c, znQ[j], s, dky, dky);
    }
  }
  if (k == 0) return (OKAY);
  r = RPowerI(h,-k);
  N_VScale(r, dky, dky);
  return (OKAY);
  
}

/*-----------------------------------------------------------------*/

int CVodeGetNumQuadRhsEvals(void *cvode_mem, int *nfQevals)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (quad==FALSE) {
    fprintf(errfp, MSG_CVG_NO_QUAD);
    return(CVG_NO_QUAD);
  }

  *nfQevals = nfQe;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumQuadErrTestFails(void *cvode_mem, int *nQetfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (quad==FALSE) {
    fprintf(errfp, MSG_CVG_NO_QUAD);
    return(CVG_NO_QUAD);
  }

  *nQetfails = netfQ;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetQuadErrWeights(void *cvode_mem, N_Vector *eQweight)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (quad==FALSE) {
    fprintf(errfp, MSG_CVG_NO_QUAD);
    return(CVG_NO_QUAD);
  }

  if(errconQ==FULL) *eQweight = ewtQ;
  else              *eQweight = NULL;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetQuadStats(void *cvode_mem, int *nfQevals, int *nQetfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (quad==FALSE) {
    fprintf(errfp, MSG_CVG_NO_QUAD);
    return(CVG_NO_QUAD);
  }

  *nfQevals = nfQe;
  *nQetfails = netfQ;

  return(OKAY);
}

/*------------------    CVodeGetSens     --------------------------*/
/* 
   This routine extracts sensitivity solution into ySout.
   This is just a wrapper that calls CvodeSensDkyAll with k=0                    
*/
/*-----------------------------------------------------------------*/
 
int CVodeGetSens(void *cvode_mem, realtype t, N_Vector *ySout)
{
  return (CVodeGetSensDkyAll(cvode_mem,t,0,ySout));
}
    
/*------------------  CVodeGetSensDkyAll   ------------------------*/
/*
  If the user calls directly CVodeSensDkyAll then s must be allocated
  prior to this call. When CVodeSensDkyAll is called by 
  CVodeSensExtract, only ier=OKAY, ier=DKY_NO_SENSI, or 
  ier=BAD_T are possible.
*/
/*-----------------------------------------------------------------*/

int CVodeGetSensDkyAll(void *cvode_mem, realtype t, int k, N_Vector *dkyA)
{
  int ier=OKAY;
  int is;
  CVodeMem cv_mem;
  
  if (cvode_mem == NULL) {
    fprintf(stdout, MSG_SDKY_NO_MEM);
    return (CVG_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;  
  
  if (dkyA == NULL) {
    fprintf(errfp, MSG_SBAD_DKYA);
    return (BAD_DKY);
  }
  
  for (is=0; is<Ns; is++) {
    ier = CVodeGetSensDky(cvode_mem,t,k,is+1,dkyA[is]);
    if (ier!=OKAY) break;
  }
  
  return (ier);
}
    
/*------------------   CVodeGetSensDky   --------------------------*/
/*
  CVodeSensDky computes the kth derivative of the yS[is] function at
  time t, where tn-hu <= t <= tn, tn denotes the current         
  internal time reached, and hu is the last internal step size   
  successfully used by the solver. The user may request 
  is=1, 2, ..., Ns and k=0, 1, ..., qu, where qu is the current
  order. The derivative vector is returned in dky. This vector 
  must be allocated by the caller. It is only legal to call this         
  function after a successful return from CVode with sensitivity 
  computation enabled.
*/
/*-----------------------------------------------------------------*/

int CVodeGetSensDky(void *cvode_mem, realtype t, int k, int is, 
                    N_Vector dky)
{ 
  realtype s, c, r;
  realtype tfuzz, tp, tn1;
  int i, j;
  CVodeMem cv_mem;
  
  /* Check all inputs for legality */
  
  if (cvode_mem == NULL) {
    fprintf(stdout, MSG_SDKY_NO_MEM);
    return (CVG_NO_MEM);
  }
  cv_mem = (CVodeMem) cvode_mem;  
  
  if(sensi != TRUE) {
    fprintf(errfp, MSG_SDKY_NO_SENSI);
    return (CVG_NO_SENS);
  }

  if (dky == NULL) {
    fprintf(errfp, MSG_SBAD_DKY);
    return (BAD_DKY);
  }
  
  if ((k < 0) || (k > q)) {
    fprintf(errfp, MSG_SBAD_K, k);
    return (BAD_K);
  }
  
  if ((is < 1) || (is > Ns)) {
    fprintf(errfp, MSG_SBAD_IS, is);
    return (BAD_IS);
  }
  
  is--;

  /* Allow for some slack */
  tfuzz = FUZZ_FACTOR * uround * (ABS(tn) + ABS(hu));
  if (hu < ZERO) tfuzz = -tfuzz;
  tp = tn - hu - tfuzz;
  tn1 = tn + tfuzz;
  if ((t-tp)*(t-tn1) > ZERO) {
    fprintf(errfp, MSG_SBAD_T, t, tn-hu, tn);
    return (BAD_T);
  }
  
  /* Sum the differentiated interpolating polynomial */
  
  s = (t - tn) / h;
  for (j=q; j >= k; j--) {
    c = ONE;
    for (i=j; i >= j-k+1; i--) c *= i;
    if (j == q) {
      N_VScale(c, znS[q][is], dky);
    } else {
      N_VLinearSum(c, znS[j][is], s, dky, dky);
    }
  }
  if (k == 0) return (OKAY);
  r = RPowerI(h,-k);
  N_VScale(r, dky, dky);
  return (OKAY);
  
}

/*-----------------------------------------------------------------*/

int CVodeGetNumSensRhsEvals(void *cvode_mem, int *nfSevals)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nfSevals = nfSe;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumRhsEvalsSens(void *cvode_mem, int *nfevalsS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nfevalsS = nfeS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumSensErrTestFails(void *cvode_mem, int *nSetfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nSetfails = netfS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumSensLinSolvSetups(void *cvode_mem, int *nlinsetupsS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nlinsetupsS = nsetupsS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetSensErrWeights(void *cvode_mem, N_Vector_S *eSweight)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *eSweight = ewtS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetSensStats(void *cvode_mem, int *nfSevals, int *nfevalsS, 
                      int *nSetfails, int *nlinsetupsS)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nfSevals = nfSe;
  *nfevalsS = nfeS;
  *nSetfails = netfS;
  *nlinsetupsS = nsetupsS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumSensNonlinSolvIters(void *cvode_mem, int *nSniters)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nSniters = nniS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumSensNonlinSolvConvFails(void *cvode_mem, int *nSncfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nSncfails = ncfnS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumStgrSensNonlinSolvIters(void *cvode_mem, int *nSTGR1niters)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  if(ism==STAGGERED1) nSTGR1niters = nniS1;
  else                nSTGR1niters = NULL;
    

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetNumStgrSensNonlinSolvConvFails(void *cvode_mem, int *nSTGR1ncfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  if(ism==STAGGERED1) nSTGR1ncfails = ncfnS1;
  else                nSTGR1ncfails = NULL;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetSensNonlinSolvStats(void *cvode_mem, int *nSniters, int *nSncfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  *nSniters = nniS;
  *nSncfails = ncfnS;

  return(OKAY);
}

/*-----------------------------------------------------------------*/

int CVodeGetStgrSensNonlinSolvStats(void *cvode_mem, int *nSTGR1niters, int *nSTGR1ncfails)
{
  CVodeMem cv_mem;

  if (cvode_mem==NULL) {
    fprintf(stdout, MSG_CVG_NO_MEM);
    return(CVG_NO_MEM);
  }

  cv_mem = (CVodeMem) cvode_mem;

  if (sensi==FALSE) {
    fprintf(errfp, MSG_CVG_NO_SENSI);
    return(CVG_NO_SENS);
  }

  if(ism==STAGGERED1) {
    nSTGR1niters  = nniS1;
    nSTGR1ncfails = ncfnS1;
  } else {
    nSTGR1niters  = NULL;
    nSTGR1ncfails = NULL;
  }
  return(OKAY);
}

/*=================================================================*/
/*END        CVODES OPTIONAL OUTPUT FUNCTIONS                      */
/*=================================================================*/

/*------------------      CVodeFree      --------------------------*/
/*
  This routine frees the problem memory allocated by CVodeMalloc.
  Such memory includes all the vectors allocated by CVAllocVectors,
  and the memory lmem for the linear solver (deallocated by a call
  to lfree), as well as (if Ns!=0) all memory allocated for 
  sensitivity computations by CVodeSensMalloc.
*/
/*-----------------------------------------------------------------*/

void CVodeFree(void *cvode_mem)
{
  CVodeMem cv_mem;

  cv_mem = (CVodeMem) cvode_mem;
  
  if (cvode_mem == NULL) return;

  CVFreeVectors(cv_mem);

  CVodeQuadFree(cv_mem);

  CVodeSensFree(cv_mem);

  if (iter == NEWTON) 
    lfree(cv_mem);

  free(cv_mem);
}

/*------------------  CVodeQuadFree      --------------------------*/
/*
  CVodeQuadFree frees the problem memory in cvode_mem allocated
  for quadrature integration. Its only argument is the pointer
  cvode_mem returned by CVodeCreate. 
*/
/*-----------------------------------------------------------------*/

void CVodeQuadFree(void *cvode_mem)
{
  CVodeMem cv_mem;
  
  if (cvode_mem == NULL) return;
  cv_mem = (CVodeMem) cvode_mem;

  if(quad) {
    CVQuadFreeVectors(cv_mem);
    quad = FALSE;
  }
}

/*------------------  CVodeSensFree      --------------------------*/
/*
  CVodeSensFree frees the problem memory in cvode_mem allocated
  for sensitivity analysis. Its only argument is the pointer
  cvode_mem returned by CVodeCreate. 
*/
/*-----------------------------------------------------------------*/

void CVodeSensFree(void *cvode_mem)
{
  CVodeMem cv_mem;
  
  if (cvode_mem == NULL) return;
  cv_mem = (CVodeMem) cvode_mem;

  if(sensi) {
    if (abstolSalloc) CVSensFreeAtol(cv_mem, abstolS);
    if (stgr1alloc) {
      free(ncfS1);
      free(ncfnS1);
      free(nniS1);
    }
    CVSensFreeVectors(cv_mem);
    sensi = FALSE;
  }
}

/*=================================================================*/
/*END          EXPORTED FUNCTIONS IMPLEMENTATION                   */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        PRIVATE FUNCTIONS IMPLEMENTATION                    */
/*=================================================================*/

/*------------------   CVAllocVectors    --------------------------*/
/*
  This routine allocates the CVODE vectors ewt, acor, tempv, ftemp, and
  zn[0], ..., zn[maxord]. The length of the vectors is the input
  parameter neq and the maximum order (needed to allocate zn) is the
  input parameter maxord. If all memory allocations are successful,
  CVAllocVectors returns TRUE. Otherwise all allocated memory is freed
  and CVAllocVectors returns FALSE.
  This routine also sets the optional outputs lrw and liw, which are
  (respectively) the lengths of the real and integer work spaces
  allocated here.
*/
/*-----------------------------------------------------------------*/

static booleantype CVAllocVectors(CVodeMem cv_mem)
{
  int i, j;

  /* Allocate ewt, acor, tempv, ftemp */
  
  ewt = N_VNew(nvspec);
  if (ewt == NULL) return (FALSE);
  acor = N_VNew(nvspec);
  if (acor == NULL) {
    N_VFree(ewt);
    return (FALSE);
  }
  tempv = N_VNew(nvspec);
  if (tempv == NULL) {
    N_VFree(ewt);
    N_VFree(acor);
    return (FALSE);
  }
  ftemp = N_VNew(nvspec);
  if (ftemp == NULL) {
    N_VFree(tempv);
    N_VFree(ewt);
    N_VFree(acor);
    return (FALSE);
  }

  /* Allocate zn[0] ... zn[maxord] */

  for (j=0; j <= qmax; j++) {
    zn[j] = N_VNew(nvspec);
    if (zn[j] == NULL) {
      N_VFree(ewt);
      N_VFree(acor);
      N_VFree(tempv);
      N_VFree(ftemp);
      for (i=0; i < j; i++) N_VFree(zn[i]);
      return (FALSE);
    }
  }

  /* Set solver workspace lengths  */

  lrw = (qmax + 5)*lrw1;
  liw = (qmax + 5)*liw1;

  return (TRUE);
}

/*------------------    CVFreeVectors    --------------------------*/
/*  
  This routine frees the CVODE vectors allocated in CVAllocVectors.
*/
/*-----------------------------------------------------------------*/

static void CVFreeVectors(CVodeMem cv_mem)
{
  int j;
  
  N_VFree(ewt);
  N_VFree(acor);
  N_VFree(tempv);
  N_VFree(ftemp);
  for (j=0; j <= qmax; j++) N_VFree(zn[j]);
}

/*------------------     CVEwtSet        --------------------------*/
/*  
  This routine is responsible for setting the error weight vector ewt,
  according to tol_type, as follows:
    
  (1) ewt[i] = 1 / (*rtol * ABS(ycur[i]) + *atol), i=0,...,neq-1
      if tol_type = SS
  (2) ewt[i] = 1 / (*rtol * ABS(ycur[i]) + atol[i]), i=0,...,neq-1
      if tol_type = SV

   CVEwtSet returns TRUE if ewt is successfully set as above to a
   positive vector and FALSE otherwise. In the latter case, ewt is
   considered undefined after the FALSE return from CVEwtSet.

   All the real work is done in the routines CVEwtSetSS, CVEwtSetSV.
*/
/*-----------------------------------------------------------------*/

static booleantype CVEwtSet(CVodeMem cv_mem, N_Vector ycur)
{
  booleantype flag=TRUE;

  switch (itol) {
  case SS: 
    flag = CVEwtSetSS(cv_mem, ycur);
    break;
  case SV: 
    flag = CVEwtSetSV(cv_mem, ycur);
    break;
  }

  return(flag);

}

/*------------------    CVEwtSetSS       --------------------------*/
/*
  This routine sets ewt as decribed above in the case tol_type = SS.
  It tests for non-positive components before inverting. CVEwtSetSS
  returns TRUE if ewt is successfully set to a positive vector
  and FALSE otherwise. In the latter case, ewt is considered
  undefined after the FALSE return from CVEwtSetSS.
*/
/*-----------------------------------------------------------------*/

static booleantype CVEwtSetSS(CVodeMem cv_mem, N_Vector ycur)
{
  realtype rtoli, atoli;
  
  rtoli = *reltol;
  atoli = *((realtype *)abstol);
  N_VAbs(ycur, tempv);
  N_VScale(rtoli, tempv, tempv);
  N_VAddConst(tempv, atoli, tempv);
  if (N_VMin(tempv) <= ZERO) return (FALSE);
  N_VInv(tempv, ewt);
  return (TRUE);
}

/*------------------     CVEwtSetSV      --------------------------*/
/*
  This routine sets ewt as decribed above in the case tol_type = SV.
  It tests for non-positive components before inverting. CVEwtSetSV
  returns TRUE if ewt is successfully set to a positive vector
  and FALSE otherwise. In the latter case, ewt is considered
  undefined after the FALSE return from CVEwtSetSV.
*/
/*-----------------------------------------------------------------*/

static booleantype CVEwtSetSV(CVodeMem cv_mem, N_Vector ycur)
{
  realtype rtoli;
  
  rtoli = *reltol;
  N_VAbs(ycur, tempv);
  N_VLinearSum(rtoli, tempv, ONE, (N_Vector)abstol, tempv);
  if (N_VMin(tempv) <= ZERO) return (FALSE);
  N_VInv(tempv, ewt);
  return (TRUE);
}

/*------------------ CVQuadAllocVectors  --------------------------*/
/*
  NOTE: Space for ewtQ is allocated even when errconQ=PARTIAL, 
  although in this case, ewtQ is never used. The reason for this
  decision is to allow the user to re-initialize the quadrature
  computation with errconQ=FULL, after an initialization with
  errconQ=FALSE, without new memory allocation within 
  CVodeQuadReInit.
*/
/*-----------------------------------------------------------------*/

static booleantype CVQuadAllocVectors(CVodeMem cv_mem) 
{
  int i, j;

  /* Allocate ewtQ */
  ewtQ = N_VNew(nvspecQ);
  if (ewtQ == NULL) {
    return (FALSE);
  }
  
  /* Allocate acorQ */
  acorQ = N_VNew(nvspecQ);
  if (acorQ == NULL) {
    N_VFree(ewtQ);
    return (FALSE);
  }

  /* Allocate yQ */
  yQ = N_VNew(nvspecQ);
  if (yQ == NULL) {
    N_VFree(ewtQ);
    N_VFree(acorQ);
    return (FALSE);
  }

  /* Allocate tempvQ */
  tempvQ = N_VNew(nvspecQ);
  if (tempvQ == NULL) {
    N_VFree(ewtQ);
    N_VFree(acorQ);
    N_VFree(yQ);
    return (FALSE);
  }

  /* Allocate zQn[0] ... zQn[maxord] */

  for (j=0; j <= qmax; j++) {
    znQ[j] = N_VNew(nvspecQ);
    if (znQ[j] == NULL) {
      N_VFree(ewtQ);
      N_VFree(acorQ);
      N_VFree(yQ);
      N_VFree(tempvQ);
      for (i=0; i < j; i++) N_VFree(znQ[i]);
      return (FALSE);
    }
  }

  /* Update solver workspace lengths */
  lrw += (qmax + 4)*lrw1Q;
  liw += (qmax + 5)*liw1Q;

  return(TRUE);
}

/*------------------   CVQuadEwtSet      --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVQuadEwtSet(CVodeMem cv_mem, N_Vector qcur)
{
  booleantype flag=TRUE;

  switch (itolQ) {
  case SS: 
    flag = CVQuadEwtSetSS(cv_mem, qcur);
    break;
  case SV: 
    flag = CVQuadEwtSetSV(cv_mem, qcur);
    break;
  }

  return(flag);

}

/*------------------   CVQuadEwtSetSS    --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVQuadEwtSetSS(CVodeMem cv_mem, N_Vector qcur)
{
  realtype rtoli, atoli;
  
  rtoli = *reltolQ;
  atoli = *((realtype *)abstolQ);

  N_VAbs(qcur, tempvQ);
  N_VScale(rtoli, tempvQ, tempvQ);
  N_VAddConst(tempvQ, atoli, tempvQ);
  if (N_VMin(tempvQ) <= ZERO) return (FALSE);
  N_VInv(tempvQ, ewtQ);

  return (TRUE);
}

/*------------------  CVQuadEwtSetSV     --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVQuadEwtSetSV(CVodeMem cv_mem, N_Vector qcur)
{
  realtype rtoli;
  
  rtoli = *reltolQ;

  N_VAbs(qcur, tempvQ);
  N_VLinearSum(rtoli, tempvQ, ONE, (N_Vector)abstolQ, tempvQ);
  if (N_VMin(tempvQ) <= ZERO) return (FALSE);
  N_VInv(tempvQ, ewtQ);

  return (TRUE);
}

/*------------------  CVQuadFreeVectors  --------------------------*/
/*-----------------------------------------------------------------*/

static void CVQuadFreeVectors(CVodeMem cv_mem)
{
  int j;
  
  N_VFree(ewtQ);
  N_VFree(acorQ);
  N_VFree(yQ);
  N_VFree(tempvQ);
  
  for (j=0; j<=qmax; j++) N_VFree(znQ[j]);
  
}

/*------------------    CVSensTestAtol   --------------------------*/
/*
  This routine tests the user provided absolute tolerances for
  sensitivities. If a negative tolerance is detected, it 
  returns TRUE, otherwise it returns FALSE.
*/
/*-----------------------------------------------------------------*/

static booleantype CVSensTestAtol(CVodeMem cv_mem, void *atolS)
{
  int is;
  realtype *atolSS;
  N_Vector *atolSV;
  
  switch (itol) {
  case SS:
    atolSS = (realtype *)atolS;
    for (is=0; is<Ns; is++)
      if (atolSS[is] < ZERO) return (TRUE);
    break;
  case SV:
    atolSV = (N_Vector *)atolS;
    for (is=0; is<Ns; is++) 
      if (N_VMin(atolSV[is]) < ZERO) return (TRUE);
    break;
  }
  
  return (FALSE);
  
}

/*------------------   CVSensAllocAtol   --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensAllocAtol(CVodeMem cv_mem, void **atolSPtr)
{

  switch (itol) {
  case SS:
    *atolSPtr = (void *)malloc(Ns*sizeof(realtype));
    break;
  case SV:
    *atolSPtr = (void *)N_VNew_S(Ns, nvspec);
    break;
  }
  
  if (*atolSPtr==NULL) return (FALSE);
  else                 return (TRUE);
  
}

/*------------------   CVSensFreeAtol  ----------------------------*/
/*-----------------------------------------------------------------*/

static void CVSensFreeAtol(CVodeMem cv_mem, void *atolS)
{
  switch (itol) {
  case SS:
    free((realtype*)atolS);
    break;
  case SV:
    N_VFree_S(Ns,(N_Vector *)atolS);
    break;
  }

}

/*------------------   CVSensSetAtol     --------------------------*/
/*
  This routine sets the absolute tolerances for sensitivities. 
  It is called only if the user has passed a NULL pointer to
  CVodeSensMalloc.
  If all memory allocation is successfull, it returns TRUE.
*/
/*-----------------------------------------------------------------*/

static booleantype CVSensSetAtol(CVodeMem cv_mem, void *atolS)
{
  booleantype flag=TRUE;

  switch (itol) {
  case SS: 
    flag = CVSensSetAtolSS(cv_mem, (realtype *)atolS);
    break;
  case SV: 
    flag = CVSensSetAtolSV(cv_mem, (N_Vector *)atolS);
    break;
  }
  
  return(flag);

}

/*------------------    CVSensSetAtolSS  --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensSetAtolSS(CVodeMem cv_mem, realtype *atolS)
{
  int is, which;
  realtype rpbar;
  
  for (is=0; is<Ns; is++) {
    if (plist!=NULL) which = abs(plist[is]) - 1; 
    else             which = is;
    if (pbar[which]==ZERO) return (FALSE);
    rpbar = ONE/ABS(pbar[which]);
    atolS[is] = *((realtype *)abstol) * rpbar;
  }
  
  return (TRUE);
}

/*------------------   CVSensSetAtolSV   --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensSetAtolSV(CVodeMem cv_mem, N_Vector *atolS)
{
  int is, which;
  realtype rpbar;
  
  for (is=0; is<Ns; is++) {
    if (plist!=NULL) which = abs(plist[is]) - 1;
    else             which = is;
    if (pbar[which]==ZERO) return (FALSE);
    rpbar = ONE/ABS(pbar[which]);
    N_VScale(rpbar, (N_Vector)abstol, atolS[is]);
  }
  
  return (TRUE);
}

/*------------------  CVSensAllocVectors --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensAllocVectors(CVodeMem cv_mem) 
{
  int i, j;
  
  /* Allocate ewtS */
  ewtS  = N_VNew_S(Ns, nvspec);
  if (ewtS == NULL) {
    return (FALSE);
  }
  
  /* Allocate acorS */
  acorS = N_VNew_S(Ns, nvspec);
  if (acorS == NULL) {
    N_VFree_S(Ns,ewtS);
    return (FALSE);
  }
  
  /* Allocate tempvS */
  tempvS = N_VNew_S(Ns, nvspec);
  if (tempvS == NULL) {
    N_VFree_S(Ns,ewtS);
    N_VFree_S(Ns,acorS);
    return (FALSE);
  }
    
  /* Allocate ftempS */
  ftempS = N_VNew_S(Ns, nvspec);
  if (ftempS == NULL) {
    N_VFree_S(Ns,ewtS);
    N_VFree_S(Ns,acorS);
    N_VFree_S(Ns,tempvS);
    return (FALSE);
  }
  
  /* Allocate znS */
  for (j=0; j<=qmax; j++) {
    znS[j] = N_VNew_S(Ns, nvspec);
    if (znS[j] == NULL) {
      N_VFree_S(Ns,ewtS);
      N_VFree_S(Ns,acorS);
      N_VFree_S(Ns,tempvS);
      N_VFree_S(Ns,ftempS);
      for (i=0; i<j; i++) N_VFree_S(Ns,znS[i]);
      return (FALSE);
    }
  }
  
  /* Update solver workspace lengths */
  lrw += (qmax + 4)*Ns*lrw1;
  liw += (qmax + 4)*Ns*liw1;
  
  return (TRUE);
}

/*------------------  CVSensFreeVectors  --------------------------*/
/*-----------------------------------------------------------------*/

static void CVSensFreeVectors(CVodeMem cv_mem) 
{
  int j;
  
  N_VFree_S(Ns,ewtS);
  N_VFree_S(Ns,acorS);
  N_VFree_S(Ns,tempvS);
  N_VFree_S(Ns,ftempS);
  
  for (j=0; j<=qmax; j++) N_VFree_S(Ns,znS[j]);
  
}

/*------------------    CVSensEwtSet     --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensEwtSet(CVodeMem cv_mem, N_Vector *yScur)
{
  booleantype flag=TRUE;

  switch (itol) {
  case SS: 
    flag = CVSensEwtSetSS(cv_mem, yScur);
    break;
  case SV: 
    flag = CVSensEwtSetSV(cv_mem, yScur);
    break;
  }

  return(flag);

}

/*------------------   CVSensEwtSetSS    --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensEwtSetSS(CVodeMem cv_mem, N_Vector *yScur)
{
  int is;
  realtype rtoli, atoli;
  
  for (is=0; is<Ns; is++) {
    rtoli = *reltolS;
    atoli = ((realtype *)abstolS)[is];
    N_VAbs(yScur[is], tempv);
    N_VScale(rtoli, tempv, tempv);
    N_VAddConst(tempv, atoli, tempv);
    if (N_VMin(tempv) <= ZERO) return (FALSE);
    N_VInv(tempv, ewtS[is]);
  }
  return (TRUE);
}

/*------------------    CVSensEwtSetSV   --------------------------*/
/*-----------------------------------------------------------------*/

static booleantype CVSensEwtSetSV(CVodeMem cv_mem, N_Vector *yScur)
{
  int is;
  realtype rtoli;
  
  for (is=0; is<Ns; is++) {
    rtoli = *reltolS;
    N_VAbs(yScur[is], tempv);
    N_VLinearSum(rtoli, tempv, ONE, ((N_Vector *)abstolS)[is], tempv);
    if (N_VMin(tempv) <= ZERO) return (FALSE);
    N_VInv(tempv, ewtS[is]);
  }
  return (TRUE);
}

/*------------------      CVHin          --------------------------*/
/*
  This routine computes a tentative initial step size h0. 
  If tout is too close to tn (= t0), then CVHin returns FALSE and
  h remains uninitialized. Otherwise, CVHin sets h to the chosen 
  value h0 and returns TRUE.

  The algorithm used seeks to find h0 as a solution of
        (WRMS norm of (h0^2 ydd / 2)) = 1, 
  where ydd = estimated second derivative of y.
*/
/*-----------------------------------------------------------------*/

static booleantype CVHin(CVodeMem cv_mem, realtype tout)
{
  int sign, count;
  realtype tdiff, tdist, tround, hlb, hub;
  realtype hg, hgs, hnew, hrat, h0, yddnrm;

  /* Test for tout too close to tn */
  
  if ((tdiff = tout-tn) == ZERO) return (FALSE);
  
  sign = (tdiff > ZERO) ? 1 : -1;
  tdist = ABS(tdiff);
  tround = uround * MAX(ABS(tn), ABS(tout));
  if (tdist < TWO*tround) return (FALSE);
  
  /* Set lower and upper bounds on h0, and take geometric mean 
     Exit with this value if the bounds cross each other       */

  hlb = HLB_FACTOR * tround;
  hub = CVUpperBoundH0(cv_mem, tdist);
  hg  = RSqrt(hlb*hub);
  if (hub < hlb) {
    if (sign == -1) hg = -hg;
    h = hg;
    return (TRUE);
  }
  
  /* Loop up to MAX_ITERS times to find h0.
     Stop if new and previous values differ by a factor < 2.
     Stop if hnew/hg > 2 after one iteration, as this probably means
     that the ydd value is bad because of cancellation error.        */

  count = 0;
  loop {
    hgs = hg*sign;
    yddnrm = CVYddNorm(cv_mem, hgs);
    hnew =  (yddnrm*hub*hub > TWO) ? RSqrt(TWO/yddnrm) : RSqrt(hg*hub);
    count++;
    if (count >= MAX_ITERS) break;
    hrat = hnew/hg;
    if ((hrat > HALF) && (hrat < TWO)) break;
    if ((count >= 2) && (hrat > TWO)) {
      hnew = hg;
      break;
    }
    hg = hnew;
  }
  
  /* Apply bounds, bias factor, and attach sign */

  h0 = H_BIAS*hnew;
  if (h0 < hlb) h0 = hlb;
  if (h0 > hub) h0 = hub;
  if (sign == -1) h0 = -h0;
  h = h0;

  return (TRUE);
}

/*------------------   CVUpperBoundH0    --------------------------*/
/*
  This routine sets an upper bound on abs(h0) based on
  tdist = tn - t0 and the values of y[i]/y'[i].
*/
/*-----------------------------------------------------------------*/

static realtype CVUpperBoundH0(CVodeMem cv_mem, realtype tdist)
{
  booleantype vectorAtol;

  realtype atoli, hub_inv, hub;
  N_Vector temp1, temp2;

  realtype hubQ_inv;
  N_Vector tempQ1, tempQ2;

  realtype *atolSS=NULL, hubS_inv;
  N_Vector *atolSV=NULL;
  int is;
    
  vectorAtol = (itol == SV);

  temp1 = tempv;
  temp2 = acor;
  N_VAbs(zn[0], temp1);
  N_VAbs(zn[1], temp2);
  if (vectorAtol) {
    N_VLinearSum(HUB_FACTOR, temp1, ONE, (N_Vector)abstol, temp1);
  } else {
    atoli = *((realtype *) abstol);
    N_VScale(HUB_FACTOR, temp1, temp1);
    N_VAddConst(temp1, atoli, temp1);
  }
  N_VDiv(temp2, temp1, temp1);
  hub_inv = N_VMaxNorm(temp1);
  
  if (quad && (errconQ == FULL)) {
    tempQ1 = tempvQ;
    tempQ2 = acorQ;
    N_VAbs(znQ[0], tempQ1);
    N_VAbs(znQ[1], tempQ2);
    if (vectorAtol) {
      N_VLinearSum(HUB_FACTOR, tempQ1, ONE, (N_Vector)abstolQ, tempQ1);
    } else {
      atoli = *((realtype *) abstolQ);
      N_VScale(HUB_FACTOR, tempQ1, tempQ1);
      N_VAddConst(tempQ1, atoli, tempQ1);
    }
    N_VDiv(tempQ2, tempQ1, tempQ1);
    hubQ_inv = N_VMaxNorm(tempQ1);
    if (hubQ_inv > hub_inv) hub_inv = hubQ_inv;
  }

  if (sensi && (errcon == FULL)) {
    if (vectorAtol) atolSV = (N_Vector *)abstolS;
    else            atolSS = (realtype *)abstolS;
    for (is=0; is<Ns; is++) {
      N_VAbs(znS[0][is], temp1);
      N_VAbs(znS[1][is], temp2);
      if (vectorAtol) {
        N_VLinearSum(HUB_FACTOR, temp1, ONE, atolSV[is], temp1);
      } else {
        N_VScale(HUB_FACTOR, temp1, temp1);
        N_VAddConst(temp1, atolSS[is], temp1);
      }
      N_VDiv(temp2, temp1, temp1);
      hubS_inv = N_VMaxNorm(temp1);
      if (hubS_inv > hub_inv) hub_inv = hubS_inv;
    }
  }
  
  hub = HUB_FACTOR*tdist;
  if (hub*hub_inv > ONE) hub = ONE/hub_inv;
  
  return (hub);
}

/*------------------    CVYddNorm        --------------------------*/
/*
  This routine computes an estimate of the second derivative of y
  using a difference quotient, and returns its WRMS norm.
*/
/*-----------------------------------------------------------------*/

static realtype CVYddNorm(CVodeMem cv_mem, realtype hg)
{
  realtype yddnrm;
  int is;
  N_Vector wrk1, wrk2;
  
  /* y     <- h*y'(t) + y(t) */
  
  N_VLinearSum(hg, zn[1], ONE, zn[0], y);
  
  if (sensi && (errcon == FULL)) 
    for (is=0; is<Ns; is++)
      N_VLinearSum(hg, znS[1][is], ONE, znS[0][is], yS[is]);
  
  /* tempv <- f(t+h, h*y'(t)+y(t)) */

  f(tn+hg, y, tempv, f_data);
  nfe++;

  if (quad && (errconQ == FULL)) {
    fQ(tn+hg, y, tempvQ, fQ_data);
    nfQe++;
  }

  if (sensi && (errcon == FULL)) {
    wrk1 = ftemp;
    wrk2 = acor;
    CVSensRhs(cv_mem, tn+hg, y, tempv, yS, tempvS, wrk1, wrk2);
  }  

  /* tempv <- f(t+h, h*y'(t)+y(t)) - y'(t) */
  /* tempv <- ydd */
  
  N_VLinearSum(ONE, tempv, -ONE, zn[1], tempv);
  N_VScale(ONE/hg, tempv, tempv);
  
  if (quad && (errconQ == FULL)) {
    N_VLinearSum(ONE, tempvQ, -ONE, znQ[1], tempvQ);
    N_VScale(ONE/hg, tempvQ, tempvQ);
  }

  if (sensi && (errcon == FULL))
    for (is=0; is<Ns; is++) {
      N_VLinearSum(ONE, tempvS[is], -ONE, znS[1][is], tempvS[is]);
      N_VScale(ONE/hg, tempvS[is], tempvS[is]);
    }

  /* Estimate ||y''|| */
  
  yddnrm = N_VWrmsNorm(tempv, ewt);
  if (quad && (errconQ == FULL)) {
    yddnrm = CVQuadUpdateNorm(cv_mem, yddnrm, tempvQ, ewtQ);
  }
  if (sensi && (errcon == FULL)) {
    yddnrm = CVSensUpdateNorm(cv_mem, yddnrm, tempvS, ewtS);
  }

  return (yddnrm);
}

/*------------------     CVStep          --------------------------*/
/* 
  This routine performs one internal cvode step, from tn to tn + h.
  It calls other routines to do all the work.

  The main operations done here are as follows:
  * preliminary adjustments if a new step size was chosen;
  * prediction of the Nordsieck history array zn at tn + h;
  * setting of multistep method coefficients and test quantities;
  * solution of the nonlinear system;
  * testing the local error;
  * updating zn and other state data if successful;
  * resetting stepsize and order for the next step.
  * if SLDET is on, check for stability, reduce order if necessary.
  On a failure in the nonlinear system solution or error test, the
  step may be reattempted, depending on the nature of the failure.
*/
/*-----------------------------------------------------------------*/

static int CVStep(CVodeMem cv_mem)
{
  realtype saved_t, dsm, dsmS, dsmQ;
  int ncf, nef, kflag, nflag, ncfS, nefS, nefQ;
  booleantype do_sensi_stg, do_sensi_stg1, passed;
  int is;

  saved_t = tn;

  ncf = nef = 0;
  nflag = FIRST_CALL;

  if (quad) nefQ = 0;

  /* Are we computing sensitivities with a staggered approach? */
  do_sensi_stg  = (sensi && (ism==STAGGERED));
  do_sensi_stg1 = (sensi && (ism==STAGGERED1));

  if (do_sensi_stg) {
    ncfS = 0;   /* local conv. failure counter for sensitivities */
    nefS = 0;   /* local err.test failure counter for sensi. is */
  }

  if (do_sensi_stg1) {
    for (is=0; is<Ns; is++) 
      ncfS1[is] = 0;
    nefS = 0;
  }

  if ((nst > 0) && (hprime != h)) CVAdjustParams(cv_mem);

  /* Looping point for attempts to take a step */
  loop {  
    CVPredict(cv_mem);  
    CVSet(cv_mem);

    nflag = CVNls(cv_mem, nflag);
    kflag = CVHandleNFlag(cv_mem, &nflag, saved_t, &ncf, &ncfn);

    /* Go back in loop if we need to predict again */
    if (kflag == PREDICT_AGAIN) continue;

    /* Return if nonlinear solve failed and recovery not possible. */
    if (kflag != DO_ERROR_TEST) return (kflag);

    passed = CVDoErrorTest(cv_mem, &nflag, &kflag, saved_t, &nef, &dsm);

    /* Return if error test failed and recovery not possible. */
    if ((!passed) && (kflag == REP_ERR_FAIL)) return (kflag);

    /* Retry step if error test failed, nflag == PREV_ERR_FAIL */
    if (!passed) continue;

    /* passed = TRUE, kflag  = DO_ERROR_TEST, nflag  = SOLVED */

    /* Correct the quadrature variables */
    if (quad) {
      /* Save quadrature correction in acorQ */
      fQ(tn, y, acorQ, fQ_data);
      N_VLinearSum(h, acorQ, -ONE, znQ[1], acorQ);
      N_VScale(rl1, acorQ, acorQ);
      /* Apply correction to quadrature variables */
      N_VLinearSum(ONE, znQ[0], ONE, acorQ, yQ);
      /* Error test on quadratures */
      if (errconQ == FULL) {
        acnrmQ = N_VWrmsNorm(acorQ, ewtQ);
        passed = CVQuadDoErrorTest(cv_mem, &nflag, &kflag, saved_t, &nefQ, &dsmQ);
        if ((!passed) && (kflag == REP_ERR_FAIL)) return (kflag);
        if (!passed) continue;
        /* update 'dsm' with 'dsmQ' (to be used in CVPrepareNextStep) */
        dsm = CVQuadUpdateDsm(cv_mem, dsm, dsmQ);
      }
    }

    /* STAGGERED approach for sensitivities */
    if (do_sensi_stg) {
      /* Reset counters for states */
      ncf = nef = 0;
      /* Evaluate f at converged y */
      f(tn, y, ftemp, f_data);
      nfe++;
      /* Nonlinear solve for sensitivities (all-at-once) */
      nflag = CVStgrNls(cv_mem);
      kflag = CVHandleNFlag(cv_mem, &nflag, saved_t, &ncfS, &ncfnS);
      if (kflag == PREDICT_AGAIN) continue;
      if (kflag != DO_ERROR_TEST) return (kflag);
      /* Error test on sensitivities */
      if (errcon == FULL) {
        passed = CVStgrDoErrorTest(cv_mem,&nflag,&kflag,saved_t,&nefS,&dsmS);
        if ((!passed) && (kflag == REP_ERR_FAIL)) return (kflag);
        if (!passed) continue;
        /* update 'dsm' with 'dsmS' (to be used in CVPrepareNextStep) */
        dsm = CVStgrUpdateDsm(cv_mem, dsm, dsmS);
      }
    }

    /* STAGGERED1 approach for sensitivities */
    if (do_sensi_stg1) {
      /* Reset counters for states */
      ncf = nef = 0;
      /* Evaluate f at converged y */
      f(tn, y, ftemp, f_data);
      nfe++;
      /* Nonlinear solve for sensitivities (one-by-one) */
      for (is=0; is<Ns; is++) { 
        nflag = CVStgr1Nls(cv_mem, is); 
        kflag = CVHandleNFlag(cv_mem, &nflag, saved_t, &ncfS1[is], &ncfnS1[is]);
        if (kflag != DO_ERROR_TEST) break; 
      }  
      if (kflag == PREDICT_AGAIN) continue;
      if (kflag != DO_ERROR_TEST) return (kflag);
      /* Error test on sensitivities */
      if (errcon == FULL) {
        acnrmS = CVSensNorm(cv_mem, acorS, ewtS);
        passed = CVStgrDoErrorTest(cv_mem,&nflag,&kflag,saved_t,&nefS,&dsmS);
        if ((!passed) && (kflag == REP_ERR_FAIL)) return (kflag);
        if (!passed) continue;
        /* update 'dsm' with 'dsmS' (to be used in CVPrepareNextStep) */
        dsm = CVStgrUpdateDsm(cv_mem, dsm, dsmS);
      }
    }
    
    /* Everything went fine; exit loop */ 
    break;

  }

  /* Nonlinear system solve and error test were both successful.
     Update data, and consider change of step and/or order.       */

  CVCompleteStep(cv_mem); 

  CVPrepareNextStep(cv_mem, dsm); 

  /* If Stablilty Limit Detection is turned on, call stability limit
     detection routine for possible order reduction. */

  if (sldeton) CVBDFStab(cv_mem);

  etamax = (nst <= SMALL_NST) ? ETAMX2 : ETAMX3;

  /*  Finally, we rescale the acor array to be the 
      estimated local error vector. */

  N_VScale(ONE/tq[2], acor, acor);

  if (quad)
    N_VScale(ONE/tq[2], acorQ, acorQ);

  if (sensi)
    for (is=0; is<Ns; is++)
      N_VScale(ONE/tq[2], acorS[is], acorS[is]);

  return (SUCCESS_STEP);
      
}

/*------------------   CVAdjustParams    --------------------------*/
/*
  This routine is called when a change in step size was decided upon,
  and it handles the required adjustments to the history array zn.
  If there is to be a change in order, we call CVAdjustOrder and reset
  q, L = q+1, and qwait.  Then in any case, we call CVRescale, which
  resets h and rescales the Nordsieck array.
*/
/*-----------------------------------------------------------------*/

static void CVAdjustParams(CVodeMem cv_mem)
{
  if (qprime != q) {
    CVAdjustOrder(cv_mem, qprime-q);
    q = qprime;
    L = q+1;
    qwait = L;
  }
  CVRescale(cv_mem);
}

/*------------------     CVAdjustOrder   --------------------------*/
/*
  This routine is a high level routine which handles an order
  change by an amount deltaq (= +1 or -1). If a decrease in order
  is requested and q==2, then the routine returns immediately.
  Otherwise CVAdjustAdams or CVAdjustBDF is called to handle the
  order change (depending on the value of lmm).
*/
/*-----------------------------------------------------------------*/

static void CVAdjustOrder(CVodeMem cv_mem, int deltaq)
{
  if ((q==2) && (deltaq != 1)) return;
  
  switch(lmm){
  case ADAMS: 
    CVAdjustAdams(cv_mem, deltaq);
    break;
  case BDF:   
    CVAdjustBDF(cv_mem, deltaq);
    break;
  }
}

/*------------------     CVAdjustAdams   --------------------------*/
/*
  This routine adjusts the history array on a change of order q by
  deltaq, in the case that lmm == ADAMS.
*/
/*-----------------------------------------------------------------*/

static void CVAdjustAdams(CVodeMem cv_mem, int deltaq)
{
  int i, j;
  int is;
  realtype xi, hsum;

  /* On an order increase, set new column of zn to zero and return */
  
  if (deltaq==1) {
    N_VConst(ZERO, zn[L]);
    if (quad)
      N_VConst(ZERO, znQ[L]);
    if (sensi)
      for (is=0; is<Ns; is++)
        N_VConst(ZERO, znS[L][is]);
    return;
  }

  /*     
     On an order decrease, each zn[j] is adjusted by a multiple of zn[q].
     The coeffs. in the adjustment are the coeffs. of the polynomial:
          x
     q * INT { u * ( u + xi_1 ) * ... * ( u + xi_{q-2} ) } du 
          0
     where xi_j = [t_n - t_(n-j)]/h => xi_0 = 0
     
  */

  for (i=0; i <= qmax; i++) l[i] = ZERO;
  l[1] = ONE;
  hsum = ZERO;
  for (j=1; j <= q-2; j++) {
    hsum += tau[j];
    xi = hsum / hscale;
    for (i=j+1; i >= 1; i--) l[i] = l[i]*xi + l[i-1];
  }
  
  for (j=1; j <= q-2; j++) l[j+1] = q * (l[j] / (j+1));
  
  for (j=2; j < q; j++)
    N_VLinearSum(-l[j], zn[q], ONE, zn[j], zn[j]);

  if (quad)
    for (j=2; j < q; j++)
      N_VLinearSum(-l[j], znQ[q], ONE, znQ[j], znQ[j]);

  if (sensi)
    for (is=0; is<Ns; is++)
      for (j=2; j < q; j++)
        N_VLinearSum(-l[j], znS[q][is], ONE, znS[j][is], znS[j][is]);

}

/*------------------    CVAdjustBDF      --------------------------*/
/*
  This is a high level routine which handles adjustments to the
  history array on a change of order by deltaq in the case that 
  lmm == BDF.  CVAdjustBDF calls CVIncreaseBDF if deltaq = +1 and 
  CVDecreaseBDF if deltaq = -1 to do the actual work.
*/
/*-----------------------------------------------------------------*/

static void CVAdjustBDF(CVodeMem cv_mem, int deltaq)
{
  switch(deltaq) {
    case 1: 
      CVIncreaseBDF(cv_mem);
      return;
    case -1: 
      CVDecreaseBDF(cv_mem);
      return;
  }
}

/*------------------   CVIncreaseBDF     --------------------------*/
/*
  This routine adjusts the history array on an increase in the 
  order q in the case that lmm == BDF.  
  A new column zn[q+1] is set equal to a multiple of the saved 
  vector (= acor) in zn[qmax].  Then each zn[j] is adjusted by
  a multiple of zn[q+1].  The coefficients in the adjustment are the 
  coefficients of the polynomial x*x*(x+xi_1)*...*(x+xi_j),
  where xi_j = [t_n - t_(n-j)]/h.
*/
/*-----------------------------------------------------------------*/

static void CVIncreaseBDF(CVodeMem cv_mem)
{
  realtype alpha0, alpha1, prod, xi, xiold, hsum, A1;
  int i, j;
  int is;

  for (i=0; i <= qmax; i++) l[i] = ZERO;
  l[2] = alpha1 = prod = xiold = ONE;
  alpha0 = -ONE;
  hsum = hscale;
  if (q > 1) {
    for (j=1; j < q; j++) {
      hsum += tau[j+1];
      xi = hsum / hscale;
      prod *= xi;
      alpha0 -= ONE / (j+1);
      alpha1 += ONE / xi;
      for (i=j+2; i >= 2; i--) l[i] = l[i]*xiold + l[i-1];
      xiold = xi;
    }
  }
  A1 = (-alpha0 - alpha1) / prod;

  /* 
     zn[qmax] contains the value Delta_n = y_n - y_n(0) 
     This value was stored there at the previous successful
     step (in CVCompleteStep) 
     
     A1 contains dbar = (1/xi* - 1/xi_q)/prod(xi_j)
  */
  
  N_VScale(A1, zn[qmax], zn[L]);
  for (j=2; j <= q; j++)
    N_VLinearSum(l[j], zn[L], ONE, zn[j], zn[j]);

  if (quad) {
    N_VScale(A1, znQ[qmax], znQ[L]);
    for (j=2; j <= q; j++)
      N_VLinearSum(l[j], znQ[L], ONE, znQ[j], znQ[j]);
  }

  if (sensi) {
    for (is=0; is<Ns; is++) {
      N_VScale(A1, znS[qmax][is], znS[L][is]);
      for (j=2; j <= q; j++)
        N_VLinearSum(l[j], znS[L][is], ONE, znS[j][is], znS[j][is]);
    }
  }

}

/*------------------    CVDecreaseBDF    --------------------------*/
/*
  This routine adjusts the history array on a decrease in the 
  order q in the case that lmm == BDF.  
  Each zn[j] is adjusted by a multiple of zn[q].  The coefficients
  in the adjustment are the coefficients of the polynomial
  x*x*(x+xi_1)*...*(x+xi_j), where xi_j = [t_n - t_(n-j)]/h.
*/
/*-----------------------------------------------------------------*/

static void CVDecreaseBDF(CVodeMem cv_mem)
{
  realtype hsum, xi;
  int i, j;
  int is;
  
  for (i=0; i <= qmax; i++) l[i] = ZERO;
  l[2] = ONE;
  hsum = ZERO;
  for (j=1; j <= q-2; j++) {
    hsum += tau[j];
    xi = hsum /hscale;
    for (i=j+2; i >= 2; i--) l[i] = l[i]*xi + l[i-1];
  }
  
  for (j=2; j < q; j++)
    N_VLinearSum(-l[j], zn[q], ONE, zn[j], zn[j]);

  if (quad) {
    for (j=2; j < q; j++)
      N_VLinearSum(-l[j], znQ[q], ONE, znQ[j], znQ[j]);
  }

  if (sensi) {
    for (is=0; is<Ns; is++) 
      for (j=2; j < q; j++)
        N_VLinearSum(-l[j], znS[q][is], ONE, znS[j][is], znS[j][is]);
  }
}

/*------------------    CVRescale        --------------------------*/
/*
  This routine rescales the Nordsieck array by multiplying the
  jth column zn[j] by eta^j, j = 1, ..., q.  Then the value of
  h is rescaled by eta, and hscale is reset to h.
*/
/*-----------------------------------------------------------------*/

static void CVRescale(CVodeMem cv_mem)
{
  int j;
  int is;
  realtype factor;

  factor = eta;
  for (j=1; j <= q; j++) {

    N_VScale(factor, zn[j], zn[j]);

    if (quad)
      N_VScale(factor, znQ[j], znQ[j]);

    if (sensi)
      for (is=0; is<Ns; is++)
        N_VScale(factor, znS[j][is], znS[j][is]);

    factor *= eta;

  }
  h = hscale * eta;
  hscale = h;
  nscon = 0;
}

/*------------------    CVPredict        --------------------------*/
/*
  This routine advances tn by the tentative step size h, and computes
  the predicted array z_n(0), which is overwritten on zn.  The
  prediction of zn is done by repeated additions.
*/
/*-----------------------------------------------------------------*/

static void CVPredict(CVodeMem cv_mem)
{
  int j, k;
  int is;

  tn += h;

  for (k = 1; k <= q; k++)
    for (j = q; j >= k; j--) 
      N_VLinearSum(ONE, zn[j-1], ONE, zn[j], zn[j-1]); 

  if (quad) {
    for (k = 1; k <= q; k++)
      for (j = q; j >= k; j--) 
        N_VLinearSum(ONE, znQ[j-1], ONE, znQ[j], znQ[j-1]);
  }

  if (sensi) {
    for (is=0; is<Ns; is++) {
      for (k = 1; k <= q; k++)
        for (j = q; j >= k; j--) 
          N_VLinearSum(ONE, znS[j-1][is], ONE, znS[j][is], znS[j-1][is]);
    }
  }
}

/*------------------       CVSet         --------------------------*/
/*
  This routine is a high level routine which calls CVSetAdams or
  CVSetBDF to set the polynomial l, the test quantity array tq, 
  and the related variables  rl1, gamma, and gamrat.
*/
/*-----------------------------------------------------------------*/

static void CVSet(CVodeMem cv_mem)
{
  switch(lmm) {
  case ADAMS: 
    CVSetAdams(cv_mem);
    break;
  case BDF: 
    CVSetBDF(cv_mem);
    break;
  }
  rl1 = ONE / l[1];
  gamma = h * rl1;
  if (nst == 0) gammap = gamma;
  gamrat = (nst > 0) ? gamma / gammap : ONE;  /* protect x / x != 1.0 */
}

/*------------------     CVSetAdams      --------------------------*/
/*
  This routine handles the computation of l and tq for the
  case lmm == ADAMS.

  The components of the array l are the coefficients of a
  polynomial Lambda(x) = l_0 + l_1 x + ... + l_q x^q, given by
                           q-1
  (d/dx) Lambda(x) = c * PRODUCT (1 + x / xi_i) , where
                           i=1
   Lambda(-1) = 0, Lambda(0) = 1, and c is a normalization factor.
   Here xi_i = [t_n - t_(n-i)] / h.

   The array tq is set to test quantities used in the convergence
   test, the error test, and the selection of h at a new order.
*/
/*-----------------------------------------------------------------*/

static void CVSetAdams(CVodeMem cv_mem)
{
  realtype m[L_MAX], M[3], hsum;
  
  if (q == 1) {
    l[0] = l[1] = tq[1] = tq[5] = ONE;
    tq[2] = TWO;
    tq[3] = TWELVE;
    tq[4] = nlscoef * tq[2];       /* = 0.1 * tq[2] */
    return;
  }
  
  hsum = CVAdamsStart(cv_mem, m);
  
  M[0] = CVAltSum(q-1, m, 1);
  M[1] = CVAltSum(q-1, m, 2);
  
  CVAdamsFinish(cv_mem, m, M, hsum);
}

/*------------------    CVAdamsStart     --------------------------*/
/*
  This routine generates in m[] the coefficients of the product
  polynomial needed for the Adams l and tq coefficients for q > 1.
*/
/*-----------------------------------------------------------------*/

static realtype CVAdamsStart(CVodeMem cv_mem, realtype m[])
{
  realtype hsum, xi_inv, sum;
  int i, j;
  
  hsum = h;
  m[0] = ONE;
  for (i=1; i <= q; i++) m[i] = ZERO;
  for (j=1; j < q; j++) {
    if ((j==q-1) && (qwait == 1)) {
      sum = CVAltSum(q-2, m, 2);
      tq[1] = m[q-2] / (q * sum);
    }
    xi_inv = h / hsum;
    for (i=j; i >= 1; i--) m[i] += m[i-1] * xi_inv;
    hsum += tau[j];
    /* The m[i] are coefficients of product(1 to j) (1 + x/xi_i) */
  }
  return (hsum);
}

/*------------------    CVAdamsFinish    --------------------------*/
/*
  This routine completes the calculation of the Adams l and tq.
*/
/*-----------------------------------------------------------------*/

static void CVAdamsFinish(CVodeMem cv_mem, realtype m[], realtype M[], realtype hsum)
{
  int i;
  realtype M0_inv, xi, xi_inv;
  
  M0_inv = ONE / M[0];
  
  l[0] = ONE;
  for (i=1; i <= q; i++) l[i] = M0_inv * (m[i-1] / i);
  xi = hsum / h;
  xi_inv = ONE / xi;
  
  tq[2] = xi * M[0] / M[1];
  tq[5] = xi / l[q];

  if (qwait == 1) {
    for (i=q; i >= 1; i--) m[i] += m[i-1] * xi_inv;
    M[2] = CVAltSum(q, m, 2);
    tq[3] = L * M[0] / M[2];
  }

  tq[4] = nlscoef * tq[2];
}

/*------------------     CVAltSum        --------------------------*/
/*  
  CVAltSum returns the value of the alternating sum
     sum (i= 0 ... iend) [ (-1)^i * (a[i] / (i + k)) ].
  If iend < 0 then CVAltSum returns 0.
  This operation is needed to compute the integral, from -1 to 0,
  of a polynomial x^(k-1) M(x) given the coefficients of M(x).
*/
/*-----------------------------------------------------------------*/

static realtype CVAltSum(int iend, realtype a[], int k)
{
  int i, sign;
  realtype sum;
  
  if (iend < 0) return (ZERO);
  
  sum = ZERO;
  sign = 1;
  for (i=0; i <= iend; i++) {
    sum += sign * (a[i] / (i+k));
    sign = -sign;
  }
  return (sum);
}

/*------------------     CVSetBDF        --------------------------*/
/*
  This routine computes the coefficients l and tq in the case
  lmm == BDF.  CVSetBDF calls CVSetTqBDF to set the test
  quantity array tq. 
  
  The components of the array l are the coefficients of a
  polynomial Lambda(x) = l_0 + l_1 x + ... + l_q x^q, given by
                                  q-1
  Lambda(x) = (1 + x / xi*_q) * PRODUCT (1 + x / xi_i) , where
                                  i=1
  xi_i = [t_n - t_(n-i)] / h.

  The array tq is set to test quantities used in the convergence
  test, the error test, and the selection of h at a new order.
*/
/*-----------------------------------------------------------------*/

static void CVSetBDF(CVodeMem cv_mem)
{
  realtype alpha0, alpha0_hat, xi_inv, xistar_inv, hsum;
  int i,j;
  
  l[0] = l[1] = xi_inv = xistar_inv = ONE;
  for (i=2; i <= q; i++) l[i] = ZERO;
  alpha0 = alpha0_hat = -ONE;
  hsum = h;
  if (q > 1) {
    for (j=2; j < q; j++) {
      hsum += tau[j-1];
      xi_inv = h / hsum;
      alpha0 -= ONE / j;
      for (i=j; i >= 1; i--) l[i] += l[i-1]*xi_inv;
      /* The l[i] are coefficients of product(1 to j) (1 + x/xi_i) */
    }
    
    /* j = q */
    alpha0 -= ONE / q;
    xistar_inv = -l[1] - alpha0;
    hsum += tau[q-1];
    xi_inv = h / hsum;
    alpha0_hat = -l[1] - xi_inv;
    for (i=q; i >= 1; i--) l[i] += l[i-1]*xistar_inv;
  }

  CVSetTqBDF(cv_mem, hsum, alpha0, alpha0_hat, xi_inv, xistar_inv);
}

/*------------------    CVSetTqBDF       --------------------------*/
/*
  This routine sets the test quantity array tq in the case
  lmm == BDF.
*/
/*-----------------------------------------------------------------*/

static void CVSetTqBDF(CVodeMem cv_mem, realtype hsum, realtype alpha0,
                       realtype alpha0_hat, realtype xi_inv, realtype xistar_inv)
{
  realtype A1, A2, A3, A4, A5, A6;
  realtype C, CPrime, CPrimePrime;
  
  A1 = ONE - alpha0_hat + alpha0;
  A2 = ONE + q * A1;
  tq[2] = ABS(alpha0 * (A2 / A1));
  tq[5] = ABS((A2) / (l[q] * xi_inv/xistar_inv));
  if (qwait == 1) {
    C = xistar_inv / l[q];
    A3 = alpha0 + ONE / q;
    A4 = alpha0_hat + xi_inv;
    CPrime = A3 / (ONE - A4 + A3);
    tq[1] = ABS(CPrime / C);
    hsum += tau[q];
    xi_inv = h / hsum;
    A5 = alpha0 - (ONE / (q+1));
    A6 = alpha0_hat - xi_inv;
    CPrimePrime = A2 / (ONE - A6 + A5);
    tq[3] = ABS(CPrimePrime * xi_inv * (q+2) * A5);
  }
  tq[4] = nlscoef * tq[2];
}

/*------------------       CVNls         --------------------------*/
/*
  This routine attempts to solve the nonlinear system associated
  with a single implicit step of the linear multistep method.
  Depending on iter, it calls CVNlsFunctional or CVNlsNewton
  to do the work.
*/
/*-----------------------------------------------------------------*/

static int CVNls(CVodeMem cv_mem, int nflag)
{
  int flag=SOLVED;

  switch(iter) {
  case FUNCTIONAL: 
    flag = CVNlsFunctional(cv_mem);
    break;
  case NEWTON: 
    flag = CVNlsNewton(cv_mem, nflag);
    break;
  }
  
  return(flag);

}

/*------------------   CVNlsFunctional   --------------------------*/
/*
  This routine attempts to solve the nonlinear system using 
  functional iteration (no matrices involved).
  
  This routine also handles the functional iteration of the
  combined system (states + sensitivities) when sensitivities are 
  computed using the SIMULTANEOUS approach.
*/
/*-----------------------------------------------------------------*/

static int CVNlsFunctional(CVodeMem cv_mem)
{
  int m;
  realtype del, delS, Del, Delp, dcon;
  int is;
  booleantype do_sensi_sim;
  N_Vector wrk1, wrk2;

  /* Are we computing sensitivities with the SIMULTANEOUS approach? */
  do_sensi_sim = (sensi && (ism==SIMULTANEOUS));

  /* Initialize counter and evaluate f at predicted y */
  crate = ONE;
  m = 0;

  f(tn, zn[0], tempv, f_data);
  nfe++;

  if (do_sensi_sim) {
    wrk1 = ftemp;
    wrk2 = ftempS[0];
    CVSensRhs(cv_mem, tn, zn[0], tempv, znS[0], tempvS, wrk1, wrk2);
  }

  /* Initialize correction to zero */

  N_VConst(ZERO, acor);
  if (do_sensi_sim) {
    for (is=0; is<Ns; is++)
      N_VConst(ZERO,acorS[is]);
  }

  /* Loop until convergence; accumulate corrections in acor */

  loop {

    nni++;

    /* Correct y directly from the last f value */

    N_VLinearSum(h, tempv, -ONE, zn[1], tempv);
    N_VScale(rl1, tempv, tempv);
    N_VLinearSum(ONE, zn[0], ONE, tempv, y);

    if (do_sensi_sim)
      for (is=0; is<Ns; is++) {
        N_VLinearSum(h, tempvS[is], -ONE, znS[1][is], tempvS[is]);
        N_VScale(rl1, tempvS[is], tempvS[is]);
        N_VLinearSum(ONE, znS[0][is], ONE, tempvS[is], yS[is]);
      }
    
    /* Get WRMS norm of current correction to use in convergence test */

    N_VLinearSum(ONE, tempv, -ONE, acor, acor);
    if (do_sensi_sim)
      for (is=0; is<Ns; is++)
        N_VLinearSum(ONE, tempvS[is], -ONE, acorS[is], acorS[is]);

    del = N_VWrmsNorm(acor, ewt);
    if (do_sensi_sim)
      delS = CVSensUpdateNorm(cv_mem, del, acorS, ewtS);

    N_VScale(ONE, tempv, acor);
    if (do_sensi_sim) 
      for (is=0; is<Ns; is++)
        N_VScale(ONE, tempvS[is], acorS[is]);
    
    /* Test for convergence.  If m > 0, an estimate of the convergence
       rate constant is stored in crate, and used in the test. 

       Recall that, even when errcon=PARTIAL, all variables are used in the
       convergence test. Hence, we use Del (and not del). However, acnrm
       is used in the error test and thus it has different forms
       depending on errcon (and this explains why we have to carry around
       del and delS)
    */
    
    Del = (do_sensi_sim) ? delS : del;
    if (m > 0) crate = MAX(CRDOWN * crate, Del / Delp);
    dcon = Del * MIN(ONE, crate) / tq[4];

    if (dcon <= ONE) {
      if (m == 0)
        if (do_sensi_sim && (errcon == FULL)) acnrm = delS;
        else                                  acnrm = del;
      else {
        acnrm = N_VWrmsNorm(acor, ewt);
        if (do_sensi_sim && (errcon == FULL))
          acnrm = CVSensUpdateNorm(cv_mem, acnrm, acorS, ewtS);
      }
      return (SOLVED);  /* Convergence achieved */
    }

    /* Stop at maxcor iterations or if iter. seems to be diverging */

    m++;
    if ((m==maxcor) || ((m >= 2) && (Del > RDIV * Delp)))
      return (CONV_FAIL);

    /* Save norm of correction, evaluate f, and loop again */

    Delp = Del;

    f(tn, y, tempv, f_data);
    nfe++;

    if (do_sensi_sim) {
      wrk1 = ftemp;
      wrk2 = ftempS[0];
      CVSensRhs(cv_mem, tn, y, tempv, yS, tempvS, wrk1, wrk2);
    }      

  } /* end loop */

}

/*------------------    CVNlsNewton      --------------------------*/
/*
  This routine handles the Newton iteration. It calls lsetup if 
  indicated, calls CVNewtonIteration to perform the iteration, and 
  retries a failed attempt at Newton iteration if that is indicated.
  See return values at top of this file.
  
  This routine also handles the Newton iteration of the combined 
  system when sensitivities are computed using the SIMULTANEOUS
  approach. Since in that case we use a quasi-Newton on the 
  combined system (by approximating the Jacobian matrix by its
  block diagonal) and thus only solve linear systems with 
  multiple right hand sides (all sharing the same coefficient
  matrix - whatever iteration matrix we decide on) we set-up
  the linear solver to handle N equations at a time.
*/
/*-----------------------------------------------------------------*/

static int CVNlsNewton(CVodeMem cv_mem, int nflag)
{
  N_Vector vtemp1, vtemp2, vtemp3, wrk1, wrk2;
  int convfail, ier;
  booleantype callSetup, do_sensi_sim;
  int is;
  
  /* Are we computing sensitivities with the SIMULTANEOUS approach? */
  do_sensi_sim = (sensi && (ism==SIMULTANEOUS));

  vtemp1 = acor;  /* rename acor as vtemp1 for readability  */
  vtemp2 = y;     /* rename y as vtemp2 for readability     */
  vtemp3 = tempv; /* rename tempv as vtemp3 for readability */
  
  /* Set flag convfail, input to lsetup for its evaluation decision */
  convfail = ((nflag == FIRST_CALL) || (nflag == PREV_ERR_FAIL)) ?
    NO_FAILURES : FAIL_OTHER;

  /* Decide whether or not to call setup routine (if one exists) */
  if (setupNonNull) {      
    callSetup = (nflag == PREV_CONV_FAIL) || (nflag == PREV_ERR_FAIL) ||
      (nst == 0) || (nst >= nstlp + MSBP) || (ABS(gamrat-ONE) > DGMAX);

    /* Decide whether to force a call to setup */
    if (forceSetup) {
      callSetup = TRUE;
      convfail = FAIL_OTHER;
    }

  } else {  
    crate = ONE;
    crateS = ONE;  /* if NO lsetup all conv. rates are set to ONE */
    callSetup = FALSE;
  }
  
  /* Looping point for the solution of the nonlinear system.
     Evaluate f at the predicted y, call lsetup if indicated, and
     call CVNewtonIteration for the Newton iteration itself.      */

  loop {

    f(tn, zn[0], ftemp, f_data);
    nfe++; 

    if (do_sensi_sim) {
      wrk1 = tempv;
      wrk2 = tempvS[0];
      CVSensRhs(cv_mem, tn, zn[0], ftemp, znS[0], ftempS, wrk1, wrk2);
    }

    if (callSetup) {
      ier = lsetup(cv_mem, convfail, zn[0], ftemp, &jcur, 
                   vtemp1, vtemp2, vtemp3);
      nsetups++;
      callSetup = FALSE;
      forceSetup = FALSE;
      gamrat = ONE; 
      gammap = gamma;
      crate = ONE;
      crateS = ONE; /* after lsetup all conv. rates are reset to ONE */
      nstlp = nst;
      /* Return if lsetup failed */
      if (ier < 0) return (SETUP_FAIL_UNREC);
      if (ier > 0) return (CONV_FAIL);
    }

    /* Set acor to zero and load prediction into y vector */
    N_VConst(ZERO, acor);
    N_VScale(ONE, zn[0], y);

    if (do_sensi_sim)
      for (is=0; is<Ns; is++) {
        N_VConst(ZERO, acorS[is]);
        N_VScale(ONE, znS[0][is], yS[is]);
      }

    /* Do the Newton iteration */
    ier = CVNewtonIteration(cv_mem);

    /* If there is a convergence failure and the Jacobian-related 
       data appears not to be current, loop again with a call to lsetup
       in which convfail=FAIL_BAD_J.  Otherwise return.                 */
    if (ier != TRY_AGAIN) return (ier);
    
    callSetup = TRUE;
    convfail = FAIL_BAD_J;
  }
}

/*------------------  CVNewtonIteration  --------------------------*/
/*
  This routine performs the Newton iteration. If the iteration succeeds,
  it returns the value SOLVED. If not, it may signal the CVNlsNewton 
  routine to call lsetup again and reattempt the iteration, by
  returning the value TRY_AGAIN. (In this case, CVNlsNewton must set 
  convfail to FAIL_BAD_J before calling setup again). 
  Otherwise, this routine returns one of the appropriate values 
  SOLVE_FAIL_UNREC or CONV_FAIL back to CVNlsNewton.
  
  If sensitivities are computed using the SIMULTANEOUS approach, this
  routine performs a quasi-Newton on the combined nonlinear system.
  The iteration matrix of the combined system is block diagonal with
  each block being the iteration matrix of the original system. Thus
  we solve linear systems with the same matrix but different right
  hand sides.
*/
/*-----------------------------------------------------------------*/

static int CVNewtonIteration(CVodeMem cv_mem)
{
  int m, ret;
  realtype del, delS, Del, Delp, dcon;
  N_Vector b, *bS=NULL, wrk1, wrk2;
  booleantype do_sensi_sim;
  int is;
  
  /* Are we computing sensitivities with the SIMULTANEOUS approach? */
  do_sensi_sim = (sensi && (ism==SIMULTANEOUS));

  mnewt = m = 0;
  
  /* At this point, ftemp  <- f(t_n, y_n(0))
                    ftempS <- fS(t_n, y_n(0), s_n(0))
                    acor   <- 0
                    acorS  <- 0
                    y      <- y_n(0)
                    yS     <- yS_n(0)                 */

  /* Looping point for Newton iteration */
  loop {
    
    /* Evaluate the residual of the nonlinear system */
    N_VLinearSum(rl1, zn[1], ONE, acor, tempv);
    N_VLinearSum(gamma, ftemp, -ONE, tempv, tempv);

    /* Call the lsolve function */
    b = tempv;
    ret = lsolve(cv_mem, b, ewt, y, ftemp); 
    nni++;

    if (ret < 0) return (SOLVE_FAIL_UNREC);
    
    /* If lsolve had a recoverable failure and Jacobian data is
       not current, signal to try the solution again            */
    if (ret > 0) { 
      if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
      return (CONV_FAIL);
    }

    /* Solve the sensitivity linear systems and do the same 
       tests on the return value of lsolve. */
 
    if (do_sensi_sim) {

      for (is=0; is<Ns; is++) {
        N_VLinearSum(rl1, znS[1][is], ONE, acorS[is], tempvS[is]);
        N_VLinearSum(gamma, ftempS[is], -ONE, tempvS[is], tempvS[is]);
      }
      bS = tempvS;
      for (is=0; is<Ns; is++) {
        ret = lsolve(cv_mem, bS[is], ewtS[is], y, ftemp);
        if (ret < 0) return (SOLVE_FAIL_UNREC);
        if (ret > 0) { 
          if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
          return (CONV_FAIL);
        }
      }
    }
    
    /* Get WRMS norm of correction; add correction to acor and y */

    del = N_VWrmsNorm(b, ewt);
    N_VLinearSum(ONE, acor, ONE, b, acor);
    N_VLinearSum(ONE, zn[0], ONE, acor, y);

    if (do_sensi_sim) {
      delS = CVSensUpdateNorm(cv_mem, del, bS, ewtS);
      for (is=0; is<Ns; is++) {
        N_VLinearSum(ONE, acorS[is], ONE, bS[is], acorS[is]);
        N_VLinearSum(ONE, znS[0][is], ONE, acorS[is], yS[is]);
      }
    }

    /* Test for convergence.  If m > 0, an estimate of the convergence
       rate constant is stored in crate, and used in the test.        */

    Del = (do_sensi_sim) ? delS : del;
    if (m > 0) crate = MAX(CRDOWN * crate, Del/Delp);
    dcon = Del * MIN(ONE, crate) / tq[4];
    
    if (dcon <= ONE) {
      if (m == 0)
        if (do_sensi_sim && (errcon == FULL)) acnrm = delS;
        else                                  acnrm = del;
      else {
        acnrm = N_VWrmsNorm(acor, ewt);
        if (do_sensi_sim && (errcon == FULL))
          acnrm = CVSensUpdateNorm(cv_mem, acnrm, acorS, ewtS);
      }
      jcur = FALSE;
      return (SOLVED);  /* Convergence achieved */
    }

    mnewt = ++m;
    
    /* Stop at maxcor iterations or if iter. seems to be diverging.
       If still not converged and Jacobian data is not current, 
       signal to try the solution again                            */
    if ((m == maxcor) || ((m >= 2) && (Del > RDIV * Delp))) {
      if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
      return (CONV_FAIL);
    }
    
    /* Save norm of correction, evaluate f, and loop again */
    Delp = Del;
    f(tn, y, ftemp, f_data);
    nfe++;

    if (do_sensi_sim) {
      wrk1 = tempv;
      wrk2 = tempvS[0];
      CVSensRhs(cv_mem, tn, y, ftemp, yS, ftempS, wrk1, wrk2);
    }

  } /* end loop */

}

/*------------------   CVHandleFlag      --------------------------*/
/*
  This routine takes action on the return value nflag = *nflagPtr
  returned by CVNls, as follows:
  
  If CVNls succeeded in solving the nonlinear system, then
  CVHandleNFlag returns the constant DO_ERROR_TEST, which tells CVStep
  to perform the error test.
  
  If the nonlinear system was not solved successfully, then ncfn and
  ncf = *ncfPtr are incremented and Nordsieck array zn is restored.
  
  If the solution of the nonlinear system failed due to an
  unrecoverable failure by setup, we return the value SETUP_FAILED.
  
  If it failed due to an unrecoverable failure in solve, then we return
  the value SOLVE_FAILED.
  
  Otherwise, a recoverable failure occurred when solving the 
  nonlinear system (CVNls returned nflag == CONV_FAIL). 
  In this case, we return the value REP_CONV_FAIL if ncf is now
  equal to maxncf or |h| = hmin. 
  If not, we set *nflagPtr = PREV_CONV_FAIL and return the value
  PREDICT_AGAIN, telling CVStep to reattempt the step.
*/
/*-----------------------------------------------------------------*/

static int CVHandleNFlag(CVodeMem cv_mem, int *nflagPtr, realtype saved_t,
                         int *ncfPtr, int *ncfnPtr)
{
  int nflag;
  
  nflag = *nflagPtr;
  
  if (nflag == SOLVED) return (DO_ERROR_TEST);

  /* The nonlinear soln. failed; increment ncfn and restore zn */
  (*ncfnPtr)++;
  CVRestore(cv_mem, saved_t);
  
  /* Return if lsetup or lsolve failed unrecoverably */
  if (nflag == SETUP_FAIL_UNREC) return (SETUP_FAILED);
  if (nflag == SOLVE_FAIL_UNREC) return (SOLVE_FAILED);
  
  /* At this point, nflag == CONV_FAIL; increment ncf */
  
  (*ncfPtr)++;
  etamax = ONE;
  /* If we had maxncf failures or |h| = hmin, return REP_CONV_FAIL */
  if ((ABS(h) <= hmin*ONEPSM) || (*ncfPtr == maxncf))
    return (REP_CONV_FAIL);

  /* Reduce step size; return to reattempt the step */
  eta = MAX(ETACF, hmin / ABS(h));
  *nflagPtr = PREV_CONV_FAIL;
  CVRescale(cv_mem);
  return (PREDICT_AGAIN);
}

/*------------------    CVRestore        --------------------------*/
/*
  This routine restores the value of tn to saved_t and undoes the
  prediction.  After execution of CVRestore, the Nordsieck array zn has
  the same values as before the call to CVPredict.
*/
/*-----------------------------------------------------------------*/


static void CVRestore(CVodeMem cv_mem, realtype saved_t)
{
  int j, k;
  int is;

  tn = saved_t;
  for (k = 1; k <= q; k++)
    for (j = q; j >= k; j--)
      N_VLinearSum(ONE, zn[j-1], -ONE, zn[j], zn[j-1]);

  if (quad) {
    for (k = 1; k <= q; k++)
      for (j = q; j >= k; j--)
        N_VLinearSum(ONE, znQ[j-1], -ONE, znQ[j], znQ[j-1]);
  }

  if (sensi) {
    for (is=0; is<Ns; is++) {
      for (k = 1; k <= q; k++)
        for (j = q; j >= k; j--)
          N_VLinearSum(ONE, znS[j-1][is], -ONE, znS[j][is], znS[j-1][is]);
    }
  }
}

/*------------------    CVDoErrorTest    --------------------------*/
/*
  This routine performs the local error test. 
  The weighted local error norm dsm is loaded into *dsmPtr, and 
  the test dsm ?<= 1 is made.
  
  If the test passes, CVDoErrorTest returns TRUE. 

  If the test fails, we undo the step just taken (call CVRestore), 
  set *nflagPtr to PREV_ERR_FAIL, and return FALSE. 
  
  If maxnef error test failures have occurred or if ABS(h) = hmin,
  we set *kflagPtr = REP_ERR_FAIL. (Otherwise *kflagPtr has the
  value last returned by CVHandleNflag.)
  
  If more than MXNEF1 error test failures have occurred, an order
  reduction is forced. If already at order 1 restart by reloading 
  zn from scratch. Note that if sensitivities are computed, znS is
  also reloaded, no matter what 'ism' or 'errcon' are. Same for 
  quadratures.
*/
/*-----------------------------------------------------------------*/

static booleantype CVDoErrorTest(CVodeMem cv_mem, int *nflagPtr, 
                                 int *kflagPtr, realtype saved_t, 
                                 int *nefPtr, realtype *dsmPtr)
{
  realtype dsm;
  int is;
  N_Vector wrk1, wrk2;

  dsm = acnrm / tq[2];

  /* If est. local error norm dsm passes test, return TRUE */  
  *dsmPtr = dsm; 
  if (dsm <= ONE) return (TRUE);
  
  /* Test failed; increment counters, set nflag, and restore zn array */
  (*nefPtr)++;
  netf++;
  *nflagPtr = PREV_ERR_FAIL;
  CVRestore(cv_mem, saved_t);

  /* At maxnef failures or |h| = hmin, return with kflag = REP_ERR_FAIL */
  if ((ABS(h) <= hmin*ONEPSM) || (*nefPtr == maxnef)) {
    *kflagPtr = REP_ERR_FAIL;
    return (FALSE);
  }

  /* Set etamax = 1 to prevent step size increase at end of this step */
  etamax = ONE;

  /* Set h ratio eta from dsm, rescale, and return for retry of step */
  if (*nefPtr <= MXNEF1) {
    eta = ONE / (RPowerR(BIAS2*dsm,ONE/L) + ADDON);
    eta = MAX(ETAMIN, MAX(eta, hmin / ABS(h)));
    if (*nefPtr >= SMALL_NEF) eta = MIN(eta, ETAMXF);
    CVRescale(cv_mem);
    return (FALSE);
  }
  
  /* After MXNEF1 failures, force an order reduction and retry step */
  if (q > 1) {
    eta = MAX(ETAMIN, hmin / ABS(h));
    CVAdjustOrder(cv_mem,-1);
    L = q;
    q--;
    qwait = L;
    CVRescale(cv_mem);
    return (FALSE);
  }

  /* If already at order 1, restart: reload zn from scratch */
  eta = MAX(ETAMIN, hmin / ABS(h));
  h *= eta;
  hscale = h;
  qwait = LONG_WAIT;
  nscon = 0;

  f(tn, zn[0], tempv, f_data);
  nfe++;
  N_VScale(h, tempv, zn[1]);

  if (quad) {
    fQ(tn, zn[0], tempvQ, fQ_data);
    nfQe++;
    N_VScale(h, tempvQ, znQ[1]);
  }

  if (sensi) {
    wrk1 = ftemp;
    wrk2 = ftempS[0];
    CVSensRhs(cv_mem, tn, zn[0], tempv, znS[0], tempvS, wrk1, wrk2);
    for (is=0; is<Ns; is++) 
      N_VScale(h, tempvS[is], znS[1][is]);
  }
  
  return (FALSE);
}

/*------------------   CVQuadDoErrorTest --------------------------*/
/*
  This routine performs the local error test on quadrature variables. 
  The weighted local error norm dsm is loaded into *dsmPtr, and 
  the test dsm ?<= 1 is made.
  
  If the test passes, CVDoErrorTest returns TRUE. 
  
  If the test fails, we undo the step just taken (call CVRestore), 
  set *nflagPtr to PREV_ERR_FAIL, and return FALSE. 
  
  If maxnef error test failures have occurred or if ABS(h) = hmin,
  we set *kflagPtr = REP_ERR_FAIL. (Otherwise *kflagPtr has the
  value last returned by CVHandleNflag.)
  
  If more than MXNEF1 error test failures have occurred, an order
  reduction is forced. If already at order 1 restart by reloading 
  zn and znQ from scratch. If sensitivities are computed, znS is
  also reloaded.
*/
/*-----------------------------------------------------------------*/

static booleantype CVQuadDoErrorTest(CVodeMem cv_mem, int *nflagPtr, 
                                     int *kflagPtr, realtype saved_t, 
                                     int *nefQPtr, realtype *dsmQPtr)
{
  realtype dsmQ;
  int is;
  N_Vector wrk1, wrk2;

  dsmQ = acnrmQ / tq[2];

  /* If dsmQ passes the test, return TRUE */
  *dsmQPtr = dsmQ; 
  if (dsmQ <= ONE) return (TRUE);

  /* Test failed; increment counters, set nflag, and restore zn array */
  (*nefQPtr)++;
  netfQ++;
  *nflagPtr = PREV_ERR_FAIL;
  CVRestore(cv_mem, saved_t);

  /* At maxnef failures or |h| = hmin, return with kflag = REP_ERR_FAIL */
  if ((ABS(h) <= hmin*ONEPSM) || (*nefQPtr == maxnef)) {
    *kflagPtr = REP_ERR_FAIL;
    return (FALSE);
  }

  /* Set etamax = 1 to prevent step size increase at end of this step */
  etamax = ONE;

  /* Set h ratio eta from dsmQ, rescale, and return for retry of step */
  if (*nefQPtr <= MXNEF1) {
    eta = ONE / (RPowerR(BIAS2*dsmQ,ONE/L) + ADDON);
    eta = MAX(ETAMIN, MAX(eta, hmin / ABS(h)));
    if (*nefQPtr >= SMALL_NEF) eta = MIN(eta, ETAMXF);
    CVRescale(cv_mem);
    return (FALSE);
  }

  /* After MXNEF1 failures, force an order reduction and retry step */
  if (q > 1) {
    eta = MAX(ETAMIN, hmin / ABS(h));
    CVAdjustOrder(cv_mem,-1);
    L = q;
    q--;
    qwait = L;
    CVRescale(cv_mem);
    return (FALSE);
  }

  /* If already at order 1, restart: reload zn and znQ from scratch */
  eta = MAX(ETAMIN, hmin / ABS(h));
  h *= eta;
  hscale = h;
  qwait = LONG_WAIT;
  nscon = 0;

  f(tn, zn[0], tempv, f_data);
  nfe++;
  N_VScale(h, tempv, zn[1]);

  fQ(tn, zn[0], tempvQ, fQ_data);
  nfQe++;
  N_VScale(h, tempvQ, znQ[1]);

  if (sensi) {
    wrk1 = ftemp;
    wrk2 = ftempS[0];
    CVSensRhs(cv_mem, tn, zn[0], tempv, znS[0], tempvS, wrk1, wrk2);
    for (is=0; is<Ns; is++) 
      N_VScale(h, tempvS[is], znS[1][is]);
  }
  
  return (FALSE);

}

/*=================================================================*/
/*BEGIN        Routines for STAGGERED and STAGGERED1               */
/*=================================================================*/

/*------------------     CVStgrNls       --------------------------*/
/*
  STAGGERED approach

  This is a high-level routine that attempts to solve the 
  sensitivity linear systems using nonlinear iterations (FUNCTIONAL
  or NEWTON - depending on the value of iter) once the states y_n
  were obtained and passed the error test.
*/
/*-----------------------------------------------------------------*/

static int CVStgrNls(CVodeMem cv_mem)
{
  int flag=SOLVED;

  switch(iter) {
  case FUNCTIONAL: 
    flag = CVStgrNlsFunctional(cv_mem);
    break;
  case NEWTON:     
    flag = CVStgrNlsNewton(cv_mem);
    break;
  }

  return(flag);

}

/*------------------  CVStgrNlsFunctional -------------------------*/
/*
  STAGGERED approach

  This routine attempts to solve the sensitivity linear systems 
  using functional iteration (no matrices involved).
  
  Possible return values:
    SOLVED
    CONV_FAIL
*/
/*-----------------------------------------------------------------*/

static int CVStgrNlsFunctional(CVodeMem cv_mem)
{
  int m;
  int is;
  realtype Del, Delp, dcon;
  N_Vector wrk1, wrk2;

  /* Initialize estimated conv. rate and counter */
  crateS = ONE;
  m = 0;

  /* Evaluate fS at predicted yS but with converged y (and corresponding f) */
  wrk1 = tempv;
  wrk2 = ftempS[0];
  CVSensRhs(cv_mem, tn, y, ftemp, znS[0], tempvS, wrk1, wrk2);

  /* Initialize correction to zero */
  for (is=0; is<Ns; is++)
    N_VConst(ZERO,acorS[is]);

  /* Loop until convergence; accumulate corrections in acorS */

  loop {
    
    nniS++;
    
    /* Correct yS from last fS value */
    for (is=0; is<Ns; is++) {
      N_VLinearSum(h, tempvS[is], -ONE, znS[1][is], tempvS[is]);
      N_VScale(rl1, tempvS[is], tempvS[is]);
      N_VLinearSum(ONE, znS[0][is], ONE, tempvS[is], yS[is]);
    }
    /* Get norm of current correction to use in convergence test */
    for (is=0; is<Ns; is++)
      N_VLinearSum(ONE, tempvS[is], -ONE, acorS[is], acorS[is]);
    Del = CVSensNorm(cv_mem, acorS, ewtS);
    for (is=0; is<Ns; is++)
      N_VScale(ONE, tempvS[is], acorS[is]);

    /* Test for convergence.  If m > 0, an estimate of the convergence
       rate constant is stored in crateS, and used in the test. 
       acnrmS contains the norm of the corrections (yS_n-yS_n(0)) and
       will be used in the error test (if errcon==FULL)                */
    if (m > 0) crateS = MAX(CRDOWN * crateS, Del / Delp);
    dcon = Del * MIN(ONE, crateS) / tq[4];
    
    if (dcon <= ONE) {
      if (errcon==FULL)
        acnrmS = (m==0)? Del : CVSensNorm(cv_mem, acorS, ewtS);
      return (SOLVED);  /* Convergence achieved */
    }

    /* Stop at maxcor iterations or if iter. seems to be diverging */
    m++;
    if ((m==maxcorS) || ((m >= 2) && (Del > RDIV * Delp)))
      return (CONV_FAIL);

    /* Save norm of correction, evaluate f, and loop again */
    Delp = Del;

    wrk1 = tempv;
    wrk2 = ftempS[0];
    CVSensRhs(cv_mem, tn, y, ftemp, yS, tempvS, wrk1, wrk2);

  } /* end loop */
  
}

/*------------------   CVStgrNlsNewton   --------------------------*/
/*
  STAGGERED approach

  This routine attempts to solve the sensitivity linear systems using 
  Newton iteration. It calls CVStgrNlsNewton to perform the actual 
  iteration. If the Newton iteration fails with out-of-date Jacobian 
  data (ier=TRY_AGAIN), it calls lsetup and retries the Newton iteration. 
  This second try is unlikely to happen when using a Krylov linear solver.
  
  Possible return values:
    SOLVED
    CONV_FAIL
    SOLVE_FAIL_UNREC
    SETUP_FAIL_UNREC
*/
/*-----------------------------------------------------------------*/

static int CVStgrNlsNewton(CVodeMem cv_mem)
{
  int is;
  int convfail, ier;
  booleantype callSetup;
  N_Vector vtemp1, vtemp2, vtemp3, wrk1, wrk2;

  callSetup = FALSE;

  loop {

    /* Set acorS to zero and load prediction into yS vector */
    for (is=0; is<Ns; is++) {
      N_VConst(ZERO, acorS[is]);
      N_VScale(ONE, znS[0][is], yS[is]);
    }
 
    /* Evaluate fS at predicted yS but with converged y (and corresponding f) */
    wrk1 = tempv;
    wrk2 = tempvS[0];
    CVSensRhs(cv_mem, tn, y, ftemp, yS, ftempS, wrk1, wrk2);
    
    /* Do the Newton iteration */
    ier = CVStgrNewtonIteration(cv_mem);

    /* If the solve was successful (ier=SOLVED) or if an error 
       that cannot be fixed by a call to lsetup occured
       (ier = SOLVE_FAIL_UNREC or CONV_FAIL) return */
    if (ier != TRY_AGAIN) return (ier);

    /* There was a convergence failure and the Jacobian-related data
       appears not to be current. Call lsetup with convfail=FAIL_BAD_J
       and then loop again */
    callSetup = TRUE;
    convfail = FAIL_BAD_J;

    /* Rename some vectors for readibility */
    vtemp1 = tempv;
    vtemp2 = yS[0];
    vtemp3 = ftempS[0];

    /* Call linear solver setup at converged y */
    ier = lsetup(cv_mem, convfail, y, ftemp, &jcur, 
                 vtemp1, vtemp2, vtemp3);
    nsetups++;
    nsetupsS++;
    gamrat = ONE;
    gammap = gamma;
    crate = ONE;
    crateS = ONE; /* after lsetup all conv. rates are reset to ONE */
    nstlp = nst;

    /* Return if lsetup failed */
    if (ier < 0) return (SETUP_FAIL_UNREC);
    if (ier > 0) return (CONV_FAIL);

  } /* end loop */

}

/*------------------ CVstgrNewtonIteration ------------------------*/
/*
  STAGGERED approach.

  This routine performs the Newton iteration for all sensitivities. 
  If the iteration succeeds, it returns the value SOLVED. 
  If not, it may signal the CVStgrNlsNewton routine to call lsetup and 
  reattempt the iteration, by returning the value TRY_AGAIN. (In this case, 
  CVStgrNlsNewton must set convfail to FAIL_BAD_J before calling setup again). 
  Otherwise, this routine returns one of the appropriate values 
  SOLVE_FAIL_UNREC or CONV_FAIL back to CVStgrNlsNewton.
*/
/*-----------------------------------------------------------------*/

static int CVStgrNewtonIteration(CVodeMem cv_mem)
{
  int m, ret;
  realtype Del, Delp, dcon;
  N_Vector *bS, wrk1, wrk2;
  int is;

  m = 0;

  /* ftemp  <- f(t_n, y_n)
     y      <- y_n
     ftempS <- fS(t_n, y_n(0), s_n(0))
     acorS  <- 0
     yS     <- yS_n(0)                   */

  loop {

    /* Evaluate the residual of the nonlinear systems */
    for (is=0; is<Ns; is++) {
      N_VLinearSum(rl1, znS[1][is], ONE, acorS[is], tempvS[is]);
      N_VLinearSum(gamma, ftempS[is], -ONE, tempvS[is], tempvS[is]);
    }

    /* Call the lsolve function */
    bS = tempvS;
    nniS++;
    for (is=0; is<Ns; is++) {

      ret = lsolve(cv_mem, bS[is], ewtS[is], y, ftemp);

      /* Unrecoverable error in lsolve */
      if (ret < 0) return (SOLVE_FAIL_UNREC);

      /* Recoverable error in lsolve and Jacobian data not current */
      if (ret > 0) { 
        if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
        return (CONV_FAIL);
      }

    }
 
    /* Get norm of correction; add correction to acorS and yS */
    Del = CVSensNorm(cv_mem, bS, ewtS);
    for (is=0; is<Ns; is++) {
      N_VLinearSum(ONE, acorS[is], ONE, bS[is], acorS[is]);
      N_VLinearSum(ONE, znS[0][is], ONE, acorS[is], yS[is]);
    }

    /* Test for convergence.  If m > 0, an estimate of the convergence
       rate constant is stored in crateS, and used in the test.        */
    if (m > 0) crateS = MAX(CRDOWN * crateS, Del/Delp);
    dcon = Del * MIN(ONE, crateS) / tq[4];
    if (dcon <= ONE) {
      if (errcon==FULL)
        acnrmS = (m==0) ? Del : CVSensNorm(cv_mem, acorS, ewtS);
      jcur = FALSE;
      return (SOLVED);  /* Convergence achieved */
    }

    m++;

    /* Stop at maxcor iterations or if iter. seems to be diverging.
       If still not converged and Jacobian data is not current, 
       signal to try the solution again                            */
    if ((m == maxcorS) || ((m >= 2) && (Del > RDIV * Delp))) {
      if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
      return (CONV_FAIL);
    }
    
    /* Save norm of correction, evaluate fS, and loop again */
    Delp = Del;
    
    wrk1 = tempv;
    wrk2 = tempvS[0];
    CVSensRhs(cv_mem, tn, y, ftemp, yS, ftempS, wrk1, wrk2);
    
  } /* end loop */

}

/*------------------     CVStgr1Nls      --------------------------*/
/*
  STAGGERED1 approach

  This is a high-level routine that attempts to solve the i-th 
  sensitivity linear system using nonlinear iterations (FUNCTIONAL
  or NEWTON - depending on the value of iter) once the states y_n
  were obtained and passed the error test.
*/
/*-----------------------------------------------------------------*/

static int CVStgr1Nls(CVodeMem cv_mem, int is)
{
  int flag=SOLVED;

  switch(iter) {
  case FUNCTIONAL: 
    flag = CVStgr1NlsFunctional(cv_mem,is);
    break;
  case NEWTON:     
    flag = CVStgr1NlsNewton(cv_mem,is);
    break;
  }

  return(flag);

}

/*------------------ CVStgr1NlsFunctional -------------------------*/
/*
  STAGGERED1 approach

  This routine attempts to solve the i-th sensitivity linear system
  using functional iteration (no matrices involved).

  Possible return values:
    SOLVED
    CONV_FAIL
*/
/*-----------------------------------------------------------------*/

static int CVStgr1NlsFunctional(CVodeMem cv_mem, int is)
{
  int m;
  realtype Del, Delp, dcon;
  N_Vector wrk1, wrk2;

  /* Initialize estimated conv. rate and counter */
  crateS = ONE;
  m = 0;

  /* Evaluate fS at predicted yS but with converged y (and corresponding f) */
  wrk1 = tempv;
  wrk2 = ftempS[0];
  CVSensRhs1(cv_mem, tn, y, ftemp, is, znS[0][is], tempvS[is], wrk1, wrk2);

  /* Initialize correction to zero */
  N_VConst(ZERO,acorS[is]);

  /* Loop until convergence; accumulate corrections in acorS */

  loop {

    nniS1[is]++;

    /* Correct yS from last fS value */
    N_VLinearSum(h, tempvS[is], -ONE, znS[1][is], tempvS[is]);
    N_VScale(rl1, tempvS[is], tempvS[is]);
    N_VLinearSum(ONE, znS[0][is], ONE, tempvS[is], yS[is]);

    /* Get WRMS norm of current correction to use in convergence test */
    N_VLinearSum(ONE, tempvS[is], -ONE, acorS[is], acorS[is]);
    Del = N_VWrmsNorm(acorS[is], ewtS[is]);
    N_VScale(ONE, tempvS[is], acorS[is]);

    /* Test for convergence.  If m > 0, an estimate of the convergence
       rate constant is stored in crateS, and used in the test. */

    if (m > 0) crateS = MAX(CRDOWN * crateS, Del / Delp);
    dcon = Del * MIN(ONE, crateS) / tq[4];

    if (dcon <= ONE) {
      return (SOLVED);  /* Convergence achieved */
    }

    /* Stop at maxcor iterations or if iter. seems to be diverging */
    m++;
    if ((m==maxcorS) || ((m >= 2) && (Del > RDIV * Delp)))
      return (CONV_FAIL);

    /* Save norm of correction, evaluate f, and loop again */
    Delp = Del;

    wrk1 = tempv;
    wrk2 = ftempS[0];
    CVSensRhs1(cv_mem, tn, y, ftemp, is, yS[is], tempvS[is], wrk1, wrk2);

  } /* end loop */
  
}

/*------------------   CVStgr1NlsNewton  --------------------------*/
/*
  STAGGERED1 approach

  This routine attempts to solve the i-th sensitivity linear system 
  using Newton iteration. It calls CVStgr1NlsNewton to perform the 
  actual iteration. If the Newton iteration fails with out-of-date 
  Jacobian data (ier=TRY_AGAIN), it calls lsetup and retries the 
  Newton iteration. This second try is unlikely to happen when 
  using a Krylov linear solver.

  Possible return values:
    SOLVED
    CONV_FAIL
    SOLVE_FAIL_UNREC
    SETUP_FAIL_UNREC
*/
/*-----------------------------------------------------------------*/

static int CVStgr1NlsNewton(CVodeMem cv_mem, int is)
{
  int convfail, ier;
  booleantype callSetup;
  N_Vector vtemp1, vtemp2, vtemp3, wrk1, wrk2;

  callSetup = FALSE;

  loop {

    /* Set acorS to zero and load prediction into yS vector */
    N_VConst(ZERO, acorS[is]);
    N_VScale(ONE, znS[0][is], yS[is]);
 
    /* Evaluate fS at predicted yS but with converged y (and corresponding f) */
    wrk1 = tempv;
    wrk2 = tempvS[0];
    CVSensRhs1(cv_mem, tn, y, ftemp, is, yS[is], ftempS[is], wrk1, wrk2);
    
    /* Do the Newton iteration */
    ier = CVStgr1NewtonIteration(cv_mem, is);

    /* If the solve was successful (ier=SOLVED) or if an error 
       that cannot be fixed by a call to lsetup occured
       (ier = SOLVE_FAIL_UNREC or CONV_FAIL) return */
    if (ier != TRY_AGAIN) return (ier);

    /* There was a convergence failure and the Jacobian-related data
       appears not to be current. Call lsetup with convfail=FAIL_BAD_J
       and then loop again */
    callSetup = TRUE;
    convfail = FAIL_BAD_J;

    /* Rename some vectors for readibility */
    vtemp1 = tempv;
    vtemp2 = yS[0];
    vtemp3 = ftempS[0];

    /* Call linear solver setup at converged y */
    ier = lsetup(cv_mem, convfail, y, ftemp, &jcur, 
                 vtemp1, vtemp2, vtemp3);
    nsetups++;
    nsetupsS++;
    gamrat = ONE;
    crate = ONE;
    crateS = ONE; /* after lsetup all conv. rates are reset to ONE */
    gammap = gamma;
    nstlp = nst;

    /* Return if lsetup failed */
    if (ier < 0) return (SETUP_FAIL_UNREC);
    if (ier > 0) return (CONV_FAIL);

  } /* end loop */

}

/*------------------ CVStgr1NewtonIteration -----------------------*/
/*
  STAGGERED1 approach.

  This routine performs the Newton iteration for the i-th sensitivity. 
  If the iteration succeeds, it returns the value SOLVED. 
  If not, it may signal the CVStgr1NlsNewton routine to call lsetup 
  and reattempt the iteration, by returning the value TRY_AGAIN. 
  (In this case, CVStgr1NlsNewton must set convfail to FAIL_BAD_J 
  before calling setup again). Otherwise, this routine returns one 
  of the appropriate values SOLVE_FAIL_UNREC or CONV_FAIL back to 
  CVStgr1NlsNewton.
*/
/*-----------------------------------------------------------------*/

static int CVStgr1NewtonIteration(CVodeMem cv_mem, int is)
{
  int m, ret;
  realtype Del, Delp, dcon;
  N_Vector *bS, wrk1, wrk2;

  m = 0;

  /* ftemp      <- f(t_n, y_n)
     y          <- y_n
     ftempS[is] <- fS(is, t_n, y_n(0), s_n(0))
     acorS[is]  <- 0
     yS[is]     <- yS_n(0)[is]                 */

  loop {

    /* Evaluate the residual of the nonlinear systems */
    N_VLinearSum(rl1, znS[1][is], ONE, acorS[is], tempvS[is]);
    N_VLinearSum(gamma, ftempS[is], -ONE, tempvS[is], tempvS[is]);

    /* Call the lsolve function */
    bS = tempvS;

    nniS1[is]++;

    ret = lsolve(cv_mem, bS[is], ewtS[is], y, ftemp);

    /* Unrecoverable error in lsolve */
    if (ret < 0) return (SOLVE_FAIL_UNREC);

    /* Recoverable error in lsolve and Jacobian data not current */
    if (ret > 0) { 
      if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
      return (CONV_FAIL);
    }

    /* Get norm of correction; add correction to acorS and yS */
    Del = N_VWrmsNorm(bS[is], ewtS[is]);
    N_VLinearSum(ONE, acorS[is], ONE, bS[is], acorS[is]);
    N_VLinearSum(ONE, znS[0][is], ONE, acorS[is], yS[is]);

    /* Test for convergence.  If m > 0, an estimate of the convergence
       rate constant is stored in crateS, and used in the test.        */
    if (m > 0) crateS = MAX(CRDOWN * crateS, Del/Delp);
    dcon = Del * MIN(ONE, crateS) / tq[4];
    if (dcon <= ONE) {
      jcur = FALSE;
      return (SOLVED);  /* Convergence achieved */
    }

    m++;

    /* Stop at maxcor iterations or if iter. seems to be diverging.
       If still not converged and Jacobian data is not current, 
       signal to try the solution again                            */
    if ((m == maxcorS) || ((m >= 2) && (Del > RDIV * Delp))) {
      if ((!jcur) && (setupNonNull)) return (TRY_AGAIN);
      return (CONV_FAIL);
    }

    /* Save norm of correction, evaluate fS, and loop again */
    Delp = Del;

    wrk1 = tempv;
    wrk2 = tempvS[0];
    CVSensRhs1(cv_mem, tn, y, ftemp, is, yS[is], ftempS[is], wrk1, wrk2);

  } /* end loop */

}

/*------------------   CVStgrDoErrorTest --------------------------*/
/* 
  STAGGERED or STAGGERED1 approach

  This routine performs the local error test on the sensitivity vars.
  The local error norm is loaded into dsmS, and the test dsmS ?<= 1 
  is made.
  
  If the test passes for all sensitivities, CVStgrDoErrorTest returns 
  TRUE. 
  
  If the test fails, we proceed like in CVDoErrorTest.
*/
/*-----------------------------------------------------------------*/

static booleantype CVStgrDoErrorTest(CVodeMem cv_mem, int *nflagPtr, 
                                     int *kflagPtr, realtype saved_t, 
                                     int *nefSPtr, realtype *dsmSPtr)
{
  int is;
  realtype dsmS;
  N_Vector wrk1, wrk2;

  dsmS = acnrmS / tq[2];

  /* If dsmS passes test, return TRUE */  
  *dsmSPtr = dsmS;
  if (dsmS <= ONE) return (TRUE);

  /* Test failed; increment counters, set nflag, and restore zn, znS arrays */
  (*nefSPtr)++;
  netfS++;
  *nflagPtr = PREV_ERR_FAIL;
  CVRestore(cv_mem, saved_t);
  
  /* At maxnef failures or |h| = hmin, return with nflag = REP_ERR_FAIL */
  if ((ABS(h) <= hmin*ONEPSM) || ( (*nefSPtr) == maxnef)) {
    *kflagPtr = REP_ERR_FAIL;
    return (FALSE);
  }

  /* Set etamax = 1 to prevent step size increase at end of this step */
  etamax = ONE;
  
  /* Set h ratio eta from dsmS, rescale, and return for retry of step */
  if ((*nefSPtr) <= MXNEF1) {
    eta = ONE / (RPowerR(BIAS2*dsmS,ONE/L) + ADDON);
    eta = MAX(ETAMIN, MAX(eta, hmin / ABS(h)));
    if ( (*nefSPtr) >= SMALL_NEF) eta = MIN(eta, ETAMXF);
    CVRescale(cv_mem);
    return (FALSE);
  }

  /* After MXNEF1 failures, force an order reduction and retry step */
  if (q > 1) {
    eta = MAX(ETAMIN, hmin / ABS(h));
    CVAdjustOrder(cv_mem,-1);
    L = q;
    q--;
    qwait = L;
    CVRescale(cv_mem);
    return (FALSE);
  }
  
  /* If already at order 1, restart: reload zn from scratch */
  eta = MAX(ETAMIN, hmin / ABS(h));
  h *= eta;
  hscale = h;
  qwait = LONG_WAIT;
  nscon = 0;
  
  f(tn, zn[0], tempv, f_data);
  nfe++;
  N_VScale(h, tempv, zn[1]);
  
  if (quad) {
    fQ(tn, zn[0], tempvQ, fQ_data);
    nfQe++;
    N_VScale(h, tempvQ, znQ[1]);
  }

  wrk1 = ftemp;
  wrk2 = ftempS[0];
  CVSensRhs(cv_mem, tn, zn[0], tempv, znS[0], tempvS, wrk1, wrk2);
  for (is=0; is<Ns; is++) 
    N_VScale(h, tempvS[is], znS[1][is]);
  
  return (FALSE);
  
}

/*=================================================================*/
/*END          Routines for STAGGERED and STAGGERED1               */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        Private Routines after succesfull step              */
/*=================================================================*/

/*------------------   CVCompleteStep    --------------------------*/
/*
  This routine performs various update operations when the solution
  to the nonlinear system has passed the local error test. 
  We increment the step counter nst, record the values hu and qu,
  update the tau array, and apply the corrections to the zn array.
  The tau[i] are the last q values of h, with tau[1] the most recent.
  The counter qwait is decremented, and if qwait == 1 (and q < qmax)
  we save acor and tq[5] for a possible order increase.
*/
/*-----------------------------------------------------------------*/

static void CVCompleteStep(CVodeMem cv_mem)
{
  int i, j;
  int is;
  
  nst++;
  nscon++;
  hu = h;
  qu = q;

  for (i=q; i >= 2; i--)  tau[i] = tau[i-1];
  if ((q==1) && (nst > 1)) tau[2] = tau[1];
  tau[1] = h;

  /* Apply correction to column j of zn: l_j * Delta_n */

  for (j=0; j <= q; j++) 
    N_VLinearSum(l[j], acor, ONE, zn[j], zn[j]);

  if (quad) {
    for (j=0; j <= q; j++) 
      N_VLinearSum(l[j], acorQ, ONE, znQ[j], znQ[j]);
  }

  if (sensi) {
    for (is=0; is<Ns; is++)
      for (j=0; j <= q; j++) 
        N_VLinearSum(l[j], acorS[is], ONE, znS[j][is], znS[j][is]);
  }

  /* If necessary, store Delta_n in zn[qmax] to be used in order increase
  
     This actually will be Delta_{n-1} in the ELTE at q+1 since it happens at
     the next to last step of order q before a possible one at order q+1
  */

  qwait--;
  if ((qwait == 1) && (q != qmax)) {
    
    N_VScale(ONE, acor, zn[qmax]);
    
    if (quad && (errconQ == FULL))
      N_VScale(ONE, acorQ, znQ[qmax]);

    if (sensi && (errcon == FULL))
      for (is=0; is<Ns; is++)
        N_VScale(ONE, acorS[is], znS[qmax][is]);
    
    saved_tq5 = tq[5];
    
  }

}

/*------------------   CVPrepareNextStep --------------------------*/
/*
  This routine handles the setting of stepsize and order for the
  next step -- hprime and qprime.  Along with hprime, it sets the
  ratio eta = hprime/h.  It also updates other state variables 
  related to a change of step size or order. 
*/
/*-----------------------------------------------------------------*/

static void CVPrepareNextStep(CVodeMem cv_mem, realtype dsm)
{
  /* If etamax = 1, defer step size or order changes */
  if (etamax == ONE) {
    qwait = MAX(qwait, 2);
    qprime = q;
    hprime = h;
    eta = ONE;
    return;
  }

  /* etaq is the ratio of new to old h at the current order */  
  etaq = ONE /(RPowerR(BIAS2*dsm,ONE/L) + ADDON);
  
  /* If no order change, adjust eta and acor in CVSetEta and return */
  if (qwait != 0) {
    eta = etaq;
    qprime = q;
    CVSetEta(cv_mem);
    return;
  }
  
  /* If qwait = 0, consider an order change.   etaqm1 and etaqp1 are 
     the ratios of new to old h at orders q-1 and q+1, respectively.
     CVChooseEta selects the largest; CVSetEta adjusts eta and acor */
  qwait = 2;
  etaqm1 = CVComputeEtaqm1(cv_mem);
  etaqp1 = CVComputeEtaqp1(cv_mem);  
  CVChooseEta(cv_mem); 
  CVSetEta(cv_mem);
}

/*------------------    CVSetEta         --------------------------*/
/*
  This routine adjusts the value of eta according to the various
  heuristic limits and the optional input hmax.  It also resets
  etamax to be the estimated local error vector.
*/
/*-----------------------------------------------------------------*/

static void CVSetEta(CVodeMem cv_mem)
{

  /* If eta below the threshhold THRESH, reject a change of step size */
  if (eta < THRESH) {
    eta = ONE;
    hprime = h;
  } else {
    /* Limit eta by etamax and hmax, then set hprime */
    eta = MIN(eta, etamax);
    eta /= MAX(ONE, ABS(h)*hmax_inv*eta);
    hprime = h * eta;
    if (qprime < q) nscon = 0;
  }

  /* Reset etamx for the next step size change, and scale acor */
}

/*------------------   CVComputeEtaqm1   --------------------------*/
/*
  This routine computes and returns the value of etaqm1 for a
  possible decrease in order by 1.
*/
/*-----------------------------------------------------------------*/

static realtype CVComputeEtaqm1(CVodeMem cv_mem)
{
  realtype ddn;
  
  etaqm1 = ZERO;

  if (q > 1) {

    ddn = N_VWrmsNorm(zn[q], ewt);

    if ( quad && (errconQ == FULL)) {
      ddn = CVQuadUpdateNorm(cv_mem, ddn, znQ[q], ewtQ);
    }

    if ( sensi && (errcon == FULL) ) {
      ddn = CVSensUpdateNorm(cv_mem, ddn, znS[q], ewtS);
    }

    ddn = ddn/tq[1];

    etaqm1 = ONE/(RPowerR(BIAS1*ddn, ONE/q) + ADDON);

  }

  return (etaqm1);
}

/*------------------   CVComputeEtaqp1   --------------------------*/
/*
  This routine computes and returns the value of etaqp1 for a
  possible increase in order by 1.
*/
/*-----------------------------------------------------------------*/

static realtype CVComputeEtaqp1(CVodeMem cv_mem)
{
  realtype dup, cquot;
  int is;
  
  etaqp1 = ZERO;

  if (q != qmax) {

    cquot = (tq[5] / saved_tq5) * RPowerI(h/tau[2], L);

    N_VLinearSum(-cquot, zn[qmax], ONE, acor, tempv);

    dup = N_VWrmsNorm(tempv, ewt);

    if ( quad && (errconQ == FULL)) {
      N_VLinearSum(-cquot, znQ[qmax], ONE, acorQ, tempvQ);
      dup = CVQuadUpdateNorm(cv_mem, dup, tempvQ, ewtQ);
    }

    if ( sensi && (errcon == FULL) ) {
      for (is=0; is<Ns; is++) 
        N_VLinearSum(-cquot, znS[qmax][is], ONE, acorS[is], tempvS[is]);
      dup = CVSensUpdateNorm(cv_mem, dup, tempvS, ewtS);
    }

    dup = dup / tq[3];

    etaqp1 = ONE / (RPowerR(BIAS3*dup, ONE/(L+1)) + ADDON);

  }

  return (etaqp1);
}

/*------------------   CVChooseEta       --------------------------*/
/*
  Given etaqm1, etaq, etaqp1 (the values of eta for qprime =
  q - 1, q, or q + 1, respectively), this routine chooses the 
  maximum eta value, sets eta to that value, and sets qprime to the
  corresponding value of q.  If there is a tie, the preference
  order is to (1) keep the same order, then (2) decrease the order,
  and finally (3) increase the order.  If the maximum eta value
  is below the threshhold THRESH, the order is kept unchanged and
  eta is set to 1.
*/
/*-----------------------------------------------------------------*/

static void CVChooseEta(CVodeMem cv_mem)
{
  realtype etam;
  int is;
  
  etam = MAX(etaqm1, MAX(etaq, etaqp1));
  
  if (etam < THRESH) {
    eta = ONE;
    qprime = q;
    return;
  }

  if (etam == etaq) {
    eta = etaq;
    qprime = q;
  } else if (etam == etaqm1) {
    eta = etaqm1;
    qprime = q - 1;
  } else {
    eta = etaqp1;
    qprime = q + 1;
    
    /* Store Delta_n in zn[qmax] to be used in order increase 
    
       This happens at the last step of order q before an increase
       to order q+1, so it represents Delta_n in the ELTE at q+1
    */
    
    N_VScale(ONE, acor, zn[qmax]);
    
    if (quad && (errconQ == FULL))
      N_VScale(ONE, acorQ, znQ[qmax]);

    if (sensi && (errcon == FULL))
      for (is=0; is<Ns; is++)
        N_VScale(ONE, acorS[is], znS[qmax][is]);
    
  }
}

/*=================================================================*/
/*END          Private Routines after succesfull step              */
/*=================================================================*/

/*------------------  CVHandleFailur     --------------------------*/
/*
  This routine prints error messages for all cases of failure by
  CVStep. It returns to CVode the value that CVode is to return to
  the user.
*/
/*-----------------------------------------------------------------*/

static int CVHandleFailure(CVodeMem cv_mem, int kflag)
{

  /* Set imxer to the index of maximum weighted local error */
  N_VProd(acor, ewt, tempv);
  N_VAbs(tempv, tempv);
  
  /* Depending on kflag, print error message and return error flag */
  switch (kflag) {
  case REP_ERR_FAIL:  
    fprintf(errfp, MSG_ERR_FAILS, tn, h);
    return (ERR_FAILURE);
  case REP_CONV_FAIL: 
    fprintf(errfp, MSG_CONV_FAILS, tn, h);
    return (CONV_FAILURE);
  case SETUP_FAILED:  
    fprintf(errfp, MSG_SETUP_FAILED, tn);
    return (SETUP_FAILURE);
  case SOLVE_FAILED:  
    fprintf(errfp, MSG_SOLVE_FAILED, tn);
    return (SOLVE_FAILURE);
  }

  return(0);

}

/*=================================================================*/
/*BEGIN        BDF Stability Limit Detection                       */
/*=================================================================*/

/*------------------     CVBDFStab       --------------------------*/
/*
  This routine handles the BDF Stability Limit Detection Algorithm
  STALD.  It is called if lmm = BDF and the SLDET option is on.
  If the order is 3 or more, the required norm data is saved.
  If a decision to reduce order has not already been made, and
  enough data has been saved, CVsldet is called.  If it signals
  a stability limit violation, the order is reduced, and the step
  size is reset accordingly.
*/
/*-----------------------------------------------------------------*/

void CVBDFStab(CVodeMem cv_mem)
{
  int i,k, ldflag, factorial;
  realtype sq, sqm1, sqm2;
      
  /* If order is 3 or greater, then save scaled derivative data,
     push old data down in i, then add current values to top.    */

  if (q >= 3) {
    for (k = 1; k <= 3; k++)
      for (i = 5; i >= 2; i--) 
        ssdat[i][k] = ssdat[i-1][k]; 
    factorial = 1;
    for (i = 1; i <= q-1; i++) factorial *= i;
    sq = factorial*q*(q+1)*acnrm/tq[5];
    sqm1 = factorial*q*N_VWrmsNorm(zn[q], ewt);
    sqm2 = factorial*N_VWrmsNorm(zn[q-1], ewt);
    ssdat[1][1] = sqm2*sqm2;
    ssdat[1][2] = sqm1*sqm1;
    ssdat[1][3] = sq*sq;
  }  

  if (qprime >= q) {

    /* If order is 3 or greater, and enough ssdat has been saved,
       nscon >= q+5, then call stability limit detection routine.  */

    if ( (q >= 3) && (nscon >= q+5) ) {
      ldflag = CVsldet(cv_mem);
      if (ldflag > 3) {
        /* A stability limit violation is indicated by
           a return flag of 4, 5, or 6.
           Reduce new order.                     */
        qprime = q-1;
        eta = etaqm1; 
        eta = MIN(eta,etamax);
        eta = eta/MAX(ONE,ABS(h)*hmax_inv*eta);
        hprime = h*eta;
        nor = nor + 1;
     /* printf(" Order reduced to %d by CVBDFStab at nst = %d,\n    h = %e hnew = %e\n",
        qprime,nst,h,h*eta); */
      }
    }
  }
  else {
    /* Otherwise, let order increase happen, and 
       reset stability limit counter, nscon.     */
    nscon = 0;
  }
}

/*------------------     CVsldet         --------------------------*/
/*
  This routine detects stability limitation using stored scaled 
  derivatives data. CVsldet returns the magnitude of the
  dominate characteristic root, rr. The presents of a stability
  limit is indicated by rr > "something a little less then 1.0",  
  and a positive kflag. This routine should only be called if
  order is greater than or equal to 3, and data has been collected
  for 5 time steps. 
 
  Returned values:
     kflag = 1 -> Found stable characteristic root, normal matrix case
     kflag = 2 -> Found stable characteristic root, quartic solution
     kflag = 3 -> Found stable characteristic root, quartic solution,
                  with Newton correction
     kflag = 4 -> Found stability violation, normal matrix case
     kflag = 5 -> Found stability violation, quartic solution
     kflag = 6 -> Found stability violation, quartic solution,
                  with Newton correction

     kflag < 0 -> No stability limitation, 
                  or could not compute limitation.

     kflag = -1 -> Min/max ratio of ssdat too small.
     kflag = -2 -> For normal matrix case, vmax > vrrt2*vrrt2
     kflag = -3 -> For normal matrix case, The three ratios
                   are inconsistent.
     kflag = -4 -> Small coefficient prevents elimination of quartics.  
     kflag = -5 -> R value from quartics not consistent.
     kflag = -6 -> No corrected root passes test on qk values
     kflag = -7 -> Trouble solving for sigsq.
     kflag = -8 -> Trouble solving for B, or R via B.
     kflag = -9 -> R via sigsq[k] disagrees with R from data.
*/
/*-----------------------------------------------------------------*/

static int CVsldet(CVodeMem cv_mem)
{
  int i, k, j, it, kmin=0, kflag=0;
  realtype rat[5][4], rav[4], qkr[4], sigsq[4], smax[4], ssmax[4];
  realtype drr[4], rrc[4],sqmx[4], qjk[4][4], vrat[5], qc[6][4], qco[6][4];
  realtype rr, rrcut, vrrtol, vrrt2, sqtol, rrtol;
  realtype smink, smaxk, sumrat, sumrsq, vmin, vmax, drrmax, adrr;
  realtype small, tem, sqmax, saqk, qp, s, sqmaxk, saqj, sqmin;
  realtype rsa, rsb, rsc, rsd, rse, rd1a, rd1b, rd1c, rd1d;
  realtype rd2a, rd2b, rd2c, rd3a, rd3b, cest1, corr1; 
  realtype ratp, ratm, qfac1, qfac2, bb, rrb;

 /* The following are cutoffs and tolerances used by this routine */

  rrcut = 0.98;
  vrrtol = 1.0e-4;
  vrrt2 = 5.0e-4;
  sqtol = 1.0e-3;
  rrtol = 1.0e-2;

  rr = ZERO;

 /*  Index k corresponds to the degree of the interpolating polynomial. */
 /*      k = 1 -> q-1          */
 /*      k = 2 -> q            */
 /*      k = 3 -> q+1          */

 /*  Index i is a backward-in-time index, i = 1 -> current time, */
 /*      i = 2 -> previous step, etc    */

 /* get maxima, minima, and variances, and form quartic coefficients  */

  for (k=1; k<=3; k++) {
    smink = ssdat[1][k];
    smaxk = ZERO;
    
    for (i=1; i<=5; i++) {
      smink = MIN(smink,ssdat[i][k]);
      smaxk = MAX(smaxk,ssdat[i][k]);
    }
    
    if (smink < TINY*smaxk) {
      kflag = -1;  
      return (kflag);
    }
    smax[k] = smaxk;
    ssmax[k] = smaxk*smaxk;
    
    sumrat = ZERO;
    sumrsq = ZERO;
    for (i=1; i<=4; i++) {
      rat[i][k] = ssdat[i][k]/ssdat[i+1][k];
      sumrat = sumrat + rat[i][k];
      sumrsq = sumrsq + rat[i][k]*rat[i][k];
    } 
    rav[k] = FOURTH*sumrat;
    vrat[k] = ABS(FOURTH*sumrsq - rav[k]*rav[k]);
      
    qc[5][k] = ssdat[1][k]*ssdat[3][k] - ssdat[2][k]*ssdat[2][k];
    qc[4][k] = ssdat[2][k]*ssdat[3][k] - ssdat[1][k]*ssdat[4][k];
    qc[3][k] = ZERO;
    qc[2][k] = ssdat[2][k]*ssdat[5][k] - ssdat[3][k]*ssdat[4][k];
    qc[1][k] = ssdat[4][k]*ssdat[4][k] - ssdat[3][k]*ssdat[5][k];
    
    for (i=1; i<=5; i++) {
      qco[i][k] = qc[i][k];
    }
  }                            /* End of k loop */
  
  /* Isolate normal or nearly-normal matrix case. Three quartic will
     have common or nearly-common roots in this case. 
     Return a kflag = 1 if this procedure works. If three root 
     differ more than vrrt2, return error kflag = -3.    */
  
  vmin = MIN(vrat[1],MIN(vrat[2],vrat[3]));
  vmax = MAX(vrat[1],MAX(vrat[2],vrat[3]));
  
  if (vmin < vrrtol*vrrtol) {

    if (vmax > vrrt2*vrrt2) {
      kflag = -2;  
      return (kflag);
    } else {
      rr = (rav[1] + rav[2] + rav[3])/THREE;
      drrmax = ZERO;
      for (k = 1;k<=3;k++) {
        adrr = ABS(rav[k] - rr);
        drrmax = MAX(drrmax, adrr);
      }
      if (drrmax > vrrt2)
        kflag = -3;    
      kflag = 1;
      /*  can compute charactistic root, drop to next section   */
    }

  } else {
      
      /* use the quartics to get rr. */
      
      if (ABS(qco[1][1]) < TINY*ssmax[1]) {
        small = qco[1][1];
        kflag = -4;    
        return (kflag);
      }
      
      tem = qco[1][2]/qco[1][1];
      for (i=2; i<=5; i++) {
        qco[i][2] = qco[i][2] - tem*qco[i][1];
      }
      
      qco[1][2] = ZERO;
      tem = qco[1][3]/qco[1][1];
      for (i=2; i<=5; i++) {
        qco[i][3] = qco[i][3] - tem*qco[i][1];
      }
      qco[1][3] = ZERO;
      
      if (ABS(qco[2][2]) < TINY*ssmax[2]) {
        small = qco[2][2];
        kflag = -4;    
        return (kflag);
      }
      
      tem = qco[2][3]/qco[2][2];
      for (i=3; i<=5; i++) {
        qco[i][3] = qco[i][3] - tem*qco[i][2];
      }
      
      if (ABS(qco[4][3]) < TINY*ssmax[3]) {
        small = qco[4][3];
        kflag = -4;    
        return (kflag);
      }
      
      rr = -qco[5][3]/qco[4][3];
      
      if (rr < TINY || rr > HUN) {
        kflag = -5;   
        return (kflag);
      }
      
      for (k=1; k<=3; k++) {
        qkr[k] = qc[5][k] + rr*(qc[4][k] + rr*rr*(qc[2][k] + rr*qc[1][k]));
      }  
      
      sqmax = ZERO;
      for (k=1; k<=3; k++) {
        saqk = ABS(qkr[k])/ssmax[k];
        if (saqk > sqmax) sqmax = saqk;
      } 
      
      if (sqmax < sqtol) {
        kflag = 2;
        
        /*  can compute charactistic root, drop to "given rr,etc"   */
        
      } else {
          
        /* do Newton corrections to improve rr.  */
        
        for (it=1; it<=3; it++) {
          for (k=1; k<=3; k++) {
            qp = qc[4][k] + rr*rr*(THREE*qc[2][k] + rr*FOUR*qc[1][k]);
            drr[k] = ZERO;
            if (ABS(qp) > TINY*ssmax[k]) drr[k] = -qkr[k]/qp;
            rrc[k] = rr + drr[k];
          } 
          
          for (k=1; k<=3; k++) {
            s = rrc[k];
            sqmaxk = ZERO;
            for (j=1; j<=3; j++) {
              qjk[j][k] = qc[5][j] + s*(qc[4][j] + 
                                        s*s*(qc[2][j] + s*qc[1][j]));
              saqj = ABS(qjk[j][k])/ssmax[j];
              if (saqj > sqmaxk) sqmaxk = saqj;
            } 
            sqmx[k] = sqmaxk;
          } 
              
          sqmin = sqmx[1] + ONE;
          for (k=1; k<=3; k++) {
            if (sqmx[k] < sqmin) {
              kmin = k;
              sqmin = sqmx[k];
            }
          } 
          rr = rrc[kmin];
              
          if (sqmin < sqtol) {
            kflag = 3;
            /*  can compute charactistic root   */
            /*  break out of Newton correction loop and drop to "given rr,etc" */ 
            break;
          } else {
            for (j=1; j<=3; j++) {
              qkr[j] = qjk[j][kmin];
            }
          }     
        } /*  end of Newton correction loop  */ 
          
        if (sqmin > sqtol) {
          kflag = -6;
          return (kflag);
        }
      } /*  end of if (sqmax < sqtol) else   */
  } /*  end of if (vmin < vrrtol*vrrtol) else, quartics to get rr. */
  
  /* given rr, find sigsq[k] and verify rr.  */
  /* All positive kflag drop to this section  */
  
  for (k=1; k<=3; k++) {
    rsa = ssdat[1][k];
    rsb = ssdat[2][k]*rr;
    rsc = ssdat[3][k]*rr*rr;
    rsd = ssdat[4][k]*rr*rr*rr;
    rse = ssdat[5][k]*rr*rr*rr*rr;
    rd1a = rsa - rsb;
    rd1b = rsb - rsc;
    rd1c = rsc - rsd;
    rd1d = rsd - rse;
    rd2a = rd1a - rd1b;
    rd2b = rd1b - rd1c;
    rd2c = rd1c - rd1d;
    rd3a = rd2a - rd2b;
    rd3b = rd2b - rd2c;
    
    if (ABS(rd1b) < TINY*smax[k]) {
      kflag = -7;
      return (kflag);
    }
    
    cest1 = -rd3a/rd1b;
    if (cest1 < TINY || cest1 > FOUR) {
      kflag = -7;
      return (kflag);
    }
    corr1 = (rd2b/cest1)/(rr*rr);
    sigsq[k] = ssdat[3][k] + corr1;
  }
  
  if (sigsq[2] < TINY) {
    kflag = -8;
    return (kflag);
  }
  
  ratp = sigsq[3]/sigsq[2];
  ratm = sigsq[1]/sigsq[2];
  qfac1 = FOURTH*(q*q - ONE);
  qfac2 = TWO/(q - ONE);
  bb = ratp*ratm - ONE - qfac1*ratp;
  tem = ONE - qfac2*bb;
  
  if (ABS(tem) < TINY) {
    kflag = -8;
    return (kflag);
  }
  
  rrb = ONE/tem;
  
  if (ABS(rrb - rr) > rrtol) {
    kflag = -9;
    return (kflag);
  }
  
  /* Check to see if rr is above cutoff rrcut  */
  if (rr > rrcut) {
    if (kflag == 1) kflag = 4;
    if (kflag == 2) kflag = 5;
    if (kflag == 3) kflag = 6;
  }
  
  /* All positive kflag returned at this point  */
  
  return (kflag);
  
}

/*=================================================================*/
/*END          BDF Stability Limit Detection                       */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        Combined norms                                      */
/*=================================================================*/

/*------------------  CVQuadUpdateNorm   --------------------------*/
/*
  Updates the norm old_nrm to account for all quadratures.
*/
/*-----------------------------------------------------------------*/

static realtype CVQuadUpdateNorm(CVodeMem cv_mem, realtype old_nrm,
                                 N_Vector xQ, N_Vector wQ)
{
  realtype qnrm;

  qnrm = N_VWrmsNorm(xQ, wQ);
  if (old_nrm > qnrm) return(old_nrm);
  else                return(qnrm);
}

/*------------------   CVQuadUpdateDsm   --------------------------*/
/*
  Usage    : dms = CVQuadUpdateDsm(cv_mem, dsm, dsmQ);

  This routine updates the local error norm dsm with quadrature
  related information. Used only if quadratures are computed
  with FULL error control.
  
  Returns the maximum over the wheighted local error norms.
*/
/*-----------------------------------------------------------------*/

static realtype CVQuadUpdateDsm(CVodeMem cv_mem, realtype old_dsm, 
                                realtype dsmQ)
{
  if ( old_dsm > dsmQ ) return (old_dsm);
  else                  return (dsmQ);
}

/*------------------   CVSensNorm        --------------------------*/
/*
  This routine returns the maximum over the weighted root mean 
  square norm of xS with weight vectors wS:

    max { wrms(xS[0],wS[0]) ... wrms(xS[Ns-1],wS[Ns-1]) }    

  Called by CVSensUpdateNorm or directly in the STAGGERED approach 
  during the NLS solution and before the error test.
*/
/*-----------------------------------------------------------------*/

static realtype CVSensNorm(CVodeMem cv_mem, N_Vector *xS, N_Vector *wS)
{
  int is;
  realtype nrm, snrm;

  nrm = N_VWrmsNorm(xS[0],wS[0]);
  for (is=1; is<Ns; is++) {
    snrm = N_VWrmsNorm(xS[is],wS[is]);
    if ( snrm > nrm ) nrm = snrm;
  }

  return (nrm);

}

/*------------------  CVSensUpdateNorm   --------------------------*/
/*
  Updates the norm old_nrm to account for all sensitivities.
*/
/*-----------------------------------------------------------------*/

static realtype CVSensUpdateNorm(CVodeMem cv_mem, realtype old_nrm,
                                 N_Vector *xS, N_Vector *wS)
{
  realtype snrm;
  
  snrm = CVSensNorm(cv_mem, xS, wS);
  if (old_nrm > snrm) return(old_nrm);
  else                return(snrm);
}


/*------------------   CVStgrUpdateNorm  --------------------------*/
/*
  Usage    : dms = CVStgrUpdateDsm(cv_mem, old_dsm, dsmS);

  This routine updates the local error norm old_dsm with sensitivity 
  related information. Used only in the STAGGERED or STAGGERED1 
  approach with FULL error control.This value is consistent with 
  the one computed in CVDoErrorTest when ism=SIMULTANEOUS and 
  errcon=FULL.
  
  Returns the maximum over the wheighted local error norms.
*/
/*-----------------------------------------------------------------*/

static realtype CVStgrUpdateDsm(CVodeMem cv_mem, realtype old_dsm, 
                                realtype dsmS)
{
  if ( old_dsm > dsmS ) return (old_dsm);
  else                  return (dsmS);
}

/*=================================================================*/
/*END          Combined norms                                      */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        Sensitivity RHS Routines                            */
/*=================================================================*/

/*------------------     CVSensRhs       --------------------------*/
/*
  CVSensRhs is a high level routine that returns right hand side 
  of sensitivity equations. Depending on the 'ifS' flag, it either 
  calls directly the fS routine (ifS=ALLSENS) or (if ifS=ONESENS) 
  calls the fS1 routine in a loop over all sensitivities.

  CVSensRhs is called:
   (*) by Cvode at the first step
   (*) by CVYddNorm if errcon=FULL
   (*) by CVnlsFunctional, CVnlsNewton, and CVNewtonIteration
       if ism=SIMULTANEOUS
   (*) by CVDoErrorTest when restarting from scratch
   (*) in the corrector loop if ism=STAGGERED
   (*) by CVStgrDoErrorTest when restarting from scratch 
*/
/*-----------------------------------------------------------------*/

static void CVSensRhs(CVodeMem cv_mem, realtype time, 
                      N_Vector ycur, N_Vector fcur, 
                      N_Vector *yScur, N_Vector *fScur,
                      N_Vector temp1, N_Vector temp2)
{
  int is;

  if (ifS==ALLSENS) {
    fS(Ns, time, ycur, fcur, yScur, fScur, 
       fS_data, temp1, temp2);
    nfSe++;
  } else {
    for (is=0; is<Ns; is++) {
      fS1(Ns, time, ycur, fcur, is, yScur[is], fScur[is], 
          fS_data, temp1, temp2);
      nfSe++;
    }
  }
}

/*------------------    CVSensRhs1       --------------------------*/
/*
  CVSensRhs1 is a high level routine that returns right hand side 
  of the is-th sensitivity equation. 
  
  CVSensRhs1 is called only during the STAGGERED1 corrector loop
  (ifS must be ONESENS, otherwise CVodeSensMalloc would have 
  issued an error message).
*/
/*-----------------------------------------------------------------*/

static void CVSensRhs1(CVodeMem cv_mem, realtype time, 
                       N_Vector ycur, N_Vector fcur, 
                       int is, N_Vector yScur, N_Vector fScur,
                       N_Vector temp1, N_Vector temp2)
{
  fS1(Ns, time, ycur, fcur, is, yScur, fScur, 
      fS_data, temp1, temp2);
  nfSe++;
}

/*=================================================================*/
/*BEGIN        Undefine Readibility Constants                      */
/*=================================================================*/

#undef Ns
#undef y
#undef yS
#undef fS_data
#undef ftemp

/*=================================================================*/
/*END          Undefine Readibility Constants                      */
/*=================================================================*/

/*=================================================================*/
/*BEGIN        DQ Approximations for Sensitivity RHS Routines      */
/*=================================================================*/

/*------------------    CVSensRhsDQ      --------------------------*/
/*
  CVSensRhsDQ computes right hand side of all sensitivity equations
  by finite differences
*/
/*-----------------------------------------------------------------*/

static void CVSensRhsDQ(int Ns, realtype t, 
                        N_Vector y, N_Vector ydot, 
                        N_Vector *yS, N_Vector *ySdot, 
                        void *fS_data,  
                        N_Vector ytemp, N_Vector ftemp)
{
  int is;
  
  for (is=0; is<Ns; is++)
    CVSensRhs1DQ(Ns, t, y, ydot, is, yS[is], ySdot[is],
                 fS_data, ytemp, ftemp);
  
}

/*------------------    CVSensRhs1DQ     --------------------------*/
/*
  CVSensRhs1DQ computes the right hand side of the is-th sensitivity 
  equation by finite differences
*/
/*-----------------------------------------------------------------*/

static void CVSensRhs1DQ(int Ns, realtype t, 
                         N_Vector y, N_Vector ydot, 
                         int is, N_Vector yS, N_Vector ySdot, 
                         void *fS_data,
                         N_Vector ytemp, N_Vector ftemp)
{
  CVodeMem cv_mem;
  int method;
  int nfel = 0, which;
  booleantype skipFP;
  realtype psave, pbari;
  realtype delta , rdelta;
  realtype Deltap, rDeltap, r2Deltap;
  realtype Deltay, rDeltay, r2Deltay;
  realtype Delta , rDelta , r2Delta ;
  realtype norms, ratio;
  
  /* fS_data points to cv_mem */
  cv_mem = (CVodeMem) fS_data;

  delta  = RSqrt(MAX(*reltol, uround));
  rdelta = ONE/delta;
  
  if (plist!=NULL) {
    which   = abs(plist[is]) - 1;
    skipFP  = (plist[is] < 0);
  } else {
    which  = is;
    skipFP = FALSE;
  }
  psave   = p[which];
  pbari   = ABS(pbar[which]);
  
  Deltap  = pbari * delta;
  rDeltap = ONE/Deltap;
  norms   = N_VWrmsNorm(yS, ewt) * pbari;
  rDeltay = MAX(norms, rdelta) / pbari;
  Deltay  = ONE/rDeltay;
  
  ratio = Deltay * rDeltap;
  
  if ((MAX(ONE/ratio, ratio) <= ABS(rhomax)) || rhomax == ZERO)
    method = (rhomax >= ZERO) ? CENTERED1 : FORWARD1; 
  else
    method = (rhomax > ZERO) ? CENTERED2 : FORWARD2;
  
  switch(method) {
    
  case CENTERED1:
    
    Delta = MIN(Deltay, Deltap);
    r2Delta = HALF/Delta;
    
    N_VLinearSum(ONE,y,Delta,yS,ytemp);
    p[which] = psave + Delta;
    f(t, ytemp, ySdot, f_data);
    nfel++;
    
    N_VLinearSum(ONE,y,-Delta,yS,ytemp);
    p[which] = psave - Delta;
    f(t, ytemp, ftemp, f_data);
    nfel++;
    
    N_VLinearSum(r2Delta,ySdot,-r2Delta,ftemp,ySdot);
    
    break;
    
  case CENTERED2:
    
    r2Deltap = HALF/Deltap;
    r2Deltay = HALF/Deltay;
    
    N_VLinearSum(ONE,y,Deltay,yS,ytemp);
    f(t, ytemp, ySdot, f_data);
    nfel++;
    N_VLinearSum(ONE,y,-Deltay,yS,ytemp);
    f(t, ytemp, ftemp, f_data);
    nfel++;
    N_VLinearSum(r2Deltay, ySdot, -r2Deltay, ftemp, ySdot);
    
    if (!skipFP) {
      p[which] = psave + Deltap;
      f(t, y, ytemp, f_data);
      nfel++;
      p[which] = psave - Deltap;
      f(t, y, ftemp, f_data);
      nfel++;
      N_VLinearSum(r2Deltap,ytemp,-r2Deltap,ftemp,ftemp);
      
      N_VLinearSum(ONE,ySdot,ONE,ftemp,ySdot);
    }
    
    break;
    
  case FORWARD1:
    
    Delta = MIN(Deltay, Deltap);
    rDelta = ONE/Delta;
    
    N_VLinearSum(ONE,y,Delta,yS,ytemp);
    p[which] = psave + Delta;
    f(t, ytemp, ySdot, f_data);
    nfel++;
    
    N_VLinearSum(rDelta,ySdot,-rDelta,ydot,ySdot);
    
    break;
    
  case FORWARD2:
    
    N_VLinearSum(ONE,y,Deltay,yS,ytemp);
    f(t, ytemp, ySdot, f_data);
    nfel++;
    N_VLinearSum(rDeltay, ySdot, -rDeltay, ydot, ySdot);
    
    if (!skipFP) {
      p[which] = psave + Deltap;
      f(t, y, ytemp, f_data);
      nfel++;
      N_VLinearSum(rDeltap,ytemp,-rDeltap,ydot,ftemp);
      
      N_VLinearSum(ONE,ySdot,ONE,ftemp,ySdot);
    }
    
    break;
    
  }
  
  p[which] = psave;
  
  /* Increment counter nfeS */
  nfeS += nfel;
  
}

/*=================================================================*/
/*END          DQ Approximations for Sensitivity RHS Routines      */
/*=================================================================*/

/*=================================================================*/
/*END          Sensitivity RHS Routines                            */
/*=================================================================*/

/*=================================================================*/
/*END          PRIVATE FUNCTIONS IMPLEMENTATION                    */
/*=================================================================*/
