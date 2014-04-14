/* Last changed Time-stamp: <2009-02-24 15:17:17 ivo> */
/*
                  minimum free energy folding
                  for a set of aligned sequences

                  c Ivo Hofacker

                  Vienna RNA package
*/

/**
*** \file alifold.c
**/


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/pair_mat.h"
#include "ViennaRNA/data_structures.h"
#include "ViennaRNA/fold.h"
#include "ViennaRNA/utils.h"
#include "ViennaRNA/energy_par.h"
#include "ViennaRNA/params.h"
#include "ViennaRNA/ribo.h"
#include "ViennaRNA/gquad.h"
#include "ViennaRNA/alifold.h"
#include "ViennaRNA/aln_util.h"
#include "ViennaRNA/loop_energies.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#define MAXSECTORS    500   /* dimension for a backtrack array */

/*
#################################
# GLOBAL VARIABLES              #
#################################
*/

/*
#################################
# PRIVATE VARIABLES             #
#################################
*/

/* some backward compatibility stuff */
PRIVATE vrna_fold_compound  *backward_compat_compound = NULL;
PRIVATE int                 backward_compat           = 0;

#ifdef _OPENMP

#pragma omp threadprivate(backward_compat_compound, backward_compat)

#endif

/*
#################################
# PRIVATE FUNCTION DECLARATIONS #
#################################
*/

PRIVATE int     fill_arrays(vrna_fold_compound *vc);
PRIVATE void    fill_arrays_circ(vrna_fold_compound *vc, sect bt_stack[], int *bt);
PRIVATE void    backtrack(vrna_fold_compound *vc, bondT *bp_stack, sect bt_stack[], int s);

PRIVATE void    energy_of_alistruct_pt(vrna_fold_compound *vc, short * ptable, int *energy);
PRIVATE void    stack_energy_pt(vrna_fold_compound *vc, int i, short *ptable, int *energy);
PRIVATE int     ML_Energy_pt(vrna_fold_compound *vc, int i, short *pt);
PRIVATE int     EL_Energy_pt(vrna_fold_compound *vc, int i, short *pt);

PRIVATE void    en_corr_of_loop_gquad(vrna_fold_compound *vc,
                                      int i,
                                      int j,
                                      const char *structure,
                                      short *pt,
                                      int *loop_idx,
                                      int en[2]);

PRIVATE float   wrap_alifold( const char **strings,
                              char *structure,
                              paramT *parameters,
                              int is_constrained,
                              int is_circular);

/*
#################################
# BEGIN OF FUNCTION DEFINITIONS #
#################################
*/

PRIVATE float
wrap_alifold( const char **strings,
              char *structure,
              paramT *parameters,
              int is_constrained,
              int is_circular){

  vrna_fold_compound  *vc;
  paramT              *P;

#ifdef _OPENMP
/* Explicitly turn off dynamic threads */
  omp_set_dynamic(0);
#endif

  /* we need the parameter structure for hard constraints */
  if(parameters){
    P = get_parameter_copy(parameters);
  } else {
    model_detailsT md;
    set_model_details(&md);
    P = get_scaled_parameters(temperature, md);
  }
  P->model_details.circ = is_circular;

  vc = vrna_get_fold_compound_ali(strings, &(P->model_details), VRNA_OPTION_MFE);

  if(parameters){ /* replace params if necessary */
    free(vc->params);
    vc->params = P;
  } else {
    free(P);
  }

  /* handle hard constraints in pseudo dot-bracket format if passed via simple interface */
  if(is_constrained && structure){
    unsigned int constraint_options = 0;
    constraint_options |= VRNA_CONSTRAINT_DB
                          | VRNA_CONSTRAINT_PIPE
                          | VRNA_CONSTRAINT_DOT
                          | VRNA_CONSTRAINT_X
                          | VRNA_CONSTRAINT_ANG_BRACK
                          | VRNA_CONSTRAINT_RND_BRACK;

    vrna_hc_add(vc, (const char *)structure, constraint_options);
  }

  if(backward_compat_compound && backward_compat)
    destroy_fold_compound(backward_compat_compound);

  backward_compat_compound  = vc;
  backward_compat           = 1;

  return vrna_ali_fold(vc, structure);
}



PUBLIC float
vrna_ali_fold(vrna_fold_compound *vc,
              char *structure){

  int  length, s, n_seq, energy;
  sect    bt_stack[MAXSECTORS]; /* stack of partial structures for backtracking */
  bondT *bp;

  length      = vc->length;
  n_seq       = vc->n_seq;
  s           = 0;

  energy = fill_arrays(vc);
  if(vc->params->model_details.circ){
    fill_arrays_circ(vc, bt_stack, &s);
    energy = vc->matrices->Fc;
  }

  if(structure && vc->params->model_details.backtrack){
    bp = (bondT *)space(sizeof(bondT) * (4*(1+length/2))); /* add a guess of how many G's may be involved in a G quadruplex */

    backtrack(vc, bp, bt_stack, s);

    vrna_parenthesis_structure(structure, bp, length);

    /*
    *  Backward compatibility:
    *  This block may be removed if deprecated functions
    *  relying on the global variable "base_pair" vanishs from within the package!
    */
    {
      if(base_pair) free(base_pair);
      base_pair = bp;
    }
  }

  if (vc->params->model_details.backtrack_type=='C')
    return (float) vc->matrices->c[vc->jindx[length]+1]/(n_seq*100.);
  else if (vc->params->model_details.backtrack_type=='M')
    return (float) vc->matrices->fML[vc->jindx[length]+1]/(n_seq*100.);
  else
    return (float) energy/(n_seq*100.);
}


/**
*** the actual forward recursion to fill the energy arrays
**/
PRIVATE int
fill_arrays(vrna_fold_compound *vc){

  int   i, j, k, p, q, energy, new_c;
  int   decomp, MLenergy, new_fML;
  int   s, *type, type_2, tt;
  int   *cc;        /* linear array for calculating canonical structures */
  int   *cc1;       /*   "     "        */
  int   *Fmi;       /* holds row i of fML (avoids jumps in memory) */
  int   *DMLi;      /* DMLi[j] holds MIN(fML[i,k]+fML[k+1,j])  */
  int   *DMLi1;     /*             MIN(fML[i+1,k]+fML[k+1,j])  */
  int   *DMLi2;     /*             MIN(fML[i+2,k]+fML[k+1,j])  */


  int             n_seq         = vc->n_seq;
  int             length        = vc->length;
  short           **S           = vc->S;                                                                   
  short           **S5          = vc->S5;     /*S5[s][i] holds next base 5' of i in sequence s*/            
  short           **S3          = vc->S3;     /*Sl[s][i] holds next base 3' of i in sequence s*/            
  char            **Ss          = vc->Ss;                                                                   
  unsigned short  **a2s         = vc->a2s;                                                                   
  paramT          *P            = vc->params;                                                                
  model_detailsT  *md           = &(P->model_details);
  int             *indx         = vc->jindx;     /* index for moving in the triangle matrices c[] and fMl[]*/
  int             *c            = vc->matrices->c;     /* energy array, given that i-j pair */                 
  int             *f5           = vc->matrices->f5;     /* energy of 5' end */                                  
  int             *fML          = vc->matrices->fML;     /* multi-loop auxiliary energy array */                 
  int             *ggg          = vc->matrices->ggg;
  int             *pscore       = vc->pscore;     /* precomputed array of pair types */                      
  short           *S_cons       = vc->S_cons;
  int             *rtype        = &(md->rtype[0]);
  int             dangle_model  = md->dangles;

  hard_constraintT *hc          = vc->hc;
  soft_constraintT **sc         = vc->scs;

  type  = (int *) space(n_seq*sizeof(int));
  cc    = (int *) space(sizeof(int)*(length+2));
  cc1   = (int *) space(sizeof(int)*(length+2));
  Fmi   = (int *) space(sizeof(int)*(length+1));
  DMLi  = (int *) space(sizeof(int)*(length+1));
  DMLi1 = (int *) space(sizeof(int)*(length+1));
  DMLi2 = (int *) space(sizeof(int)*(length+1));

  /* init energies */

  int max_bpspan = (md->max_bp_span > 0) ? md->max_bp_span : length;

  for (j=1; j<=length; j++){
    Fmi[j]=DMLi[j]=DMLi1[j]=DMLi2[j]=INF;
    for (i=(j>TURN?(j-TURN):1); i<j; i++) {
      c[indx[j]+i] = fML[indx[j]+i] = INF;
    }
  }

  /* begin recursions */
  for (i = length-TURN-1; i >= 1; i--) { /* i,j in [1..length] */
    for (j = i+TURN+1; j <= length; j++) {
      int ij, psc, l1, maxq, minq, c0;
      ij = indx[j]+i;

      for (s=0; s<n_seq; s++) {
        type[s] = md->pair[S[s][i]][S[s][j]];
        if (type[s]==0) type[s]=7;
      }

      psc = pscore[indx[j]+i];
      if (psc>=MINPSCORE && ((j - i) < max_bpspan)) {   /* a pair to consider */
        int stackEnergy = INF;
        /* hairpin ----------------------------------------------*/


        for (new_c=s=0; s<n_seq; s++) {
          if ((a2s[s][j-1]-a2s[s][i])<3) new_c+=600;
          else  new_c += E_Hairpin(a2s[s][j-1]-a2s[s][i],type[s],S3[s][i],S5[s][j],Ss[s]+(a2s[s][i-1]), P);
          if(sc){
            if(sc[s]->en_basepair)
              new_c += sc[s]->en_basepair[indx[a2s[s][j]] + a2s[s][i]];
          }
        }
        /*--------------------------------------------------------
          check for elementary structures involving more than one
          closing pair.
          --------------------------------------------------------*/

        for (p = i+1; p <= MIN2(j-2-TURN,i+MAXLOOP+1) ; p++) {
          minq = j-i+p-MAXLOOP-2;
          if (minq<p+1+TURN) minq = p+1+TURN;
          for (q = minq; q < j; q++) {
            if (pscore[indx[q]+p]<MINPSCORE) continue;
            for (energy = s=0; s<n_seq; s++) {
              type_2 = md->pair[S[s][q]][S[s][p]]; /* q,p not p,q! */
              if (type_2 == 0) type_2 = 7;
              energy += E_IntLoop(a2s[s][p-1]-a2s[s][i], a2s[s][j-1]-a2s[s][q], type[s], type_2,
                                   S3[s][i], S5[s][j],
                                   S5[s][p], S3[s][q], P);
              if(sc){
                if(sc[s]->en_basepair)
                  energy += sc[s]->en_basepair[indx[a2s[s][j]] + a2s[s][i]]
                            + sc[s]->en_basepair[indx[a2s[s][q]] + a2s[s][p]];
              }
            }
            if ((p==i+1)&&(j==q+1)){
              if(sc){
                for(s = 0; s < n_seq; s++)
                  if(sc[s]->en_stack){
                    energy +=   sc[s]->en_stack[i]
                              + sc[s]->en_stack[p]
                              + sc[s]->en_stack[q]
                              + sc[s]->en_stack[j];
                  }
                }
              stackEnergy = energy; /* remember stack energy */
            }
            new_c = MIN2(new_c, energy + c[indx[q]+p]);
          } /* end q-loop */
        } /* end p-loop */

        /* multi-loop decomposition ------------------------*/
        decomp = DMLi1[j-1];
        if(dangle_model){
          for(s=0; s<n_seq; s++){
            tt = rtype[type[s]];
            decomp += E_MLstem(tt, S5[s][j], S3[s][i], P);
          }
        }
        else{
          for(s=0; s<n_seq; s++){
            tt = rtype[type[s]];
            decomp += E_MLstem(tt, -1, -1, P);
          }
        }
        if(sc)
          for(s=0; s<n_seq; s++){
            if(sc[s]->en_basepair)
              decomp += sc[s]->en_basepair[indx[a2s[s][j]] + a2s[s][i]];
        }
        MLenergy = decomp + n_seq*P->MLclosing;
        new_c = MIN2(new_c, MLenergy);

        if(md->gquad){
          decomp = 0;
          for(s=0;s<n_seq;s++){
            tt = type[s];
            if(dangle_model == 2)
              decomp += P->mismatchI[tt][S3[s][i]][S5[s][j]];
            if(tt > 2)
              decomp += P->TerminalAU;
          }
          for(p = i + 2; p < j - VRNA_GQUAD_MIN_BOX_SIZE; p++){
            l1    = p - i - 1;
            if(l1>MAXLOOP) break;
            if(S_cons[p] != 3) continue;
            minq  = j - i + p - MAXLOOP - 2;
            c0    = p + VRNA_GQUAD_MIN_BOX_SIZE - 1;
            minq  = MAX2(c0, minq);
            c0    = j - 1;
            maxq  = p + VRNA_GQUAD_MAX_BOX_SIZE + 1;
            maxq  = MIN2(c0, maxq);
            for(q = minq; q < maxq; q++){
              if(S_cons[q] != 3) continue;
              c0    = decomp + ggg[indx[q] + p] + n_seq * P->internal_loop[l1 + j - q - 1];
              new_c = MIN2(new_c, c0);
            }
          }

          p = i + 1;
          if(S_cons[p] == 3){
            if(p < j - VRNA_GQUAD_MIN_BOX_SIZE){
              minq  = j - i + p - MAXLOOP - 2;
              c0    = p + VRNA_GQUAD_MIN_BOX_SIZE - 1;
              minq  = MAX2(c0, minq);
              c0    = j - 3;
              maxq  = p + VRNA_GQUAD_MAX_BOX_SIZE + 1;
              maxq  = MIN2(c0, maxq);
              for(q = minq; q < maxq; q++){
                if(S_cons[q] != 3) continue;
                c0  = decomp + ggg[indx[q] + p] + n_seq * P->internal_loop[j - q - 1];
                new_c   = MIN2(new_c, c0);
              }
            }
          }
          q = j - 1;
          if(S_cons[q] == 3)
            for(p = i + 4; p < j - VRNA_GQUAD_MIN_BOX_SIZE; p++){
              l1    = p - i - 1;
              if(l1>MAXLOOP) break;
              if(S_cons[p] != 3) continue;
              c0  = decomp + ggg[indx[q] + p] + n_seq * P->internal_loop[l1];
              new_c   = MIN2(new_c, c0);
            }
        }

        new_c = MIN2(new_c, cc1[j-1]+stackEnergy);

        cc[j] = new_c - psc; /* add covariance bonnus/penalty */
        if (md->noLP)
          c[ij] = cc1[j-1]+stackEnergy-psc;
        else
          c[ij] = cc[j];

      } /* end >> if (pair) << */

      else c[ij] = INF;
      /* done with c[i,j], now compute fML[i,j] */
      /* free ends ? -----------------------------------------*/

      new_fML = fML[ij+1] + n_seq * P->MLbase;
      new_fML = MIN2(fML[indx[j-1]+i]+n_seq*P->MLbase, new_fML);
      energy = c[ij];
      if(dangle_model){
        for (s=0; s<n_seq; s++) {
          energy += E_MLstem(type[s], S5[s][i], S3[s][j], P);
        }
      }
      else{
        for (s=0; s<n_seq; s++) {
          energy += E_MLstem(type[s], -1, -1, P);
        }
      }
      new_fML = MIN2(energy, new_fML);

      if(md->gquad){
        decomp = ggg[indx[j] + i] + n_seq * E_MLstem(0, -1, -1, P);
        new_fML = MIN2(new_fML, decomp);
      }

      /* modular decomposition -------------------------------*/
      for (decomp = INF, k = i+1+TURN; k <= j-2-TURN; k++)
        decomp = MIN2(decomp, Fmi[k]+fML[indx[j]+k+1]);

      DMLi[j] = decomp;               /* store for use in ML decompositon */
      new_fML = MIN2(new_fML,decomp);

      /* coaxial stacking deleted */

      fML[ij] = Fmi[j] = new_fML;     /* substring energy */
    } /* END for j */

    {
      int *FF; /* rotate the auxilliary arrays */
      FF = DMLi2; DMLi2 = DMLi1; DMLi1 = DMLi; DMLi = FF;
      FF = cc1; cc1=cc; cc=FF;
      for (j=1; j<=length; j++) {cc[j]=Fmi[j]=DMLi[j]=INF; }
    }
  } /* END for i */
  /* calculate energies of 5' and 3' fragments */

  f5[TURN + 1] = 0;
  switch(dangle_model){
    case 0:   for(j = TURN + 2; j <= length; j++){
                f5[j] = f5[j-1];
                if (c[indx[j]+1]<INF){
                  energy = c[indx[j]+1];
                  for(s = 0; s < n_seq; s++){
                    tt = md->pair[S[s][1]][S[s][j]];
                    if(tt==0) tt=7;
                    energy += E_ExtLoop(tt, -1, -1, P);
                  }
                  f5[j] = MIN2(f5[j], energy);
                }

                if(md->gquad){
                  if(ggg[indx[j]+1] < INF)
                    f5[j] = MIN2(f5[j], ggg[indx[j]+1]);
                }

                for(i = j - TURN - 1; i > 1; i--){
                  if(c[indx[j]+i]<INF){
                    energy = f5[i-1] + c[indx[j]+i];
                    for(s = 0; s < n_seq; s++){
                      tt = md->pair[S[s][i]][S[s][j]];
                      if(tt==0) tt=7;
                      energy += E_ExtLoop(tt, -1, -1, P);
                    }
                    f5[j] = MIN2(f5[j], energy);
                  }

                  if(md->gquad){
                    if(ggg[indx[j]+i] < INF)
                      f5[j] = MIN2(f5[j], f5[i-1] + ggg[indx[j]+i]);
                  }

                }
              }
              break;
    default:  for(j = TURN + 2; j <= length; j++){
                f5[j] = f5[j-1];
                if (c[indx[j]+1]<INF) {
                  energy = c[indx[j]+1];
                  for(s = 0; s < n_seq; s++){
                    tt = md->pair[S[s][1]][S[s][j]];
                    if(tt==0) tt=7;
                    energy += E_ExtLoop(tt, -1, (j<length) ? S3[s][j] : -1, P);
                  }
                  f5[j] = MIN2(f5[j], energy);
                }

                if(md->gquad){
                  if(ggg[indx[j]+1] < INF)
                    f5[j] = MIN2(f5[j], ggg[indx[j]+1]);
                }

                for(i = j - TURN - 1; i > 1; i--){
                  if (c[indx[j]+i]<INF) {
                    energy = f5[i-1] + c[indx[j]+i];
                    for(s = 0; s < n_seq; s++){
                      tt = md->pair[S[s][i]][S[s][j]];
                      if(tt==0) tt=7;
                      energy += E_ExtLoop(tt, S5[s][i], (j < length) ? S3[s][j] : -1, P);
                    }
                    f5[j] = MIN2(f5[j], energy);
                  }

                  if(md->gquad){
                    if(ggg[indx[j]+i] < INF)
                      f5[j] = MIN2(f5[j], f5[i-1] + ggg[indx[j]+i]);
                  }

                }
              }
              break;
  }
  free(type);
  free(cc);
  free(cc1);
  free(Fmi);
  free(DMLi);
  free(DMLi1);
  free(DMLi2);
  return(f5[length]);
}

#include "ViennaRNA/alicircfold.inc"

/**
*** backtrack in the energy matrices to obtain a structure with MFE
**/
PRIVATE void
backtrack(vrna_fold_compound *vc,
          bondT *bp_stack,
          sect bt_stack[],
          int s) {

  /*------------------------------------------------------------------
    trace back through the "c", "f5" and "fML" arrays to get the
    base pairing list. No search for equivalent structures is done.
    This inverts the folding procedure, hence it's very fast.
    ------------------------------------------------------------------*/
   /* normally s=0.
     If s>0 then s items have been already pushed onto the sector stack */

  int  i, j, k, p, q, energy, c0, l1, minq, maxq, type_2, tt, mm, b=0, cov_en = 0, *type;

  int             n_seq         = vc->n_seq;
  int             length        = vc->length;
  short           **S           = vc->S;                                                                   
  short           **S5          = vc->S5;     /*S5[s][i] holds next base 5' of i in sequence s*/            
  short           **S3          = vc->S3;     /*Sl[s][i] holds next base 3' of i in sequence s*/            
  char            **Ss          = vc->Ss;                                                                   
  unsigned short  **a2s         = vc->a2s;                                                                   
  paramT          *P            = vc->params;                                                                
  model_detailsT  *md           = &(P->model_details);
  int             *indx         = vc->jindx;     /* index for moving in the triangle matrices c[] and fMl[]*/
  int             *c            = vc->matrices->c;     /* energy array, given that i-j pair */                 
  int             *f5           = vc->matrices->f5;     /* energy of 5' end */                                  
  int             *fML          = vc->matrices->fML;     /* multi-loop auxiliary energy array */                 
  int             *pscore       = vc->pscore;     /* precomputed array of pair types */                      
  int             *ggg          = vc->matrices->ggg;
  short           *S_cons       = vc->S_cons;
  int             *rtype        = &(md->rtype[0]);
  int             dangle_model  = md->dangles;

  hard_constraintT *hc          = vc->hc;
  soft_constraintT **sc         = vc->scs;


  type = (int *) space(n_seq*sizeof(int));

  if (s==0) {
    bt_stack[++s].i = 1;
    bt_stack[s].j = length;
    bt_stack[s].ml = (md->backtrack_type=='M') ? 1 : ((md->backtrack_type=='C')?2:0);
  }
  while (s>0) {
    int ss, ml, fij, fi, cij, traced, i1, j1, jj=0, gq=0;
    int canonical = 1;     /* (i,j) closes a canonical structure */
    i  = bt_stack[s].i;
    j  = bt_stack[s].j;
    ml = bt_stack[s--].ml;   /* ml is a flag indicating if backtracking is to
                              occur in the fML- (1) or in the f-array (0) */
    if (ml==2) {
      bp_stack[++b].i = i;
      bp_stack[b].j   = j;
      cov_en += pscore[indx[j]+i];
      goto repeat1;
    }

    if (j < i+TURN+1) continue; /* no more pairs in this interval */

    fij = (ml)? fML[indx[j]+i] : f5[j];
    fi  = (ml)?(fML[indx[j-1]+i]+n_seq*P->MLbase):f5[j-1];

    if (fij == fi) {  /* 3' end is unpaired */
      bt_stack[++s].i = i;
      bt_stack[s].j   = j-1;
      bt_stack[s].ml  = ml;
      continue;
    }

    if (ml == 0) { /* backtrack in f5 */
      switch(dangle_model){
        case 0:   /* j or j-1 is paired. Find pairing partner */
                  for (i=j-TURN-1,traced=0; i>=1; i--) {
                    int en;
                    jj = i-1;
                    if (c[indx[j]+i]<INF) {
                      en = c[indx[j]+i] + f5[i-1];
                      for(ss = 0; ss < n_seq; ss++){
                        type[ss] = md->pair[S[ss][i]][S[ss][j]];
                        if (type[ss]==0) type[ss] = 7;
                        en += E_ExtLoop(type[ss], -1, -1, P);
                      }
                      if (fij == en) traced=j;
                    }

                    if(md->gquad){
                      if(fij == f5[i-1] + ggg[indx[j]+i]){
                        /* found the decomposition */
                        traced = j; jj = i - 1; gq = 1;
                        break;
                      }
                    }

                    if (traced) break;
                  }
                  break;
        default:  /* j or j-1 is paired. Find pairing partner */
                  for (i=j-TURN-1,traced=0; i>=1; i--) {
                    int en;
                    jj = i-1;
                    if (c[indx[j]+i]<INF) {
                      en = c[indx[j]+i] + f5[i-1];
                      for(ss = 0; ss < n_seq; ss++){
                        type[ss] = md->pair[S[ss][i]][S[ss][j]];
                        if (type[ss]==0) type[ss] = 7;
                        en += E_ExtLoop(type[ss], (i>1) ? S5[ss][i]: -1, (j < length) ? S3[ss][j] : -1, P);
                      }
                      if (fij == en) traced=j;
                    }

                    if(md->gquad){
                      if(fij == f5[i-1] + ggg[indx[j]+i]){
                        /* found the decomposition */
                        traced = j; jj = i - 1; gq = 1;
                        break;
                      }
                    }

                    if (traced) break;
                  }
                  break;
      }

      if (!traced) nrerror("backtrack failed in f5");
      /* push back the remaining f5 portion */
      bt_stack[++s].i = 1;
      bt_stack[s].j   = jj;
      bt_stack[s].ml  = ml;

      /* trace back the base pair found */
      j=traced;

      if(md->gquad && gq){
        /* goto backtrace of gquadruplex */
        goto repeat_gquad;
      }

      bp_stack[++b].i = i;
      bp_stack[b].j   = j;
      cov_en += pscore[indx[j]+i];
      goto repeat1;
    }
    else { /* trace back in fML array */
      if (fML[indx[j]+i+1]+n_seq*P->MLbase == fij) { /* 5' end is unpaired */
        bt_stack[++s].i = i+1;
        bt_stack[s].j   = j;
        bt_stack[s].ml  = ml;
        continue;
      }

      if(md->gquad){
        if(fij == ggg[indx[j]+i] + n_seq * E_MLstem(0, -1, -1, P)){
          /* go to backtracing of quadruplex */
          goto repeat_gquad;
        }
      }

      cij = c[indx[j]+i];
      if(dangle_model){
        for(ss = 0; ss < n_seq; ss++){
          tt = md->pair[S[ss][i]][S[ss][j]];
          if(tt==0) tt=7;
          cij += E_MLstem(tt, S5[ss][i], S3[ss][j], P);
        }
      }
      else{
        for(ss = 0; ss < n_seq; ss++){
          tt = md->pair[S[ss][i]][S[ss][j]];
          if(tt==0) tt=7;
          cij += E_MLstem(tt, -1, -1, P);
        }
      }

      if (fij==cij){
        /* found a pair */
        bp_stack[++b].i = i;
        bp_stack[b].j   = j;
        cov_en += pscore[indx[j]+i];
        goto repeat1;
      }

      for (k = i+1+TURN; k <= j-2-TURN; k++)
        if (fij == (fML[indx[k]+i]+fML[indx[j]+k+1]))
          break;

      bt_stack[++s].i = i;
      bt_stack[s].j   = k;
      bt_stack[s].ml  = ml;
      bt_stack[++s].i = k+1;
      bt_stack[s].j   = j;
      bt_stack[s].ml  = ml;

      if (k>j-2-TURN) nrerror("backtrack failed in fML");
      continue;
    }

  repeat1:

    /*----- begin of "repeat:" -----*/
    if (canonical)  cij = c[indx[j]+i];

    for (ss=0; ss<n_seq; ss++) {
      type[ss] = md->pair[S[ss][i]][S[ss][j]];
      if (type[ss]==0) type[ss] = 7;
    }

    if (md->noLP)
      if (cij == c[indx[j]+i]) {
        /* (i.j) closes canonical structures, thus
           (i+1.j-1) must be a pair                */
        for (ss=0; ss<n_seq; ss++) {
          type_2 = md->pair[S[ss][j-1]][S[ss][i+1]];  /* j,i not i,j */
          if (type_2==0) type_2 = 7;
          cij -= P->stack[type[ss]][type_2];
          if(sc){
            if(sc[ss]->en_basepair)
              cij -= sc[s]->en_basepair[indx[a2s[ss][j]] + a2s[ss][i]];
          }
        }
        cij += pscore[indx[j]+i];
        bp_stack[++b].i = i+1;
        bp_stack[b].j   = j-1;
        cov_en += pscore[indx[j-1]+i+1];
        i++; j--;
        canonical=0;
        goto repeat1;
      }
    canonical = 1;
    cij += pscore[indx[j]+i];

    {int cc=0;
    for (ss=0; ss<n_seq; ss++) {
        if ((a2s[ss][j-1]-a2s[ss][i])<3) cc+=600;
        else cc += E_Hairpin(a2s[ss][j-1]-a2s[ss][i], type[ss], S3[ss][i], S5[ss][j], Ss[ss]+a2s[ss][i-1], P);
      }
    if (cij == cc) /* found hairpin */
      continue;
    }
    for (p = i+1; p <= MIN2(j-2-TURN,i+MAXLOOP+1); p++) {
      minq = j-i+p-MAXLOOP-2;
      if (minq<p+1+TURN) minq = p+1+TURN;
      for (q = j-1; q >= minq; q--) {

        if (c[indx[q]+p]>=INF) continue;

        for (ss=energy=0; ss<n_seq; ss++) {
          type_2 = md->pair[S[ss][q]][S[ss][p]];  /* q,p not p,q */
          if (type_2==0) type_2 = 7;
          energy += E_IntLoop(a2s[ss][p-1]-a2s[ss][i],a2s[ss][j-1]-a2s[ss][q],
                               type[ss], type_2,
                               S3[ss][i], S5[ss][j],
                               S5[ss][p], S3[ss][q], P);

        }
        if ((p==i+1)&&(j==q+1)){
          if(sc){
            for(ss = 0; ss < n_seq; ss++)
              if(sc[ss]->en_stack){
                energy +=   sc[ss]->en_stack[i]
                          + sc[ss]->en_stack[p]
                          + sc[ss]->en_stack[q]
                          + sc[ss]->en_stack[j];
              }
            }
        }
        traced = (cij == energy+c[indx[q]+p]);
        if (traced) {
          bp_stack[++b].i = p;
          bp_stack[b].j   = q;
          cov_en += pscore[indx[q]+p];
          i = p, j = q;
          goto repeat1;
        }
      }
    }

    /* end of repeat: --------------------------------------------------*/

    /* (i.j) must close a multi-loop */

    i1 = i+1;
    j1 = j-1;

    if(md->gquad){
      /*
        The case that is handled here actually resembles something like
        an interior loop where the enclosing base pair is of regular
        kind and the enclosed pair is not a canonical one but a g-quadruplex
        that should then be decomposed further...
      */
      mm = 0;
      for(s=0;s<n_seq;s++){
        tt = type[s];
        if(tt == 0) tt = 7;
        if(dangle_model == 2)
          mm += P->mismatchI[tt][S3[s][i]][S5[s][j]];
        if(tt > 2)
          mm += P->TerminalAU;
      }

      for(p = i + 2;
        p < j - VRNA_GQUAD_MIN_BOX_SIZE;
        p++){
        if(S_cons[p] != 3) continue;
        l1    = p - i - 1;
        if(l1>MAXLOOP) break;
        minq  = j - i + p - MAXLOOP - 2;
        c0    = p + VRNA_GQUAD_MIN_BOX_SIZE - 1;
        minq  = MAX2(c0, minq);
        c0    = j - 1;
        maxq  = p + VRNA_GQUAD_MAX_BOX_SIZE + 1;
        maxq  = MIN2(c0, maxq);
        for(q = minq; q < maxq; q++){
          if(S_cons[q] != 3) continue;
          c0  = mm + ggg[indx[q] + p] + n_seq * P->internal_loop[l1 + j - q - 1];
          if(cij == c0){
            i=p;j=q;
            goto repeat_gquad;
          }
        }
      }
      p = i1;
      if(S_cons[p] == 3){
        if(p < j - VRNA_GQUAD_MIN_BOX_SIZE){
          minq  = j - i + p - MAXLOOP - 2;
          c0    = p + VRNA_GQUAD_MIN_BOX_SIZE - 1;
          minq  = MAX2(c0, minq);
          c0    = j - 3;
          maxq  = p + VRNA_GQUAD_MAX_BOX_SIZE + 1;
          maxq  = MIN2(c0, maxq);
          for(q = minq; q < maxq; q++){
            if(S_cons[q] != 3) continue;
            if(cij == mm + ggg[indx[q] + p] + n_seq * P->internal_loop[j - q - 1]){
              i = p; j=q;
              goto repeat_gquad;
            }
          }
        }
      }
      q = j1;
      if(S_cons[q] == 3)
        for(p = i1 + 3; p < j - VRNA_GQUAD_MIN_BOX_SIZE; p++){
          l1    = p - i - 1;
          if(l1>MAXLOOP) break;
          if(S_cons[p] != 3) continue;
          if(cij == mm + ggg[indx[q] + p] + n_seq * P->internal_loop[l1]){
            i = p; j = q;
            goto repeat_gquad;
          }
        }
    }

    mm = n_seq*P->MLclosing;
    if(dangle_model){
      for(ss = 0; ss < n_seq; ss++){
        tt = rtype[type[ss]];
        mm += E_MLstem(tt, S5[ss][j], S3[ss][i], P);
      }
    }
    else{
      for(ss = 0; ss < n_seq; ss++){
        tt = rtype[type[ss]];
        mm += E_MLstem(tt, -1, -1, P);
      }
    }
    bt_stack[s+1].ml  = bt_stack[s+2].ml = 1;

    for (k = i1+TURN+1; k < j1-TURN-1; k++){
      if(cij == fML[indx[k]+i1] + fML[indx[j1]+k+1] + mm) break;
    }

    if (k<=j-3-TURN) { /* found the decomposition */
      bt_stack[++s].i = i1;
      bt_stack[s].j   = k;
      bt_stack[++s].i = k+1;
      bt_stack[s].j   = j1;
    } else {
        nrerror("backtracking failed in repeat");
    }

    continue; /* this is a workarround to not accidentally proceed in the following block */

  repeat_gquad:
    /*
      now we do some fancy stuff to backtrace the stacksize and linker lengths
      of the g-quadruplex that should reside within position i,j
    */
    {
      int cnt1, l[3], L, size;
      size = j-i+1;

      for(L=0; L < VRNA_GQUAD_MIN_STACK_SIZE;L++){
        if(S_cons[i+L] != 3) break;
        if(S_cons[j-L] != 3) break;
      }

      if(L == VRNA_GQUAD_MIN_STACK_SIZE){
        /* continue only if minimum stack size starting from i is possible */
        for(; L<=VRNA_GQUAD_MAX_STACK_SIZE;L++){
          if(S_cons[i+L-1] != 3) break; /* break if no more consecutive G's 5' */
          if(S_cons[j-L+1] != 3) break; /* break if no more consecutive G'1 3' */
          for(    l[0] = VRNA_GQUAD_MIN_LINKER_LENGTH;
                  (l[0] <= VRNA_GQUAD_MAX_LINKER_LENGTH)
              &&  (size - 4*L - 2*VRNA_GQUAD_MIN_LINKER_LENGTH - l[0] >= 0);
              l[0]++){
            /* check whether we find the second stretch of consecutive G's */
            for(cnt1 = 0; (cnt1 < L) && (S_cons[i+L+l[0]+cnt1] == 3); cnt1++);
            if(cnt1 < L) continue;
            for(    l[1] = VRNA_GQUAD_MIN_LINKER_LENGTH;
                    (l[1] <= VRNA_GQUAD_MAX_LINKER_LENGTH)
                &&  (size - 4*L - VRNA_GQUAD_MIN_LINKER_LENGTH - l[0] - l[1] >= 0);
                l[1]++){
              /* check whether we find the third stretch of consectutive G's */
              for(cnt1 = 0; (cnt1 < L) && (S_cons[i+2*L+l[0]+l[1]+cnt1] == 3); cnt1++);
              if(cnt1 < L) continue;

              /*
                the length of the third linker now depends on position j as well
                as the other linker lengths... so we do not have to loop too much
              */
              l[2] = size - 4*L - l[0] - l[1];
              if(l[2] < VRNA_GQUAD_MIN_LINKER_LENGTH) break;
              if(l[2] > VRNA_GQUAD_MAX_LINKER_LENGTH) continue;
              /* check for contribution */
              if(ggg[indx[j]+i] == E_gquad_ali(i, L, l, (const short **)S, n_seq, P)){
                int a;
                /* fill the G's of the quadruplex into base_pair2 */
                for(a=0;a<L;a++){
                  bp_stack[++b].i = i+a;
                  bp_stack[b].j   = i+a;
                  bp_stack[++b].i = i+L+l[0]+a;
                  bp_stack[b].j   = i+L+l[0]+a;
                  bp_stack[++b].i = i+L+l[0]+L+l[1]+a;
                  bp_stack[b].j   = i+L+l[0]+L+l[1]+a;
                  bp_stack[++b].i = i+L+l[0]+L+l[1]+L+l[2]+a;
                  bp_stack[b].j   = i+L+l[0]+L+l[1]+L+l[2]+a;
                }
                goto repeat_gquad_exit;
              }
            }
          }
        }
      }
      nrerror("backtracking failed in repeat_gquad");
    }
  repeat_gquad_exit:
    asm("nop");

  }

  /* fprintf(stderr, "covariance energy %6.2f\n", cov_en/100.);  */

  bp_stack[0].i = b;    /* save the total number of base pairs */
  free(type);
}

PUBLIC int
vrna_ali_get_mpi( char *Alseq[],
                  int n_seq,
                  int length,
                  int *mini){

  int   i, j, k, pairnum = 0, sumident = 0;
  float ident = 0, minimum = 1.;

  for(j=0; j<n_seq-1; j++)
    for(k=j+1; k<n_seq; k++) {
      ident=0;
      for (i=1; i<=length; i++){
        if (Alseq[k][i]==Alseq[j][i]) ident++;
        pairnum++;
      }
      if ((ident/length)<minimum) minimum=ident/(float)length;
      sumident+=ident;
    }
  mini[0]=(int)(minimum*100.);
  if (pairnum>0)   return (int) (sumident*100/pairnum);
  else return 0;

}

PRIVATE void
en_corr_of_loop_gquad(vrna_fold_compound *vc,
                      int i,
                      int j,
                      const char *structure,
                      short *pt,
                      int *loop_idx,
                      int en[2]){

  int pos, energy, en_covar, p, q, r, s, u, type, type2, gq_en[2];
  int L, l[3];

  short           **S           = vc->S;                                                                   
  short           **S5          = vc->S5;     /*S5[s][i] holds next base 5' of i in sequence s*/            
  short           **S3          = vc->S3;     /*Sl[s][i] holds next base 3' of i in sequence s*/            
  char            **Ss          = vc->Ss;                                                                   
  unsigned short  **a2s         = vc->a2s;                                                                   
  paramT          *P            = vc->params;                                                                
  model_detailsT  *md           = &(P->model_details);
  int             n_seq         = vc->n_seq;
  int             dangle_model  = md->dangles;

  energy = en_covar = 0;
  q = i;
  while((pos = parse_gquad(structure + q-1, &L, l)) > 0){
    q += pos-1;
    p = q - 4*L - l[0] - l[1] - l[2] + 1;
    if(q > j) break;
    /* we've found the first g-quadruplex at position [p,q] */
    E_gquad_ali_en(p, L, l, (const short **)S, n_seq, gq_en, P);
    energy    += gq_en[0];
    en_covar  += gq_en[1];
    /* check if it's enclosed in a base pair */
    if(loop_idx[p] == 0){ q++; continue; /* g-quad in exterior loop */}
    else{
      energy += E_MLstem(0, -1, -1, P) * n_seq;
      /*  find its enclosing pair */
      int num_elem, num_g, elem_i, elem_j, up_mis;
      num_elem  = 0;
      num_g     = 1;
      r         = p - 1;
      up_mis    = q - p + 1;

      /* seek for first pairing base located 5' of the g-quad */
      for(r = p - 1; !pt[r] && (r >= i); r--);
      if(r < i) nrerror("this should not happen");

      if(r < pt[r]){ /* found the enclosing pair */
        s = pt[r];
      } else {
        num_elem++;
        elem_i = pt[r];
        elem_j = r;
        r = pt[r]-1 ;
        /* seek for next pairing base 5' of r */
        for(; !pt[r] && (r >= i); r--);
        if(r < i) nrerror("so nich");
        if(r < pt[r]){ /* found the enclosing pair */
          s = pt[r];
        } else {
          /* hop over stems and unpaired nucleotides */
          while((r > pt[r]) && (r >= i)){
            if(pt[r]){ r = pt[r]; num_elem++;}
            r--;
          }
          if(r < i) nrerror("so nich");
          s = pt[r]; /* found the enclosing pair */
        }
      }
      /* now we have the enclosing pair (r,s) */

      u = q+1;
      /* we know everything about the 5' part of this loop so check the 3' part */
      while(u<s){
        if(structure[u-1] == '.') u++;
        else if (structure[u-1] == '+'){ /* found another gquad */
          pos = parse_gquad(structure + u - 1, &L, l);
          if(pos > 0){
            E_gquad_ali_en(u, L, l, (const short **)S, n_seq, gq_en, P);
            energy += gq_en[0] + E_MLstem(0, -1, -1, P) * n_seq;
            en_covar += gq_en[1];
            up_mis += pos;
            u += pos;
            num_g++;
          }
        } else { /* we must have found a stem */
          if(!(u < pt[u])) nrerror("wtf!");
          num_elem++;
          elem_i = u;
          elem_j = pt[u];
          en_corr_of_loop_gquad(vc,
                                u,
                                pt[u],
                                structure,
                                pt,
                                loop_idx,
                                gq_en);
          energy    += gq_en[0];
          en_covar  += gq_en[1];
          u = pt[u] + 1;
        }
      }
      if(u!=s) nrerror("what the ...");
      else{ /* we are done since we've found no other 3' structure element */
        switch(num_elem){
          /* g-quad was misinterpreted as hairpin closed by (r,s) */
          case 0:   /*if(num_g == 1)
                      if((p-r-1 == 0) || (s-q-1 == 0))
                        nrerror("too few unpaired bases");
                    */
                    {
                      int ee = 0;
                      int cnt;
                      for(cnt=0;cnt<n_seq;cnt++){
                        type = md->pair[S[cnt][r]][S[cnt][s]];
                        if(type == 0) type = 7;
                        if ((a2s[cnt][s-1]-a2s[cnt][r])<3) ee+=600;
                        else ee += E_Hairpin( a2s[cnt][s-1] - a2s[cnt][r],
                                              type,
                                              S3[cnt][r],
                                              S5[cnt][s],
                                              Ss[cnt] + a2s[cnt][r-1],
                                              P);
                      }
                      energy -= ee;
                      ee = 0;
                      for(cnt=0;cnt < n_seq; cnt++){
                        type = md->pair[S[cnt][r]][S[cnt][s]];
                        if(type == 0) type = 7;
                        if(dangle_model == 2)
                          ee += P->mismatchI[type][S3[cnt][r]][S5[cnt][s]];
                        if(type > 2)
                          ee += P->TerminalAU;
                      }
                      energy += ee;
                    }
                    energy += n_seq * P->internal_loop[s-r-1-up_mis];
                    break;
          /* g-quad was misinterpreted as interior loop closed by (r,s) with enclosed pair (elem_i, elem_j) */
          case 1:   {
                      int ee = 0;
                      int cnt;
                      for(cnt = 0; cnt<n_seq;cnt++){
                        type = md->pair[S[cnt][r]][S[cnt][s]];
                        if(type == 0) type = 7;
                        type2 = md->pair[S[cnt][elem_j]][S[cnt][elem_i]];
                        if(type2 == 0) type2 = 7;
                        ee += E_IntLoop(a2s[cnt][elem_i-1] - a2s[cnt][r],
                                        a2s[cnt][s-1] - a2s[cnt][elem_j],
                                        type,
                                        type2,
                                        S3[cnt][r],
                                        S5[cnt][s],
                                        S5[cnt][elem_i],
                                        S3[cnt][elem_j],
                                        P);
                      }
                      energy -= ee;
                      ee = 0;
                      for(cnt = 0; cnt < n_seq; cnt++){
                        type = md->pair[S[cnt][s]][S[cnt][r]];
                        if(type == 0) type = 7;
                        ee += E_MLstem(type, S5[cnt][s], S3[cnt][r], P);
                        type = md->pair[S[cnt][elem_i]][S[cnt][elem_j]];
                        if(type == 0) type = 7;
                        ee += E_MLstem(type, S5[cnt][elem_i], S3[cnt][elem_j], P);
                      }
                      energy += ee;
                    }
                    energy += (P->MLclosing + (elem_i-r-1+s-elem_j-1-up_mis) * P->MLbase) * n_seq;
                    break;
          /* gquad was misinterpreted as unpaired nucleotides in a multiloop */
          default:  energy -= (up_mis) * P->MLbase * n_seq;
                    break;
        }
      }
      q = s+1;
    }
  }
  en[0] = energy;
  en[1] = en_covar;
}

PRIVATE void energy_of_alistruct_pt(vrna_fold_compound *vc, short *pt, int *energy){

  int length        = vc->length;
  int i;

  energy[0] =  vc->params->model_details.backtrack_type=='M' ? ML_Energy_pt(vc, 0, pt) : EL_Energy_pt(vc, 0, pt);
  energy[1] = 0;
  for (i=1; i<=length; i++) {
    if (pt[i]==0) continue;
    stack_energy_pt(vc, i, pt, energy);
    i=pt[i];
  }
}

PRIVATE void
stack_energy_pt(vrna_fold_compound *vc, int i, short *pt, int *energy){

  /* calculate energy of substructure enclosed by (i,j) */
  short           **S         = vc->S;                                                                       
  short           **S5        = vc->S5;     /*S5[s][i] holds next base 5' of i in sequence s*/               
  short           **S3        = vc->S3;     /*Sl[s][i] holds next base 3' of i in sequence s*/               
  char            **Ss        = vc->Ss;                                                                      
  unsigned short  **a2s       = vc->a2s;                                                                     
  paramT          *P          = vc->params;                                                                  
  model_detailsT  *md         = &(P->model_details);
  int             *indx       = vc->jindx;     /* index for moving in the triangle matrices c[] and fMl[]*/  
  int             *pscore     = vc->pscore;     /* precomputed array of pair types */                        
  int             n_seq       = vc->n_seq;                                                                   

  int ee= 0;
  int j, p, q, s;
  int *type = (int *) space(n_seq*sizeof(int));

  j = pt[i];
  for (s=0; s<n_seq; s++) {
    type[s] = md->pair[S[s][i]][S[s][j]];
    if (type[s]==0) {
    type[s]=7;
    }
  }
  p=i; q=j;
  while (p<q) { /* process all stacks and interior loops */
    int type_2;
    while (pt[++p]==0);
    while (pt[--q]==0);
    if ((pt[q]!=(short)p)||(p>q)) break;
    ee=0;
    for (s=0; s<n_seq; s++) {
      type_2 = md->pair[S[s][q]][S[s][p]];
      if (type_2==0) {
        type_2=7;
      }
      ee += E_IntLoop(a2s[s][p-1]-a2s[s][i], a2s[s][j-1]-a2s[s][q], type[s], type_2, S3[s][i], S5[s][j], S5[s][p], S3[s][q], P);
    }
    energy[0] += ee;
    energy[1] += pscore[indx[j]+i];
    i=p; j=q;
    for (s=0; s<n_seq; s++) {
      type[s] = md->pair[S[s][i]][S[s][j]];
      if (type[s]==0) type[s]=7;
    }
  }  /* end while */

  /* p,q don't pair must have found hairpin or multiloop */

  if (p>q) {
    ee=0;/* hair pin */
    for (s=0; s< n_seq; s++) {
      if ((a2s[s][j-1]-a2s[s][i])<3) ee+=600;
      else ee += E_Hairpin(a2s[s][j-1]-a2s[s][i], type[s], S3[s][i], S5[s][j], Ss[s]+(a2s[s][i-1]), P);
    }
    energy[0] += ee;
    energy[1] += pscore[indx[j]+i];
    free(type);
    return;
  }
  /* (i,j) is exterior pair of multiloop */
  energy[1] += pscore[indx[j]+i];
  while (p<j) {
    /* add up the contributions of the substructures of the ML */
    stack_energy_pt(vc, p, pt, energy);
    p = pt[p];
    /* search for next base pair in multiloop */
    while (pt[++p]==0);
  }
  energy[0] += ML_Energy_pt(vc, i, pt);
  free(type);
}



PRIVATE int ML_Energy_pt(vrna_fold_compound *vc, int i, short *pt){

  /* i is the 5'-base of the closing pair */

  int   energy, tt, i1, j, p, q, u, s;
  short d5, d3;

  short           **S           = vc->S;                                                                     
  short           **S5          = vc->S5;     /*S5[s][i] holds next base 5' of i in sequence s*/             
  short           **S3          = vc->S3;     /*Sl[s][i] holds next base 3' of i in sequence s*/             
  unsigned short  **a2s         = vc->a2s;                                                                   
  paramT          *P            = vc->params;                                                                
  model_detailsT  *md           = &(P->model_details);
  int             n_seq         = vc->n_seq;                                                                 
  int             dangle_model  = md->dangles;

  j = pt[i];
  i1  = i;
  p   = i+1;
  u   = 0;
  energy = 0;

  do{ /* walk around the multi-loop */
    /* hop over unpaired positions */
    while (p < j && pt[p]==0) p++;
    if(p >= j) break;
    /* get position of pairing partner */
    q  = pt[p];
    /* memorize number of unpaired positions? no, we just approximate here */
    u += p-i1-1;

    for (s=0; s< n_seq; s++) {
      /* get type of base pair P->q */
      tt = md->pair[S[s][p]][S[s][q]];
      if (tt==0) tt=7;
      d5 = dangle_model && (a2s[s][p]>1) && (tt!=0) ? S5[s][p] : -1;
      d3 = dangle_model && (a2s[s][q]<a2s[s][S[0][0]]) ? S3[s][q] : -1;
      energy += E_MLstem(tt, d5, d3, P);
    }
    i1  = q;
    p   = q + 1;
  }while(1);

  if(i > 0){
    energy  += P->MLclosing * n_seq;
    if(dangle_model){
      for (s=0; s<n_seq; s++){
        tt = md->pair[S[s][j]][S[s][i]];
        if (tt==0) tt=7;
        energy += E_MLstem(tt, S5[s][j], S3[s][i], P);
      }
    }
    else{
      for (s=0; s<n_seq; s++){
        tt = md->pair[S[s][j]][S[s][i]];
        if (tt==0) tt=7;
        energy += E_MLstem(tt, -1, -1, P);
      }
    }
  }
  u += j - i1 - 1;
  energy += u * P->MLbase * n_seq;
  return energy;
}

PRIVATE int EL_Energy_pt(vrna_fold_compound *vc, int i, short *pt){
  int   energy, tt, j, p, q, s;
  short d5, d3;

  short           **S           = vc->S;                                                                     
  short           **S5          = vc->S5;     /*S5[s][i] holds next base 5' of i in sequence s*/             
  short           **S3          = vc->S3;     /*Sl[s][i] holds next base 3' of i in sequence s*/             
  unsigned short  **a2s         = vc->a2s;                                                                   
  paramT          *P            = vc->params;                                                                
  model_detailsT  *md           = &(P->model_details);
  int             n_seq         = vc->n_seq;                                                                 
  int             dangle_model  = md->dangles;

  j = pt[0];

  p   = i+1;
  energy = 0;

  do{ /* walk along the backbone */
    /* hop over unpaired positions */
    while (p < j && pt[p]==0) p++;
    if(p == j) break; /* no more stems */
    /* get position of pairing partner */
    q  = pt[p];
    for (s=0; s< n_seq; s++) {
      /* get type of base pair P->q */
      tt = md->pair[S[s][p]][S[s][q]];
      if (tt==0) tt=7;
      d5 = dangle_model && (a2s[s][p]>1) && (tt!=0) ? S5[s][p] : -1;
      d3 = dangle_model && (a2s[s][q]<a2s[s][S[0][0]]) ? S3[s][q] : -1;
      energy += E_ExtLoop(tt, d5, d3, P);
    }
    p   = q + 1;
  }while(p < j);

  return energy;
}

/*###########################################*/
/*# deprecated functions below              #*/
/*###########################################*/

PUBLIC void
free_alifold_arrays(void){

  if(backward_compat_compound && backward_compat){
    destroy_fold_compound(backward_compat_compound);
    backward_compat_compound  = NULL;
    backward_compat           = 0;
  }
}

PUBLIC float
alifold(const char **strings,
        char *structure){

  return wrap_alifold(strings, structure, NULL, fold_constrained, 0);
}

PUBLIC float circalifold( const char **strings,
                          char *structure) {

  return wrap_alifold(strings, structure, NULL, fold_constrained, 1);
}

PUBLIC void 
update_alifold_params(void){

  vrna_fold_compound *v;

  if(backward_compat_compound && backward_compat){
    v = backward_compat_compound;

    if(v->params)
      free(v->params);

    model_detailsT md;
    set_model_details(&md);
    v->params = vrna_get_energy_contributions(md);
  }
}

PUBLIC void
alloc_sequence_arrays(const char **sequences,
                      short ***S,
                      short ***S5,
                      short ***S3,
                      unsigned short ***a2s,
                      char ***Ss,
                      int circ){

  unsigned int s, n_seq, length;
  if(sequences[0] != NULL){
    length = strlen(sequences[0]);
    for (s=0; sequences[s] != NULL; s++);
    n_seq = s;
    *S    = (short **)          space((n_seq+1) * sizeof(short *));
    *S5   = (short **)          space((n_seq+1) * sizeof(short *));
    *S3   = (short **)          space((n_seq+1) * sizeof(short *));
    *a2s  = (unsigned short **) space((n_seq+1) * sizeof(unsigned short *));
    *Ss   = (char **)           space((n_seq+1) * sizeof(char *));
    for (s=0; s<n_seq; s++) {
      if(strlen(sequences[s]) != length) nrerror("uneqal seqence lengths");
      (*S5)[s]  = (short *)         space((length + 2) * sizeof(short));
      (*S3)[s]  = (short *)         space((length + 2) * sizeof(short));
      (*a2s)[s] = (unsigned short *)space((length + 2) * sizeof(unsigned short));
      (*Ss)[s]  = (char *)          space((length + 2) * sizeof(char));
      (*S)[s]   = (short *)         space((length + 2) * sizeof(short));
      encode_ali_sequence(sequences[s], (*S)[s], (*S5)[s], (*S3)[s], (*Ss)[s], (*a2s)[s], circ);
    }
    (*S5)[n_seq]  = NULL;
    (*S3)[n_seq]  = NULL;
    (*a2s)[n_seq] = NULL;
    (*Ss)[n_seq]  = NULL;
    (*S)[n_seq]   = NULL;
  }
  else nrerror("alloc_sequence_arrays: no sequences in the alignment!");
}

PUBLIC void
free_sequence_arrays( unsigned int n_seq,
                      short ***S,
                      short ***S5,
                      short ***S3,
                      unsigned short ***a2s,
                      char ***Ss){

  unsigned int s;
  for (s=0; s<n_seq; s++) {
    free((*S)[s]);
    free((*S5)[s]);
    free((*S3)[s]);
    free((*a2s)[s]);
    free((*Ss)[s]);
  }
  free(*S);   *S    = NULL;
  free(*S5);  *S5   = NULL;
  free(*S3);  *S3   = NULL;
  free(*a2s); *a2s  = NULL;
  free(*Ss);  *Ss   = NULL;
}

PUBLIC void
encode_ali_sequence(const char *sequence,
                    short *S,
                    short *s5,
                    short *s3,
                    char *ss,
                    unsigned short *as,
                    int circular){

  unsigned int i,l;
  unsigned short p;
  l     = strlen(sequence);
  S[0]  = (short) l;
  s5[0] = s5[1] = 0;

  /* make numerical encoding of sequence */
  for(i=1; i<=l; i++){
    short ctemp;
    ctemp=(short) encode_char(toupper(sequence[i-1]));
    S[i]= ctemp ;
  }

  if (oldAliEn){
    /* use alignment sequences in all energy evaluations */
    ss[0]=sequence[0];
    for(i=1; i<l; i++){
      s5[i] = S[i-1];
      s3[i] = S[i+1];
      ss[i] = sequence[i];
      as[i] = i;
    }
    ss[l]   = sequence[l];
    as[l]   = l;
    s5[l]   = S[l-1];
    s3[l]   = 0;
    S[l+1]  = S[1];
    s5[1]   = 0;
    if (circular) {
      s5[1]   = S[l];
      s3[l]   = S[1];
      ss[l+1] = S[1];
    }
  }
  else{
    if(circular){
      for(i=l; i>0; i--){
        char c5;
        c5 = sequence[i-1];
        if ((c5=='-')||(c5=='_')||(c5=='~')||(c5=='.')) continue;
        s5[1] = S[i];
        break;
      }
      for (i=1; i<=l; i++) {
        char c3;
        c3 = sequence[i-1];
        if ((c3=='-')||(c3=='_')||(c3=='~')||(c3=='.')) continue;
        s3[l] = S[i];
        break;
      }
    }
    else  s5[1]=s3[l]=0;

    for(i=1,p=0; i<=l; i++){
      char c5;
      c5 = sequence[i-1];
      if ((c5=='-')||(c5=='_')||(c5=='~')||(c5=='.'))
        s5[i+1]=s5[i];
      else { /* no gap */
        ss[p++]=sequence[i-1]; /*start at 0!!*/
        s5[i+1]=S[i];
      }
      as[i]=p;
    }
    for (i=l; i>=1; i--) {
      char c3;
      c3 = sequence[i-1];
      if ((c3=='-')||(c3=='_')||(c3=='~')||(c3=='.'))
        s3[i-1]=s3[i];
      else
        s3[i-1]=S[i];
    }
  }
}

PUBLIC int
get_mpi(char *Alseq[],
        int n_seq,
        int length,
        int *mini){

  return vrna_ali_get_mpi(Alseq, n_seq, length, mini);
}

PUBLIC float
energy_of_ali_gquad_structure(const char **sequences,
                              const char *structure,
                              int n_seq,
                              float *energy){

  unsigned int  n;
  short         *pt;
  int           *loop_idx;
  int           en_struct[2], gge[2];

  if(sequences[0] != NULL){
    
    vrna_fold_compound  *vc;

    model_detailsT md;
    set_model_details(&md);

    vc = vrna_get_fold_compound_ali(sequences, &md, VRNA_OPTION_MFE);

    n = vc->length;

    pt = vrna_pt_get(structure);
    energy_of_alistruct_pt(vc, pt, &(en_struct[0]));
    loop_idx    = vrna_get_loop_index(pt);
    en_corr_of_loop_gquad(vc, 1, n, structure, pt, loop_idx, gge);
    en_struct[0] += gge[0];
    en_struct[1] += gge[1];

    free(loop_idx);
    free(pt);
    energy[0] = (float)en_struct[0]/(float)(100*n_seq);
    energy[1] = (float)en_struct[1]/(float)(100*n_seq);

    if(backward_compat_compound && backward_compat)
      destroy_fold_compound(backward_compat_compound);

    backward_compat_compound  = vc;
    backward_compat           = 1;

  }
  else nrerror("energy_of_alistruct(): no sequences in alignment!");

  return energy[0];

}

PUBLIC  float
energy_of_alistruct(const char **sequences,
                    const char *structure,
                    int n_seq,
                    float *energy){

  short         *pt;
  int           en_struct[2];

  if(sequences[0] != NULL){
    vrna_fold_compound  *vc;

    model_detailsT md;
    set_model_details(&md);

    vc = vrna_get_fold_compound_ali(sequences, &md, VRNA_OPTION_MFE);

    pt = vrna_pt_get(structure);
    energy_of_alistruct_pt(vc, pt, &(en_struct[0]));

    free(pt);
    energy[0] = (float)en_struct[0]/(float)(100*n_seq);
    energy[1] = (float)en_struct[1]/(float)(100*n_seq);

    if(backward_compat_compound && backward_compat)
      destroy_fold_compound(backward_compat_compound);

    backward_compat_compound  = vc;
    backward_compat           = 1;

  }
  else nrerror("energy_of_alistruct(): no sequences in alignment!");

  return energy[0];
}