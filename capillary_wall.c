#include <math.h>
#include "pluto.h"
#include "capillary_wall.h"

//debug macro
// #define DBG_FIND_CLOSEST

// Box useful for setting bcs internal to the domain
static RBox rbox_center_capWall[2];
static RBox rbox_center_capCorn[1];

double const zcap = ZCAP/UNIT_LENGTH;
double const dzcap = DZCAP/UNIT_LENGTH;
double const rcap = RCAP/UNIT_LENGTH;
// Actual values used inside the simulation for zcap, rcap, dzcap;
double zcap_real, rcap_real, dzcap_real;
int capillary_not_set = 1;
int i_cap_inter_end, j_cap_inter_end, j_elec_start;

double en_cond_loss = 0;
double en_res_loss = 0;
double en_adv_loss = 0;
double en_Bvloss = 0;

Corr d_correction[3] = { {},{},{} };

/******************************************************************/
/* Sets the remarkable indexes of the grid for the capillary*/
/******************************************************************/
int SetRemarkableIdxs(Grid *grid){

  /* Capillary:
                                     j=j_elec_start (first cell belonging to electrode)
                                      :     j=j_cap_inter_end (ghost)
               r                      :      |
               ^    |                 :      *
               |    |       wall      :      *
                    |                 v      *
i=i_cap_inter_end+1 |****(ghosts)*****o*******|
i=i_cap_inter_end   |                        j=j_cap_inter_end+1 (first outside, not ghost)
                    |
                    |
i=0                 o-------------------------------->(axis)
                   j=0           -> z
    // I should not change the grid size exacly on the capillary end!
    */

  /* I find the indexes of the cells closest to the capillary bounds*/
  i_cap_inter_end = IBEG + FindIdxClosest(&(grid[IDIR].xr_glob[IBEG]), IEND-IBEG+1, rcap);
  j_cap_inter_end = JBEG + FindIdxClosest(&(grid[JDIR].xr_glob[JBEG]), JEND-JBEG+1, zcap);
  j_elec_start = JBEG + FindIdxClosest(&(grid[JDIR].xl_glob[JBEG]), JEND-JBEG+1, zcap-dzcap);

  if (j_elec_start > j_cap_inter_end) {
    print1("\n[SetRemarkableIdxs]Electrode appears to start after end of capillary! Quitting.");
    QUIT_PLUTO(1);
  }

  rcap_real = grid[IDIR].xr_glob[i_cap_inter_end];
  zcap_real = grid[JDIR].xr_glob[j_cap_inter_end];
  dzcap_real = grid[JDIR].xr_glob[j_cap_inter_end]-grid[JDIR].xl_glob[j_elec_start];

  print1("\n  -------------------------------------------------------------------------");
  print1("\n  Indexes of remarkable internal bounary points:");
  print1("\n  i_cap_inter_end: \t%d", i_cap_inter_end);
  print1("\n  j_cap_inter_end: \t%d", j_cap_inter_end);
  print1("\n  j_elec_start:    \t%d\n", j_elec_start);
  print1("\n  Remarkable points:");
  print1("\n  Capillary radius,      set: %g; \tactual: %g \t(cm)", RCAP, rcap_real*UNIT_LENGTH);
  print1("\n  Capillary half length, set: %g; \tactual: %g \t(cm)", ZCAP, zcap_real*UNIT_LENGTH);
  print1("\n  Electrode length,      set: %g; \tactual: %g \t(cm)", DZCAP, dzcap_real*UNIT_LENGTH);
  print1("\n  ( electrode actual start: z=%g; \t(cm) )",(zcap_real-dzcap_real)*UNIT_LENGTH);
  print1("\n");
  print1("\n  Just so that you know:");
  print1("\n  NX3_TOT=%d, NX2_TOT=%d, NX1_TOT=%d",NX3_TOT, NX2_TOT, NX1_TOT);
  print1("\n  IBEG=%d, IEND=%d, JBEG=%d, JEND=%d, KBEG=%d, KEND=%d", IBEG, IEND, JBEG, JEND, KBEG, KEND);
  print1("\n  ---------------------------------------------------------------------------");
  print1("\n");
  
  capillary_not_set = 0;
  return 0;
}

/******************************************************************/
/*Finds the index of the element in vec closest to the value of v*/
/******************************************************************/
int FindIdxClosest(double *vec, int Nvec, double v){
  int i, i_mindiff;
  double diff;

  #ifdef DBG_FIND_CLOSEST
    print1("\nvec[0]:%g, v:%g", vec[0], v);
    print1("\nfabs(vec[0]-v):%g", fabs(vec[0]-v));
  #endif

  diff = fabs(vec[0]-v);
  for (i=1;i<Nvec;i++){
    if (fabs(vec[i]-v) < diff) {
      #ifdef DBG_FIND_CLOSEST
        print1("\nabs(vec[%d](=%e)-%e)<%e",i,vec[i],v,diff);
      #endif
      diff = fabs(vec[i]-v);
      i_mindiff = i;
    } else {
      #ifdef DBG_FIND_CLOSEST
        print1("\nfabs(vec[%d](=%e)-%e)>%e",i,vec[i],v,diff);
      #endif
    }
  }
  return i_mindiff;
}

/*--------------------------------------------------------------------
 alloc_Data(Data *data) : Allocate space for a Data element         
--------------------------------------------------------------------*/
Data* alloc_Data() {
  Data* newdata = (Data *)malloc(sizeof(Data));
  /*******************************************************************
   *  What follows has been copied from initialize.c, lines: 448-472 *
   *******************************************************************/
  // print1 ("\n> Memory allocation\n");
  newdata->Vc = ARRAY_4D(NVAR, NX3_TOT, NX2_TOT, NX1_TOT, double);
  newdata->Uc = ARRAY_4D(NX3_TOT, NX2_TOT, NX1_TOT, NVAR, double);

  #ifdef STAGGERED_MHD
   newdata->Vs = ARRAY_1D(DIMENSIONS, double ***);
   D_EXPAND(
     newdata->Vs[BX1s] = ArrayBox( 0, NX3_TOT-1, 0, NX2_TOT-1,-1, NX1_TOT-1); ,
     newdata->Vs[BX2s] = ArrayBox( 0, NX3_TOT-1,-1, NX2_TOT-1, 0, NX1_TOT-1); ,
     newdata->Vs[BX3s] = ArrayBox(-1, NX3_TOT-1, 0, NX2_TOT-1, 0, NX1_TOT-1);)
  #endif

  #if UPDATE_VECTOR_POTENTIAL == YES
   D_EXPAND(                                                  ,
     newdata->Ax3 = ARRAY_3D(NX3_TOT, NX2_TOT, NX1_TOT, double);  ,
     newdata->Ax1 = ARRAY_3D(NX3_TOT, NX2_TOT, NX1_TOT, double);
     newdata->Ax2 = ARRAY_3D(NX3_TOT, NX2_TOT, NX1_TOT, double);
   )
  #endif

  #if RESISTIVITY != NO
   newdata->J = ARRAY_4D(3,NX3_TOT, NX2_TOT, NX1_TOT, double);
  #endif

  newdata->flag = ARRAY_3D(NX3_TOT, NX2_TOT, NX1_TOT, unsigned char);

  return newdata;
}

/*--------------------------------------------------------------------
 Copies the Vc field of a Data structure
--------------------------------------------------------------------*/
void copy_Data_Vc(Data *d_target, const Data *d_source) {
  int i,j,k,nv;
  TOT_LOOP (k,j,i) {
    VAR_LOOP (nv) d_target->Vc[nv][k][j][i] = d_source->Vc[nv][k][j][i];
  }
}

/*--------------------------------------------------------------------
 free_Data(Data *data) Free the memory of a Data element         
--------------------------------------------------------------------*/
void free_Data(Data *data) {
  // print1 ("\n> Memory allocation\n");
  FreeArray4D ((void *) data->Vc);
  FreeArray4D ((void *) data->Uc);

  #ifdef STAGGERED_MHD
    #error deallocation of Vs is not yet implemented 
  #endif

  #if UPDATE_VECTOR_POTENTIAL == YES
    #error deallocation of Ax1, Ax2, Ax3 is not yet implemented
  #endif

  #if RESISTIVITY != NO
    FreeArray4D ((void *) data->J);
  #endif

  FreeArray3D ((void *) data->flag);

  free((Data *) data);
}

void SetRBox_capWall(int Nghost) {
  int s;
  /* ---------------------------------------------------
    0. set CAP_WALL_INTERNAL grid index ranges
   --------------------------------------------------- */
  
  s = CAP_WALL_INTERNAL;

  rbox_center_capWall[s].vpos = CENTER;

  rbox_center_capWall[s].ib = i_cap_inter_end + 1;
  rbox_center_capWall[s].ie = i_cap_inter_end + 1 + Nghost - 1;
  
  rbox_center_capWall[s].jb = 0;
  rbox_center_capWall[s].je = j_cap_inter_end - Nghost;
  
  rbox_center_capWall[s].kb = 0;
  rbox_center_capWall[s].ke = NX3_TOT-1;

  /* ---------------------------------------------------
    2. set CAP_WALL_EXTERNAL grid index ranges
   --------------------------------------------------- */

  s = CAP_WALL_EXTERNAL;

  rbox_center_capWall[s].vpos = CENTER;

  rbox_center_capWall[s].ib = i_cap_inter_end + 1 + Nghost - 1;
  rbox_center_capWall[s].ie = NX1_TOT-1;

  rbox_center_capWall[s].jb = j_cap_inter_end - Nghost + 1;
  rbox_center_capWall[s].je = j_cap_inter_end;

  rbox_center_capWall[s].kb = 0;
  rbox_center_capWall[s].ke = NX3_TOT-1;

  /* ---------------------------------------------------
    3. set grid index ranges for capillary
       corner correction (rbox_center_capCorn),
   --------------------------------------------------- */
  s = 0;
  
  rbox_center_capCorn[s].vpos = CENTER;

  rbox_center_capCorn[s].ib = i_cap_inter_end + 1;
  rbox_center_capCorn[s].ie = i_cap_inter_end + 1 + Nghost - 1;

  rbox_center_capCorn[s].jb = j_cap_inter_end - Nghost + 1;
  rbox_center_capCorn[s].je = j_cap_inter_end;

  rbox_center_capCorn[s].kb = 0;
  rbox_center_capCorn[s].ke = NX3_TOT-1;
}

/* ********************************************************************* */
RBox *GetRBoxCap(int side, int vpos)
/*!
 *  Returns a pointer to a local static RBox 
 *
 *  \param[in]  side  the region of the computational domain where 
 *                    the box is required.
 *                    Possible values :
 *                    CAP_WALL_INTERNAL       
 *                    CAP_WALL_EXTERNAL       
 *                    CAP_WALL_CORNER_INTERNAL
 *                    CAP_WALL_CORNER_EXTERNAL
 * 
 *  \param[in]  vpos  the variable position inside the cell:
 *                    CENTER is the only avaiable for now.
 *
 *********************************************************************** */
{
  if (vpos != CENTER) {
    print1("\n[GetRBoxCap] Only vpos == CENTER is implemented!");
  }
  if      (side == CAP_WALL_INTERNAL) 
    return &(rbox_center_capWall[CAP_WALL_INTERNAL]);
  else if (side == CAP_WALL_EXTERNAL)
    return &(rbox_center_capWall[CAP_WALL_EXTERNAL]);
  else if (side == CAP_WALL_CORNER_INTERNAL)
    return &(rbox_center_capCorn[0]);
  else if (side == CAP_WALL_CORNER_EXTERNAL)
    return &(rbox_center_capCorn[0]);
  else
    return NULL;
}

void ReflectiveBoundCap (double ***q, int s, int side, int vpos)
/*!
 * Make symmetric (s = 1) or anti-symmetric (s=-1) profiles.
 * The sign is set by the FlipSign() function. 
 *
 * \param [in,out] q   a 3D flow quantity
 * \param [in] s   an integer taking only the values +1 (symmetric 
 *                 profile) or -1 (antisymmetric profile)
 *   
 *********************************************************************** */
{
  int   i, j, k;
  RBox *box = GetRBoxCap(side, vpos);

  if (side == CAP_WALL_INTERNAL) {
    /* [Ema] Values are simply reflected across the boundary
          (depending on "s", with sign changed or not!),
          even if the ghost cells are more than one,
          e.g.: with 2 ghosts cells per side:
                q[k][j][IEND+1] = q[k][j][IEND]
                q[k][j][IEND+2] = q[k][j][IEND-1]
          remember: IEND is the last cell index inside the real domain.
    */
    BOX_LOOP(box,k,j,i) q[k][j][i] = s*q[k][j][2*i_cap_inter_end-i+1];

  }else if (side == CAP_WALL_EXTERNAL){  
    BOX_LOOP(box,k,j,i) q[k][j][i] = s*q[k][2*(j_cap_inter_end+1)-j-1][i];
  } else if ( side == CAP_WALL_CORNER_EXTERNAL || side == CAP_WALL_CORNER_INTERNAL) {
    #error qui devi cambiare delle cose (o usare addirittura un altra funz. perchè devi agire su d_correction.Vc)
  }
}