/*Functions and utilities for integration of parabolic (currently resistive
and thermal conduction) terms with the Alternating Directino Implicit algorithm*/
#include "pluto.h"
#include "adi.h"
#include "capillary_wall.h"
#include "current_table.h"
#include "Thermal_Conduction/tc.h"
#include "pvte_law_heat_capacity.h"

#if KBEG != KEND
  #error grid in k direction should only be of 1 point
#endif

// Remarkable comments:
// [Opt] = it can be optimized (in terms of performance)

void ADI(const Data *d, Time_Step *Dts, Grid *grid) {
  static int first_call=1;
  int i,j,k, l;
  static Lines lines[2]; /*I define two of them as they are 1 per direction (r and z)*/
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    static double **Ip_B, **Im_B, **Jp_B, **Jm_B, **CI_B, **CJ_B;
    static double **Bra1, **Bra2, **Brb1, **Brb2;
    static double **Br;
    // Energy increse(due to electro-magnetics) terms
    static double **dUres_a1, **dUres_a2, **dUres_b1, **dUres_b2;
  #endif
  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    static double **Ip_T, **Im_T, **Jp_T, **Jm_T, **CI_T, **CJ_T;
    static double **Ta1, **Ta2, **Tb1, **Tb2;
    static double **T;
    static double **dEdT;
    double v[NVAR]; /*[Ema] I hope that NVAR as dimension is fine!*/
    double rhoe_old, rhoe_new;
    int nv;
  #endif
  const double dt = g_dt;
  double ****Uc, ****Vc;
  double *r, *r_1;
  /*Initial time before advancing the equations with the ADI method*/
  double const t_start = g_time; /*g_time è: "The current integration time."(dalla docuementazione in Doxigen)*/

  // Find the remarkable indexes (if they had not been found before)
  if (capillary_not_set) {
    if (SetRemarkableIdxs(grid)){
      print1("\nError while setting remarkable points!");
      QUIT_PLUTO(1);
    }
  }
  /* Some shortcuts */
  Vc = d->Vc;
  Uc = d->Uc;
  r = grid[IDIR].x_glob;
  r_1 = grid[IDIR].r_1;

  /* -------------------------------------------------------------------
  Build geometry and allocate some stuff
  ----------------------------------------------------------------------*/
  if (first_call) {
    GeometryADI(lines, grid);
    #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
      Ip_B = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Im_B = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Jp_B = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Jm_B = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      CI_B = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      CJ_B = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      Bra1 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Bra2 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Brb1 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Brb2 = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      Br = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      dUres_a1 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      dUres_a2 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      dUres_b1 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      dUres_b2 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
    #endif

    #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
      Ip_T = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Im_T = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Jp_T = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Jm_T = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      CI_T = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      CJ_T = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      Ta1 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Ta2 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Tb1 = ARRAY_2D(NX2_TOT, NX1_TOT, double);
      Tb2 = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      T = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      dEdT = ARRAY_2D(NX2_TOT, NX1_TOT, double);

      TOT_LOOP (k,j,i) {
        Ta1[j][i] = 0.0;
        Ta2[j][i] = 0.0;
        Tb1[j][i] = 0.0;
        Tb2[j][i] = 0.0;
      }
    #endif

    first_call=0;
  }

  /* -------------------------------------------------------------------------
      Compute the conservative vector in order to start the cycle.
      This step will be useless if the data structure
      contains Vc as well as Uc (for future improvements).
      [Ema] (Comment copied from sts.c)
    --------------------------------------------------------------------------- */
  PrimToConsLines (Vc, Uc, lines);

  /* -------------------------------------------------------------------------
      Build the temperature (T) and/or the product magnetic field with radius (B*r)
     ------------------------------------------------------------------------- */
  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    #if EOS==IDEAL
      DOM_LOOP(k,j,i) T[j][i] = Vc[PRS][k][j][i]/Vc[RHO][k][j][i];
    #elif EOS==PVTE_LAW
      DOM_LOOP(k,j,i) {
        for (nv=NVAR; nv--;) v[nv] = Vc[nv][k][j][i];
        if (GetPV_Temperature(v, &(T[j][i]) )!=0) {
          print1("ADI:[Ema] Error computing temperature!\n");
        }
        T[j][i] = T[j][i] / KELVIN;
      }
    #else
      print1("ADI:[Ema] Error computing temperature, this EOS not implemented!")
    #endif
    BuildIJ_forTC(d, grid, lines, Ip_T, Im_T, Jp_T, Jm_T, CI_T, CJ_T, dEdT);
  #endif
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    // Build a handy magnetic field matrix
    DOM_LOOP(k,j,i)
      Br[j][i] = r[i]*Uc[k][j][i][BX3];
    BuildIJ_forRes(d, grid, lines, Ip_B, Im_B, Jp_B, Jm_B, CI_B, CJ_B);
  #endif
  BoundaryADI(lines, d, grid, t_start); // Get bcs at t

  /*[Rob] Maybe I can partially couple the two problems:
    first do a half step of both, then recompute the quantities
    and do the second half-step of both*/

  /**********************************
   (a.1) Explicit update sweeping IDIR
  **********************************/
  //[Err] Remove next #if lines
  // #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
  //   ResEnergyIncrease(dUres_a1, Ip_B, Im_B, Br, grid, &lines[IDIR], 0.5*dt, IDIR);
  //   ResEnergyIncrease(dUres_a2, Jp_B, Jm_B, Br, grid, &lines[JDIR], 0.5*dt, JDIR);
  // #endif
  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    ExplicitUpdate (Ta1, T, NULL, Ip_T, Im_T, CI_T, &lines[IDIR],
                  lines[IDIR].lbound[TDIFF], lines[IDIR].rbound[TDIFF], 0.5*dt, IDIR);
  #endif
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    ExplicitUpdate (Bra1, Br, NULL, Ip_B, Im_B, CI_B, &lines[IDIR],
                    lines[IDIR].lbound[BDIFF], lines[IDIR].rbound[BDIFF], 0.5*dt, IDIR);
    // [Err] Decomment next #if lines
    #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
      ResEnergyIncrease(dUres_a1, Ip_B, Im_B, Br, grid, &lines[IDIR], 0.5*dt, IDIR);
    #endif
  #endif

  /**********************************
   (a.2) Implicit update sweeping JDIR
  **********************************/
  BoundaryADI(lines, d, grid, t_start+0.5*dt); // Get bcs at half step (not exaclty at t+0.5*dt)
  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    ImplicitUpdate (Ta2, Ta1, NULL, Jp_T, Jm_T, CJ_T, &lines[JDIR],
                    lines[JDIR].lbound[TDIFF], lines[JDIR].rbound[TDIFF], 0.5*dt, JDIR);
  #endif
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    ImplicitUpdate (Bra2, Bra1, NULL, Jp_B, Jm_B, CJ_B, &lines[JDIR],
                      lines[JDIR].lbound[BDIFF], lines[JDIR].rbound[BDIFF], 0.5*dt, JDIR);
    // [Err] Decomment next #if lines
    #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
      ResEnergyIncrease(dUres_a2, Jp_B, Jm_B, Bra2, grid, &lines[JDIR], 0.5*dt, JDIR);
    #endif
  #endif
  // [Err] Remove next four lines
  // ResEnergyIncrease(dUres_a1, Ip_B, Im_B, Bra2, grid, &lines[IDIR], 0.5*dt, IDIR);
  // ResEnergyIncrease(dUres_a2, Jp_B, Jm_B, Bra2, grid, &lines[JDIR], 0.5*dt, JDIR);
  // ResEnergyIncrease(dUres_b1, Ip_B, Im_B, Bra2, grid, &lines[IDIR], 0.5*dt, IDIR);
  // ResEnergyIncrease(dUres_b2, Jp_B, Jm_B, Bra2, grid, &lines[JDIR], 0.5*dt, JDIR);

  /**********************************
   (b.1) Explicit update sweeping JDIR
  **********************************/
  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    ExplicitUpdate (Tb1, Ta2, NULL, Jp_T, Jm_T, CJ_T, &lines[JDIR],
                    lines[JDIR].lbound[TDIFF], lines[JDIR].rbound[TDIFF], 0.5*dt, JDIR);
  #endif
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    ExplicitUpdate (Brb1, Bra2, NULL, Jp_B, Jm_B, CJ_B, &lines[JDIR],
                    lines[JDIR].lbound[BDIFF], lines[JDIR].rbound[BDIFF], 0.5*dt, JDIR);
    // [Err] Decomment next #if lines
    #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
    /* [Opt]: I could inglobate this call to ResEnergyIncrease in the previous one by using dt instead of 0.5*dt
       (but in this way it is more readable)*/
      ResEnergyIncrease(dUres_b1, Jp_B, Jm_B, Bra2, grid, &lines[JDIR], 0.5*dt, JDIR);
    #endif
  #endif

  /**********************************
   (b.2) Implicit update sweeping IDIR
  **********************************/
  BoundaryADI(lines, d, grid, t_start+dt); // Get bcs at t+dt
  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    ImplicitUpdate (Tb2, Tb1, NULL, Ip_T, Im_T, CI_T, &lines[IDIR],
                  lines[IDIR].lbound[TDIFF], lines[IDIR].rbound[TDIFF], 0.5*dt, IDIR);
  #endif
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    ImplicitUpdate (Brb2, Brb1, NULL, Ip_B, Im_B, CI_B, &lines[IDIR],
                      lines[IDIR].lbound[BDIFF], lines[IDIR].rbound[BDIFF], 0.5*dt, IDIR);
    // [Err] Decomment next two lines
    #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
      ResEnergyIncrease(dUres_b2, Ip_B, Im_B, Brb2, grid, &lines[IDIR], 0.5*dt, IDIR);
    #endif
  #endif
  //[Err] Remove next #if lines
  // #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
  //   ResEnergyIncrease(dUres_b1, Ip_B, Im_B, Brb2, grid, &lines[IDIR], 0.5*dt, IDIR);
  //   ResEnergyIncrease(dUres_b2, Jp_B, Jm_B, Brb2, grid, &lines[JDIR], 0.5*dt, JDIR);
  // #endif

/* ------------------------------------------------------------
   ------------------------------------------------------------
    Update data
   ------------------------------------------------------------
   ------------------------------------------------------------ */
  KDOM_LOOP(k) {
    LINES_LOOP(lines[IDIR], l, j, i) {
      #if (RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT)
        Uc[k][j][i][BX3] = Brb2[j][i]*r_1[i];

        #if (HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
          Uc[k][j][i][ENG] += dUres_a1[j][i]+dUres_a2[j][i]+dUres_b1[j][i]+dUres_b2[j][i];
        #endif
      #endif

      #if (THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT)
        // I get the int. energy from the temperature
        #if EOS==IDEAL
          #error Not implemented for ideal eos (but it is easy to add it!)
        #elif EOS==PVTE_LAW
            #ifdef TEST_ADI
              for (nv=NVAR; nv--;) v[nv] = Vc[nv][k][j][i];

              /*I think in this way the update does not conserve the energy*/
              rhoe_old = 3/2*CONST_kB*v[RHO]*UNIT_DENSITY/CONST_mp*T[j][i]*KELVIN;
              rhoe_old /= (UNIT_DENSITY*UNIT_VELOCITY*UNIT_VELOCITY);
              rhoe_new = 3/2*CONST_kB*v[RHO]*UNIT_DENSITY/CONST_mp*Tb2[j][i]*KELVIN;
              rhoe_new /= (UNIT_DENSITY*UNIT_VELOCITY*UNIT_VELOCITY);
              Uc[k][j][i][ENG] += rhoe_new-rhoe_old;

              /*I think in this way the update should conserve the energy, but it seems unstable!(23052018)*/
              // Uc[k][j][i][ENG] += dEdT[j][i]*(Tb2[j][i]-T[j][i]);
            #else
              /*I think in this way the update should conserve the energy, but it seems unstable!(23052018)*/
              // Uc[k][j][i][ENG] += dEdT[j][i]*(Tb2[j][i]-T[j][i]);

              /*[Err]/[Rob] I think in this way the update does not conserve the energy*/
              for (nv=NVAR; nv--;) v[nv] = Vc[nv][k][j][i];
              rhoe_old = InternalEnergyFunc(v, T[j][i]*KELVIN); // I guess in this way it is not conservative!
              rhoe_new = InternalEnergyFunc(v, Tb2[j][i]*KELVIN); // I guess in this way it is not conservative!
              Uc[k][j][i][ENG] += rhoe_new-rhoe_old;
            #endif
        #else
          print1("ADI:[Ema] Error computing internal energy, this EOS not implemented!");
        #endif
      #endif
    }
  }

  /* -------------------------------------------------------------------------
      Compute back the primitive vector from the updated conservative vector.
     ------------------------------------------------------------------------- */
  ConsToPrimLines (Uc, Vc, d->flag, lines);

}

/****************************************************************************
* Function to build the bcs of lines
* In the current implementation of this function Data *d is not used
* but I leave it there since before or later it might be needed
*****************************************************************************/
void BoundaryADI(Lines lines[2], const Data *d, Grid *grid, double t) {
  int i,j,l;
  double t_sec;
  # if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    double Twall;
    double Twall_K = g_inputParam[TWALL]; // Wall temperature in Kelvin
  #endif
  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    double Bwall;
    double curr, unit_Mfield;
  #endif
  // [Err]
  double L = 0.02/UNIT_LENGTH;

  t_sec = t*(UNIT_LENGTH/UNIT_VELOCITY);

  #if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
    // I compute the wall temperature
    Twall = Twall_K / KELVIN;

    // IDIR lines
    for (l=0; l<lines[IDIR].N; l++) {
      j = lines[IDIR].dom_line_idx[l];
      /* :::: Axis ::::*/
      lines[IDIR].lbound[TDIFF][l].kind = NEUMANN_HOM;
      lines[IDIR].lbound[TDIFF][l].values[0] = 0.0;
      // [Opt] (I can avoid making so many if..)
      if (j <= j_cap_inter_end) {
        /* :::: Capillary wall :::: */

        // [Err] Decomment next two lines
        lines[IDIR].rbound[TDIFF][l].kind = DIRICHLET;
        lines[IDIR].rbound[TDIFF][l].values[0] = Twall;

        // [Err] Remove next two lines
        // lines[IDIR].rbound[TDIFF][l].kind = NEUMANN_HOM;
        // lines[IDIR].rbound[TDIFF][l].values[0] = 0.0;
      } else {
        /* :::: Outer domain boundary :::: */
        // lines[IDIR].rbound[TDIFF][l].kind = DIRICHLET;
        // // IS THIS OK?? Before or later change here, and also in its equivalent in init.c
        // lines[IDIR].rbound[TDIFF][l].values[0] = Twall;
        lines[IDIR].rbound[TDIFF][l].kind = NEUMANN_HOM;
        lines[IDIR].rbound[TDIFF][l].values[0] = 0.0;
      }
    }
    // JDIR lines
    for (l=0; l<lines[JDIR].N; l++) {
      i = lines[JDIR].dom_line_idx[l];
      if (i <= i_cap_inter_end){
        /* :::: Capillary internal (symmetry plane) ::::*/

        // [Err] Decomment next two lines
        lines[JDIR].lbound[TDIFF][l].kind = NEUMANN_HOM;
        lines[JDIR].lbound[TDIFF][l].values[0] = 0.0;

        // [Err] Remove next two lines
        // lines[JDIR].lbound[TDIFF][l].kind = DIRICHLET;
        // lines[JDIR].lbound[TDIFF][l].values[0] = Twall;
      } else {
        /* :::: Outer capillary wall ::::*/
        lines[JDIR].lbound[TDIFF][l].kind = DIRICHLET;
        lines[JDIR].lbound[TDIFF][l].values[0] = Twall;
      }
      /* :::: Outer domain boundary ::::*/
      lines[JDIR].rbound[TDIFF][l].kind = NEUMANN_HOM;
      lines[JDIR].rbound[TDIFF][l].values[0] = 0.0;
    }
  #endif

  #if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
    // I compute the wall magnetic field
    unit_Mfield = COMPUTE_UNIT_MFIELD(UNIT_VELOCITY, UNIT_DENSITY);
    curr = current_from_time(t_sec);
    Bwall = BIOTSAV_GAUSS_S_A(curr, RCAP)/unit_Mfield;

    // IDIR lines
    for (l=0; l<lines[IDIR].N; l++) {
      j = lines[IDIR].dom_line_idx[l];
      /* :::: Axis :::: */
      lines[IDIR].lbound[BDIFF][l].kind = DIRICHLET;
      lines[IDIR].lbound[BDIFF][l].values[0] = 0.0;
      if ( j < j_elec_start) {
        /* :::: Capillary wall (no electrode) :::: */
        lines[IDIR].rbound[BDIFF][l].kind = DIRICHLET;
        lines[IDIR].rbound[BDIFF][l].values[0] = Bwall*rcap_real;
      } else if (j >= j_elec_start && j <= j_cap_inter_end) {
        /* :::: Electrode :::: */
        lines[IDIR].rbound[BDIFF][l].kind = DIRICHLET;
        lines[IDIR].rbound[BDIFF][l].values[0] = Bwall*rcap_real * \
            (1 - (grid[JDIR].x_glob[j]-(zcap_real-dzcap_real))/dzcap_real );
        //[Err]
        // lines[IDIR].rbound[BDIFF][l].kind = DIRICHLET;
        // if (grid[JDIR].x_glob[j]>zcap_real-dzcap_real+L) {
        //   lines[IDIR].rbound[BDIFF][l].values[0] = 0;
        // } else {
        //   lines[IDIR].rbound[BDIFF][l].values[0] = Bwall*rcap_real * \
        //     (1 - (grid[JDIR].x_glob[j]-(zcap_real-dzcap_real))/L );
        // }
        // [Err] end err part
      } else {
        /* :::: Outer domain boundary :::: */
        lines[IDIR].rbound[BDIFF][l].kind = DIRICHLET;
        lines[IDIR].rbound[BDIFF][l].values[0] = 0.0;
      }
    }
    // JDIR lines
    for (l=0; l<lines[JDIR].N; l++) {
      i = lines[JDIR].dom_line_idx[l];
      if ( i <= i_cap_inter_end) {
        /* :::: Capillary internal (symmetry plane) :::: */
        lines[JDIR].lbound[BDIFF][l].kind = NEUMANN_HOM;
        lines[JDIR].lbound[BDIFF][l].values[0] = 0.0;
      } else {
        /* :::: Outer capillary wall :::: */
        lines[JDIR].lbound[BDIFF][l].kind = DIRICHLET;
        lines[JDIR].lbound[BDIFF][l].values[0] = 0.0;
      }
      /* :::: Outer domain boundary :::: */
      lines[JDIR].rbound[BDIFF][l].kind = DIRICHLET;
      lines[JDIR].rbound[BDIFF][l].values[0] = 0.0;
    }
  #endif
}

#if THERMAL_CONDUCTION == ALTERNATING_DIRECTION_IMPLICIT
/****************************************************************************
Function to build the Ip,Im,Jp,Jm, CI, CJ (and also dEdT) for the thermal conduction problem
Note that I must make available for outside dEdT, as I will use it later to
advance the energy in a way that conserves the energy
*****************************************************************************/
void BuildIJ_forTC(const Data *d, Grid *grid, Lines *lines,
                   double **Ip, double **Im, double **Jp,
                   double **Jm, double **CI, double **CJ, double **dEdT) {
  int i,j,k;
  int nv, l;
  double kpar, knor, phi; // Thermal conductivity
  double v[NVAR];
  double ****Vc;
  double *inv_dri, *inv_dzi;
  double *zL, *zR;
  double *rL, *rR;
  double *ArR, *ArL;
  double *dVr, *dVz;
  double *r, *z, *theta;
  double T;

  /* -- set a pointer to the primitive vars array --
    I do this because it is done also in other parts of the code
    maybe it makes the program faster or just easier to write/read...*/
  Vc = d->Vc;

  inv_dzi = grid[JDIR].inv_dxi;
  inv_dri = grid[IDIR].inv_dxi;

  theta = grid[KDIR].x;

  r = grid[IDIR].x;
  z = grid[JDIR].x;
  rL = grid[IDIR].xl;
  rR = grid[IDIR].xr;
  zL = grid[JDIR].xl;
  zR = grid[JDIR].xr;
  ArR = grid[IDIR].A;
  ArL = grid[IDIR].A - 1;
  dVr = grid[IDIR].dV;
  dVz = grid[JDIR].dV;

  /*[Opt] This is probably useless, it is here just for debugging purposes*/
  TOT_LOOP(k, j, i) {
    Ip[j][i] = 0.0;
    Im[j][i] = 0.0;
    Jp[j][i] = 0.0;
    Jm[j][i] = 0.0;
    CI[j][i] = 0.0;
    CJ[j][i] = 0.0;
    dEdT[j][i] = 0.0;
  }
  // lines->lidx
  KDOM_LOOP(k) {
    LINES_LOOP(lines[IDIR], l, j, i) {
      /* :::: Ip :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j][i+1]);
      TC_kappa( v, rR[i], z[j], theta[k], &kpar, &knor, &phi);
      Ip[j][i] = knor*ArR[i]*inv_dri[i];
      /* [Opt] Here I could use the already computed k to compute
      also Im[1,i+1] (since it needs k at the same interface).
      Doing so I would reduce the calls to TC_kappa by almost a factor 1/2
      (obviously I could do the same for Im/Ip) */

      /* :::: Im :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j][i-1]);
      TC_kappa( v, rL[i], z[j], theta[k], &kpar, &knor, &phi);
      Im[j][i] = knor*ArL[i]*inv_dri[i-1];

      /* :::: Jp :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j+1][i]);
      TC_kappa( v, r[i], zR[j], theta[k], &kpar, &knor, &phi);
      Jp[j][i] = knor*inv_dzi[j];

      /* :::: Jm :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j-1][i]);
      TC_kappa( v, r[i], zL[j], theta[k], &kpar, &knor, &phi);
      Jm[j][i] = knor*inv_dzi[j-1];
      #ifdef TEST_ADI
        HeatCapacity_test(v, r[i], z[j], theta[k], &(dEdT[j][i]) );
      #else
        if (GetPV_Temperature(v, &(T) )!=0) {
          print1("\nTC_kappa:[Ema] Error computing temperature!");
        }
        HeatCapacity(v, T, &(dEdT[j][i]) );
      #endif
      /* :::: CI :::: */
      CI[j][i] = dEdT[j][i]*dVr[i];
      
      /* :::: CJ :::: */
      CJ[j][i] = dEdT[j][i]*dVz[j];
    }
  }
}

#ifdef TEST_ADI
/**************************************************************************
 * GetHeatCapacity: Computes the derivative dE/dT (E is the internal energy
 * per unit volume, T is the temperature). This function also normalizes
 * the dEdT.
 * ************************************************************************/
void HeatCapacity_test(double *v, double r, double z, double theta, double *dEdT) {
  double c;
//
  /*[Opt] This should be done with a table or something similar (also I should
    make a more general computation as soon as possible)*/
  /*[Opt] I could also include the normalization in the definition so that I
    do not do the computations twice!*/

  // Specific heat (heat to increase of 1 Kelvin 1 gram of Hydrogen)
    // Low temperature case, no ionization
    c = 3/2*(1/CONST_mp)*CONST_kB;
    // High temperature case, full ionization
    // c = 3/2*(2/CONST_mp)*CONST_kB;

    // Heat capacity (heat to increas of 1 Kelvin 1 cm³ of Hydrogen)
    *dEdT = c*v[RHO]*UNIT_DENSITY;


  // Normalization
  *dEdT = (*dEdT)/(UNIT_DENSITY*CONST_kB/CONST_mp);
}
#endif
#endif

#if RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT
/****************************************************************************
Function to build the Ip,Im,Jp,Jm for the electrical resistivity problem
*****************************************************************************/
void BuildIJ_forRes(const Data *d, Grid *grid, Lines *lines,
                   double **Ip, double **Im, double **Jp,
                   double **Jm, double **CI, double **CJ) {
  int i,j,k;
  int nv, l;
  double eta[3]; // Electr. resistivity
  double v[NVAR];
  double ****Vc;
  double *inv_dri, *inv_dzi, *inv_dr, *inv_dz, *r_1;
  double *zL, *zR;
  double *rL, *rR;
  double *r, *z, *theta;

  /* -- set a pointer to the primitive vars array --
    I do this because it is done also in other parts of the code
    maybe it makes the program faster or just easier to write/read...*/
  Vc = d->Vc;

  inv_dzi = grid[JDIR].inv_dxi;
  inv_dri = grid[IDIR].inv_dxi;
  inv_dz = grid[JDIR].inv_dx;
  inv_dr = grid[IDIR].inv_dx;
  r_1 = grid[IDIR].r_1;

  theta = grid[KDIR].x;

  r = grid[IDIR].x;
  z = grid[JDIR].x;
  rL = grid[IDIR].xl;
  rR = grid[IDIR].xr;
  zL = grid[JDIR].xl;
  zR = grid[JDIR].xr;

  /*[Opt] This is probably useless, it is here just for debugging purposes*/
  TOT_LOOP(k, j, i) {
    Ip[j][i] = 0.0;
    Im[j][i] = 0.0;
    Jp[j][i] = 0.0;
    Jm[j][i] = 0.0;
    CI[j][i] = 0.0;
    CJ[j][i] = 0.0;
  }
  // lines->lidx
  KDOM_LOOP(k) {
    LINES_LOOP(lines[IDIR], l, j, i) {
      /* :::: Ip :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j][i+1]);
      /*[Rob] I should give also J to Resistive_eta().
        I don't, but I could simply compute it by modifying easily the GetCurrent()
        function, as it only uses J and B element of the Data structure.
        Even easier: I could change the whole adi implementation and make it
        advance directly d->Vc instead of allocating vectors for T and Br */
      Resistive_eta( v, rR[i], z[j], theta[k], NULL, eta);
      if (eta[0] != eta[1] || eta[1] != eta[2]) {
        print1("Anisotropic resistivity is not implemented in ADI!");
        QUIT_PLUTO(1);
      }
      Ip[j][i] = eta[0]*inv_dr[i]*inv_dri[i]/rR[i];
      /* [Opt] Here I could use the already computed eta to compute
      also Im[1,i+1] (since it needs eta at the same interface).
      Doing so I would reduce the calls to Resistive_eta by almost a factor 1/2
      (obviously I could do the same for Im/Ip) */

      /* :::: Im :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j][i-1]);
      Resistive_eta( v, rL[i], z[j], theta[k], NULL, eta);
      if (eta[0] != eta[1] || eta[1] != eta[2]) {
        print1("Anisotropic resistivity is not implemented in ADI!");
        QUIT_PLUTO(1);
      }
      if (rL[i]!=0.0)
        Im[j][i] = eta[0]*inv_dri[i-1]*inv_dr[i]/rL[i];
      else
        Im[j][i] = eta[0]/(r[i]*r[i])/rR[i];

      /* :::: Jp :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j+1][i]);
      Resistive_eta( v, r[i], zR[j], theta[k], NULL, eta);
      if (eta[0] != eta[1] || eta[1] != eta[2]) {
        print1("Anisotropic resistivity is not implemented in ADI!");
        QUIT_PLUTO(1);
      }
      Jp[j][i] = eta[0]*inv_dz[j]*inv_dzi[j];

      /* :::: Jm :::: */
      for (nv=0; nv<NVAR; nv++)
        v[nv] = 0.5 * (Vc[nv][k][j][i] + Vc[nv][k][j-1][i]);
      Resistive_eta( v, r[i], zL[j], theta[k], NULL, eta);
      Jm[j][i] = eta[0]*inv_dz[j]*inv_dzi[j-1];

      /* :::: CI :::: */
      CI[j][i] = r_1[i];

      /* :::: CJ :::: */
      CJ[j][i] = 1.0;
    }
  }
}
#endif

/****************************************************************************
Performs an implicit update of a diffusive problem (either for B or for T)
*****************************************************************************/
void ImplicitUpdate (double **v, double **b, double **source,
                     double **Hp, double **Hm, double **C,
                     Lines *lines, Bcs *lbound, Bcs *rbound, double dt,
                     int const dir) {
  /*[Opt] Maybe I could pass to this func. an integer which tells which bc has to be
  used inside the structure *lines, instead of passing separately the bcs (which are
  still also contained inside *lines)*/
  /*[Opt] Maybe I could use g_dir instead of passing dir, but I am afraid of
  doing caos modifiying the value of g_dir for the rest of PLUTO*/
  // int *m, *n, *i, *j; // m and n play the role of i and j (not necessarly respectively)
  // int s, dom_line_idx;
  // const int zero=0;
  int i,j;
  int Nlines = lines->N;
  int ridx, lidx, l;
  /* I allocate these as big as if I had to cover the whole domain, so that I
   don't need to reallocate at every domain line that I update */
  double *diagonal, *upper, *lower, *rhs, *x;

  /*[Opt] Maybe I could do that it allocates static arrays with size NMAX_POINT (=max(NX1_TOT,NX2_TOT)) ?*/
  if (dir == IDIR) {
    diagonal = ARRAY_1D(NX1_TOT, double);
    rhs = ARRAY_1D(NX1_TOT, double);
    upper = ARRAY_1D(NX1_TOT, double);
    lower = ARRAY_1D(NX1_TOT, double);
    x = ARRAY_1D(NX1_TOT, double);
  } else if (dir == JDIR) {
    diagonal = ARRAY_1D(NX2_TOT, double);
    rhs = ARRAY_1D(NX2_TOT, double);
    upper = ARRAY_1D(NX2_TOT, double);
    lower = ARRAY_1D(NX2_TOT, double);
    x = ARRAY_1D(NX2_TOT, double);
  }
    /*[Opt] potrei sempificare il programma facendo che
  i membri di destra delle assegnazione prendono degli indici
  che puntano a j o a i a seconda del valore di dir*/
  // if (dir == IDIR) {
  //   j = &dom_line_idx;
  //   m = &zero;
  //   n = &s;
  //   i = &lidx;
  // } else if (dir == JDIR) {
  //   j = &lidx;
  //   m = &s;
  //   n = &zero;
  //   i = &dom_line_idx;
  // }
  // for (l = 0; l<Nlines; l++) {
  //   dom_line_idx = lines->dom_line_idx;
  //   lidx = lines->lidx;
  //   ridx = lines->ridx;
  //   s = lidx;
  //     [*j+*m][*i+*n];
  // }
  if (dir == IDIR) {
  /********************
  * Case direction IDIR
  *********************/
    for (l = 0; l < Nlines; l++) {
      j = lines->dom_line_idx[l];
      lidx = lines->lidx[l];
      ridx = lines->ridx[l];

      upper[lidx] = -dt/C[j][lidx]*Hp[j][lidx];
      lower[ridx] = -dt/C[j][ridx]*Hm[j][ridx];
      rhs[lidx] = b[j][lidx];
      rhs[ridx] = b[j][ridx];
      for (i = lidx+1; i < ridx; i++) {
        diagonal[i] = 1 + dt/C[j][i] * (Hp[j][i]+Hm[j][i]);
        rhs[i] = b[j][i];
        upper[i] = -dt/C[j][i]*Hp[j][i];
        lower[i] = -dt/C[j][i]*Hm[j][i];
      }
      /* I include the effect of the source */
      if (source != NULL) {
        for (i = lidx; i <= ridx; i++)
          rhs[i] += source[j][i]*dt;
      }
      // I set the Bcs for left boundary
      if (lbound[l].kind == DIRICHLET){
        diagonal[lidx] = 1 + dt/C[j][lidx]*(Hp[j][lidx]+2*Hm[j][lidx]);
        rhs[lidx] += dt/C[j][lidx]*Hm[j][lidx]*2*lbound[l].values[0];
      } else if (lbound[l].kind == NEUMANN_HOM) {
        diagonal[lidx] = 1 + dt/C[j][lidx]*Hp[j][lidx];
      } else {
        print1("\n[ImplicitUpdate]Error setting left bc (in dir i), not known bc kind!");
        QUIT_PLUTO(1);
      }
      // I set the Bcs for right boundary
      if (rbound[l].kind == DIRICHLET){
        diagonal[ridx] = 1 + dt/C[j][ridx]*(2*Hp[j][ridx]+Hm[j][ridx]);
        rhs[ridx] += dt/C[j][ridx]*Hp[j][ridx]*2*rbound[l].values[0];
      } else if (rbound[l].kind == NEUMANN_HOM) {
        diagonal[ridx] = 1 + dt/C[j][ridx]*Hm[j][ridx];
      } else {
        print1("\n[ImplicitUpdate]Error setting right bc (in dir i), not known bc kind!");
        QUIT_PLUTO(1);
      }
      // Now I solve the system
      tdm_solver( x+lidx, diagonal+lidx, upper+lidx, lower+lidx+1, rhs+lidx, ridx-lidx+1);
      /*[Opt] Is this for a waste of time? maybe I could engineer better the use of tdm_solver function(or the way it is written)*/
      for (i=lidx; i<=ridx; i++)
        v[j][i] = x[i];
    }
  } else if (dir == JDIR) {
    /********************
    * Case direction JDIR
    *********************/
    for (l = 0; l < Nlines; l++) {
      i = lines->dom_line_idx[l];
      lidx = lines->lidx[l];
      ridx = lines->ridx[l];

      upper[lidx] = -dt/C[lidx][i]*Hp[lidx][i];
      lower[ridx] = -dt/C[ridx][i]*Hm[ridx][i];
      rhs[lidx] = b[lidx][i];
      rhs[ridx] = b[ridx][i];
      for (j = lidx+1; j < ridx; j++) {
        diagonal[j] = 1 + dt/C[j][i] * (Hp[j][i]+Hm[j][i]);
        rhs[j] = b[j][i];
        upper[j] = -dt/C[j][i]*Hp[j][i];
        lower[j] = -dt/C[j][i]*Hm[j][i];
      }
      /* I include the effect of the source */
      if (source != NULL) {
        for (j = lidx; j <= ridx; j++)
          rhs[j] += source[j][i]*dt;
      }
      // I set the Bcs for left boundary
      if (lbound[l].kind == DIRICHLET){
        diagonal[lidx] = 1 + dt/C[lidx][i]*(Hp[lidx][i]+2*Hm[lidx][i]);
        rhs[lidx] += dt/C[lidx][i]*Hm[lidx][i]*2*lbound[l].values[0];
      } else if (lbound[l].kind == NEUMANN_HOM) {
        diagonal[lidx] = 1 + dt/C[lidx][i]*Hp[lidx][i];
      } else {
        print1("\n[ImplicitUpdate]Error setting left bc (in dir j), not known bc kind!");
        QUIT_PLUTO(1);
      }
      // I set the Bcs for right boundary
      if (rbound[l].kind == DIRICHLET){
        diagonal[ridx] = 1 + dt/C[ridx][i]*(2*Hp[ridx][i]+Hm[ridx][i]);
        rhs[ridx] += dt/C[ridx][i]*Hp[ridx][i]*2*rbound[l].values[0];
      } else if (rbound[l].kind == NEUMANN_HOM) {
        diagonal[ridx] = 1 + dt/C[ridx][i]*Hm[ridx][i];
      } else {
        print1("\n[ImplicitUpdate]Error setting right bcs (in dir j), not known bc kind!");
        QUIT_PLUTO(1);
      }
      // Now I solve the system
      tdm_solver( x+lidx, diagonal+lidx, upper+lidx, lower+lidx+1, rhs+lidx, ridx-lidx+1);
      for (j=lidx; j<=ridx; j++)
        v[j][i] = x[j];
    }
  } else {
    print1("[ImplicitUpdate] Unimplemented choice for 'dir'!");
    QUIT_PLUTO(1);
  }

  FreeArray1D(diagonal);
  FreeArray1D(x);
  FreeArray1D(upper);
  FreeArray1D(lower);
  FreeArray1D(rhs);
}
//
/****************************************************************************
Performs an explicit update of a diffusive problem (either for B or for T)
*****************************************************************************/
void ExplicitUpdate (double **v, double **b, double **source,
                     double **Hp, double **Hm, double **C,
                     Lines *lines, Bcs *lbound, Bcs *rbound, double dt,
                     int const dir) {
  int i,j,l;
  int ridx, lidx;
  int Nlines = lines->N;
  double b_ghost;
  static double **rhs;
  static int first_call = 1;

  if (first_call) {
    rhs = ARRAY_2D(NX2_TOT, NX1_TOT, double);
    first_call = 0;
  }

  if (dir == IDIR) {
    /********************
    * Case direction IDIR
    *********************/
    for (l = 0; l < Nlines; l++) {
      j = lines->dom_line_idx[l];
      lidx = lines->lidx[l];
      ridx = lines->ridx[l];

      if (source != NULL) {
        for (i = lidx; i <= ridx; i++)
          rhs[j][i] = b[j][i] + source[j][i]*dt;
      } else {
        /*[Opt] Maybe I could assign directly the address. BE CAREFUL:
        if I assign the address, then I have to recover the old address of rhs (by saving temporarly the old rhs address
        inside another variable), otherwise
        at the next call of this function I will write over the memory of the old b*/
        for (i = lidx; i <= ridx; i++)
          rhs[j][i] = b[j][i];
      }

      // Actual update
      for (i = lidx+1; i < ridx; i++)
        v[j][i] = rhs[j][i] + dt/C[j][i] * (b[j][i+1]*Hp[j][i] - b[j][i]*(Hp[j][i]+Hm[j][i]) + b[j][i-1]*Hm[j][i]);
      // Cells near left boundary
      if (lbound[l].kind == DIRICHLET){
        b_ghost = 2*lbound[l].values[0] - b[j][lidx];
        v[j][lidx] = rhs[j][lidx] + dt/C[j][lidx] * (b[j][lidx+1]*Hp[j][lidx] - b[j][lidx]*(Hp[j][lidx]+Hm[j][lidx]) + b_ghost*Hm[j][lidx]);
      } else if (lbound[l].kind == NEUMANN_HOM) {
        v[j][lidx] = rhs[j][lidx] + dt/C[j][lidx] * (b[j][lidx+1]*Hp[j][lidx] - b[j][lidx]*Hp[j][lidx]);
      } else {
        print1("\n[ExplicitUpdate]Error setting left bc (in dir i), not known bc kind!");
        QUIT_PLUTO(1);
      }
      // Cells near right boundary
      if (rbound[l].kind == DIRICHLET){
        b_ghost = 2*rbound[l].values[0] - b[j][ridx];
        v[j][ridx] = rhs[j][ridx] + dt/C[j][ridx] * (b_ghost*Hp[j][ridx] - b[j][ridx]*(Hp[j][ridx]+Hm[j][ridx]) + b[j][ridx-1]*Hm[j][ridx]);
      } else if (rbound[l].kind == NEUMANN_HOM) {
        v[j][ridx] = rhs[j][ridx] + dt/C[j][ridx] * (-b[j][ridx]*Hm[j][ridx] + b[j][ridx-1]*Hm[j][ridx]);
      } else {
        print1("\n[ExplicitUpdate]Error setting right bc (in dir i), not known bc kind!");
        QUIT_PLUTO(1);
      }
    }
  } else if (dir == JDIR) {
    /********************
    * Case direction JDIR
    *********************/
    for (l = 0; l < Nlines; l++) {
      i = lines->dom_line_idx[l];
      lidx = lines->lidx[l];
      ridx = lines->ridx[l];

      if (source != NULL) {
        for (j = lidx; j <= ridx; j++)
          rhs[j][i] = b[j][i] + source[j][i]*dt;
      } else {
        /*[Opt] Maybe I could assign directly the address. BE CAREFUL:
        if I assign the address, then I have to recover the old address of rhs (by saving temporarly the old rhs address
        inside another variable), otherwise
        at the next call of this function I will write over the memory of the old b*/
        for (j = lidx; j <= ridx; j++)
          rhs[j][i] = b[j][i];
      }

      // Actual update
      for (j = lidx+1; j < ridx; j++)
        v[j][i] = rhs[j][i] + dt/C[j][i] * (b[j+1][i]*Hp[j][i] - b[j][i]*(Hp[j][i]+Hm[j][i]) + b[j-1][i]*Hm[j][i]);
      // Cells near left boundary
      if (lbound[l].kind == DIRICHLET){
        b_ghost = 2*lbound[l].values[0] - b[lidx][i];
        v[lidx][i] = rhs[lidx][i] + dt/C[lidx][i] * (b[lidx+1][i]*Hp[lidx][i] - b[lidx][i]*(Hp[lidx][i]+Hm[lidx][i]) + b_ghost*Hm[lidx][i]);
      } else if (lbound[l].kind == NEUMANN_HOM) {
        v[lidx][i] = rhs[lidx][i] + dt/C[lidx][i] * (b[lidx+1][i]*Hp[lidx][i] - b[lidx][i]*Hp[lidx][i]);
      } else {
        print1("\n[ExplicitUpdate]Error setting left bc (in dir j), not known bc kind!");
        QUIT_PLUTO(1);
      }
      // Cells near right boundary
      if (rbound[l].kind == DIRICHLET){
        b_ghost = 2*rbound[l].values[0] - b[ridx][i];
        v[ridx][i] = rhs[ridx][i] + dt/C[ridx][i] * (b_ghost*Hp[ridx][i] - b[ridx][i]*(Hp[ridx][i]+Hm[ridx][i]) + b[ridx-1][i]*Hm[ridx][i]);
      } else if (rbound[l].kind == NEUMANN_HOM) {
        v[ridx][i] = rhs[ridx][i] + dt/C[ridx][i] * (-b[ridx][i]*Hm[ridx][i] + b[ridx-1][i]*Hm[ridx][i]);
      } else {
        print1("\n[ExplicitUpdate]Error setting right bc (in dir j), not known bc kind!");
        QUIT_PLUTO(1);
      }
    }
  } else {
    print1("[ImplicitUpdate] Unimplemented choice for 'dir'!");
    QUIT_PLUTO(1);
  }
}

#if (RESISTIVITY == ALTERNATING_DIRECTION_IMPLICIT && HAVE_ENERGY && JOULE_EFFECT_AND_MAG_ENG)
/****************************************************************************
Function to build the a matrix which contain the amount of increase of the
energy due to joule effect and magnetic field energy (flux of poynting vector due to
resistive magnetic diffusion)
[Opt]: Note that in the actual implementation this function needs to take as
input both the Hp_B and the Hm_B. But for the whole internal (i.e. boudary excluded)
only one among Hp_B and Hm_B is necessary. The other one is used to fill F
at one side (left or right) of the domain.
*****************************************************************************/
void ResEnergyIncrease(double **dUres, double** Hp_B, double** Hm_B, double **Br,
                       Grid *grid, Lines *lines, double dt, int dir){
  /* F :Power flux flowing from cell (i,j) to (i+1,j), when dir==IDIR;
        or from cell (i,j) to (i,j+1), when dir == JDIR.
  */
  static double **F;
  double *dr, *dz;
  int i,j,l;
  int lidx, ridx;
  int Nlines = lines->N;
  int static first_call = 1;
  double *dV, *inv_dz, *r_1, *r;
  double *rL, *rR;
  double Br_ghost;
  Bcs *rbound, *lbound;

  /*[Opt] Maybe I could do that it allocates static arrays with size NMAX_POINT (=max(NX1_TOT,NX2_TOT)) ?*/
  if (first_call) {
    /* I define it 2d in case I need to export it later*/
    F = ARRAY_2D(NX2_TOT, NX1_TOT, double);
    /*This is useless, it's just for debugging purposes*/
    ITOT_LOOP(i)
      JTOT_LOOP(j)
        F[j][i] = 0.0;
    first_call = 0;
  }
  /*This is useless, it's just for debugging purposes*/
  ITOT_LOOP(i)
    JTOT_LOOP(j)
      dUres[j][i] = 0.0;

  lbound = lines->lbound[BDIFF];
  rbound = lines->rbound[BDIFF];
  dr = grid[IDIR].dx;
  r_1 = grid[IDIR].r_1;

  if (dir == IDIR) {
    rR = grid[IDIR].xr;
    rL = grid[IDIR].xl;
    dV = grid[IDIR].dV;
    r = grid[IDIR].x_glob;

    for (l = 0; l<Nlines; l++) {
      j = lines->dom_line_idx[l];
      lidx = lines->lidx[l];
      ridx = lines->ridx[l];
      for (i=lidx; i<ridx; i++) // I stop before ridx, as for the last interface(as for the first) I must use the BCs
        F[j][i] = -Hp_B[j][i] * (Br[j][i+1] - Br[j][i])*dr[i] * 0.5*(Br[j][i+1]*r_1[i+1] + Br[j][i]*r_1[i]);
      /*Define boundary fluxes*/
      if (lbound[l].kind == DIRICHLET){
        // [Err] Delete next two lines
        Br_ghost = r[lidx-1] * (0 - Br[j][lidx]*r_1[lidx]);
        // [Err] Decomment next line (original)
        // Br_ghost = 2*lbound[l].values[0] - Br[j][lidx];
        // [Err] Remove next line (this is a test)
        // Br_ghost = lbound[l].values[0];
        // [Err] Original verison (decomment)
        // F[j][lidx-1] = -Hm_B[j][lidx] * (Br[j][lidx] - Br_ghost)*dr[lidx] * 0.5*(Br[j][lidx]*r_1[lidx] + Br_ghost*r_1[lidx-1]);
        // [Err] Remove next line
        F[j][lidx-1] = -Hm_B[j][lidx] * (Br[j][lidx] - Br_ghost)*dr[lidx] * 0.0;
      } else if (lbound[l].kind == NEUMANN_HOM) {
        F[j][lidx-1] = 0.0;
      }
      if (rbound[l].kind == DIRICHLET){
        // [Err] Delete next two lines
        Br_ghost = r[ridx+1] * (2*rbound[l].values[0]/rR[ridx] - Br[j][ridx]*r_1[ridx]);
        // [Err] Decomment next 2 lines and comment previous (original)
        // Br_ghost = 2*rbound[l].values[0] - Br[j][ridx];

        // [Err] Remove next line (test)
        // Br_ghost = rbound[l].values[0];

        F[j][ridx] = -Hp_B[j][ridx] * (Br_ghost - Br[j][ridx])*dr[ridx] * 0.5*(Br_ghost*r_1[ridx+1] + Br[j][ridx]*r_1[ridx]);
      } else if (rbound[l].kind == NEUMANN_HOM) {
        F[j][ridx] = 0.0;
      }
      // Build dU
      for (i=lidx; i<=ridx; i++)
       //[Err] delete questa linea
        // dUres[j][i] = (rR[i]*F[j][i] - rL[i]*F[j][i-1])*dt/dV[i];
       //[Err] deccoment next line
        dUres[j][i] = -(rR[i]*F[j][i] - rL[i]*F[j][i-1])*dt/dV[i];
    }

  } else if (dir == JDIR) {
    dz = grid[JDIR].dx;
    inv_dz = grid[JDIR].inv_dx;

    for (l = 0; l<Nlines; l++) {
      i = lines->dom_line_idx[l];
      lidx = lines->lidx[l];
      ridx = lines->ridx[l];
      
      for (j=lidx; j<ridx; j++)
        //[Err] ho aggiunto *r_1[i] nella formula
        F[j][i] = -Hp_B[j][i] * (Br[j+1][i] - Br[j][i])*dz[j]*r_1[i]*r_1[i] * 0.5*(Br[j+1][i] + Br[j][i]);

      /*Define boundary fluxes*/
      if (lbound[l].kind == DIRICHLET){
        Br_ghost = 2*lbound[l].values[0] - Br[lidx][i];
        //[Err] ho aggiunto *r_1[i] nella formula
        F[lidx-1][i] = -Hm_B[lidx][i] * (Br[lidx][i] - Br_ghost)*dz[lidx] * 0.5*r_1[i]*r_1[i]*(Br[lidx][i] + Br_ghost);
      } else if (lbound[l].kind == NEUMANN_HOM) {
        F[lidx-1][i] = 0.0;
      }
      if (rbound[l].kind == DIRICHLET){
        Br_ghost = 2*rbound[l].values[0] - Br[ridx][i];
        //[Err] ho aggiunto *r_1[i] nella formula
        F[ridx][i] = -Hp_B[ridx][i] * (Br_ghost - Br[ridx][i])*dz[ridx] * 0.5*r_1[i]*r_1[i]*(Br_ghost + Br[ridx][i]);
      } else if (rbound[l].kind == NEUMANN_HOM) {
        F[ridx][i] = 0.0;
      }
      // Build dU
      for (j=lidx; j<=ridx; j++)
              //[Err] delete questa linea
        // dUres[j][i] = (F[j][i] - F[j-1][i])*dt*inv_dz[j];
        //[Err] deccoment next line
        dUres[j][i] = -(F[j][i] - F[j-1][i])*dt*inv_dz[j];
    }
  }
}
#endif

/****************************************************************************
Function to initialize lines, I hope this kind of initialization is ok and
there are no problems with data continuity and similar things
*****************************************************************************/
void InitializeLines(Lines *lines, int N){
  int i;

  lines->dom_line_idx = ARRAY_1D(N, int);
  lines->lidx = ARRAY_1D(N, int);
  lines->ridx = ARRAY_1D(N, int);
  lines->N = N;
  for (i=0; i<NADI; i++) {
    lines->lbound[i] = ARRAY_1D(N, Bcs);
    lines->rbound[i] = ARRAY_1D(N, Bcs);
  }
}

/****************************************************************************
Function to build geometrical parameters belonging to lines
[Rob] Maybe it's better to move this to another file? the one containing init()??
*****************************************************************************/
void GeometryADI(Lines *lines, Grid *grid){
  int i,j;

  // Use the number of rows and cols internal to the domain to define adi lines
  InitializeLines(&lines[IDIR], NX2); // I have NX2 lines sweeping the i-direction
  InitializeLines(&lines[JDIR], NX1); // I have NX1 lines sweeping the j-direction

  // A couple of cross-checks:
  if (2*grid[JDIR].nghost+NX2 != NX2_TOT) {
    print1("Something wrong with the line definition/not understood how the grid[DIR] is made!");
    QUIT_PLUTO(1);
  }
  if (2*grid[IDIR].nghost+NX1 != NX1_TOT) {
    print1("Something wrong with the line definition/not understood how the grid[JDIR] is made!");
    QUIT_PLUTO(1);
  }

  // Use remarkable capillary indexes to define the lines
  for (j=0;j<lines[IDIR].N;j++)
    lines[IDIR].dom_line_idx[j] = j + grid[JDIR].nghost;
  for (j=0;j<lines[IDIR].N;j++){
    lines[IDIR].lidx[j] = grid[IDIR].nghost;
    if (lines[IDIR].dom_line_idx[j] <= j_cap_inter_end){
      lines[IDIR].ridx[j] = i_cap_inter_end;
    } else {
      lines[IDIR].ridx[j] = NX1_TOT - 1 - grid[IDIR].nghost;
    }
  }
  for (i=0;i<lines[JDIR].N;i++)
    lines[JDIR].dom_line_idx[i] = i + grid[IDIR].nghost;
  for (i=0;i<lines[JDIR].N;i++){
    lines[JDIR].ridx[i] = NX2_TOT - 1 - grid[JDIR].nghost;
    if (lines[JDIR].dom_line_idx[i] <= i_cap_inter_end){
      lines[JDIR].lidx[i] = grid[JDIR].nghost;
    } else {
      lines[JDIR].lidx[i] = j_cap_inter_end+1;
    }
  }
}

/************************************************************
 * Solve a linear system made by a tridiagonal matrix.
 * See  https://en.wikipedia.org/wiki/Tridiagonal_matrix_algorithm
 * for info (accessed on 24/3/2017)
 *
 * N: the size of the arrays x, diagonal, right_hand_side.
 * x: solution
 * *********************************************************/
void tdm_solver(double *x, double const *diagonal, double const *upper,
                double const *lower, double const *right_hand_side, int const N) {
    /*[Opt] Is it really needed to define two new arrays?
            Maybe I can make that it uses directly the diagonal, upper,
            lower arrays, modifying them, the only problem is that I
            don't like the idea as a principle, that the function
            modifies its actual input with no apparent reason*/
    double up[N-1];
    double rhs[N];
    int i;

    /*[Opt] Maybe there is another way to copy the arrays... more clever, shorter, faster..*/
    for (i=0;i<N;i++)
      rhs[i] = right_hand_side[i];
    for (i=0;i<N-1;i++)
      up[i] = upper[i];

    up[0] = upper[0]/diagonal[0];
    for (i=1; i<N-1; i++)
      up[i] = up[i] / (diagonal[i]-lower[i-1]*up[i-1]);

    rhs[0] = rhs[0]/diagonal[0];
    for (i=1; i<N; i++)
      rhs[i] = (rhs[i] - lower[i-1]*rhs[i-1]) / (diagonal[i] - lower[i-1]*up[i-1]);

    x[N-1] = rhs[N-1];
    for (i=N-2; i>-1; i--)
      x[i] = rhs[i] - up[i]*x[i+1];
  }

/*******************************************************
 * COSE DA FARE, ma che sono secondarie:
 *
 * 0) Mettere a zero all'inizio i Jp,Ip,Jm,Im,C alla prima chiamata?
 * 1) Mettere un po' di cicli #if di controllo che la geometria
 *    il modello (MHD) e altro siano ok.
 * 2) Pensare alla compatabilità con STS o EXPL
 * 3) Capire se come è scritto va bene anche per griglia stretchata
 *    (è chiaro che comunque se stretcho la griglia lentamente va bene,
 *    ma rigorosamente parlando, va bene? Forse devo ragionare sullo sviluppo di taylor
 *    per trovarmi le derivate delle incognite sui punti di griglia)
 * 4) Pensare ad un warning in caso di eta con componenti diverse tra loro
 * 5) Alberto dice di provare dopo eventualemente (se vedo probelmi o se voglio migliorare accuratezzax) a far aggiornare a t+dt/2 Jmp,Imp
 * 6) fare che viene stampate nell'output di pluto anche il dt parabolico che ci sarebbe con step esplicito
 * 7) Attenzione alla griglia stretchata: forse devo cambiare la
 *    discretizzazione delle derivate se voglio usare rigorosamente una griglia stretchata
 * 8) Le bc devono essere impostate in modo almeno parzialmente coerente con come sono fatte nel resto del programma:
 *    ovvero, se ho impostato al runtime OUTFLOW, REFLECTIVE.. (tranne al più USERDEF)
 *    le condizioni dell'adi dovranno essere concordi a ciò (rimane la questione di come imporre
 *    facilmente le bc per il caso userdef)
 ********************************************************/
