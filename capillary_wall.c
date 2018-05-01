#include <math.h>
#include "pluto.h"
#include "capillary_wall.h"

//debug macro
// #define DBG_FIND_CLOSEST

double const zcap = ZCAP/UNIT_LENGTH;
double const dzcap = DZCAP/UNIT_LENGTH;
double const rcap = RCAP/UNIT_LENGTH;
// Actual values used inside the simulation for zcap, rcap, dzcap;
double zcap_real, rcap_real, dzcap_real;
int capillary_not_set = 1;
int i_cap_inter_end, j_cap_inter_end, j_elec_start;

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
  i_cap_inter_end = find_idx_closest(grid[0].xr_glob, grid[0].gend-grid[0].gbeg+1, rcap);
  j_cap_inter_end = find_idx_closest(grid[1].xr_glob, grid[1].gend-grid[1].gbeg+1, zcap);
  j_elec_start = find_idx_closest(grid[1].xl_glob, grid[1].gend-grid[1].gbeg+1, zcap-dzcap);

  rcap_real = grid[0].xr_glob[i_cap_inter_end];
  zcap_real = grid[1].xr_glob[j_cap_inter_end];
  dzcap_real = grid[1].xr_glob[j_cap_inter_end]-grid[1].xl_glob[j_elec_start];

  print1("\n\n-------------------------------------------------------------------------");
  print1("\nIndexes of remarkable internal bounary points:");
  print1("\ni_cap_inter_end: \t%d", i_cap_inter_end);
  print1("\nj_cap_inter_end: \t%d", j_cap_inter_end);
  print1("\nj_elec_start:    \t%d\n", j_elec_start);
  print1("\nRemarkable points:");
  print1("\nCapillary radius,      set: %g; \tactual: %g \t(cm)", RCAP, rcap_real*UNIT_LENGTH);
  print1("\nCapillary half length, set: %g; \tactual: %g \t(cm)", ZCAP, zcap_real*UNIT_LENGTH);
  print1("\nElectrode length,      set: %g; \tactual: %g \t(cm)", DZCAP, dzcap_real*UNIT_LENGTH);
  print1("\n( electrode actual start: z=%g; \t(cm) )",(zcap_real-dzcap_real)*UNIT_LENGTH);
  print1("\n---------------------------------------------------------------------------");
  print1("\n");
  
  capillary_not_set = 0;
  return 0;
}

/******************************************************************/
/*Finds the index of the element in vec closest to the value of v*/
/******************************************************************/
int find_idx_closest(double *vec, int Nvec, double v){
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