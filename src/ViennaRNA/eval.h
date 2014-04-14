#ifndef __VIENNA_RNA_PACKAGE_EVAL_H__
#define __VIENNA_RNA_PACKAGE_EVAL_H__

#include <stdio.h>
#include <ViennaRNA/data_structures.h>

#ifdef __GNUC__
#define DEPRECATED(func) func __attribute__ ((deprecated))
#else
#define DEPRECATED(func) func
#endif

/**
 *  \file eval.h
 *  \brief Functions and variables related to energy evaluation
 *  of sequence/structure pairs.
 */


/**
 *  \defgroup eval Energy evaluation
 *  @{
 *    \brief This module contains all functions and variables related to energy evaluation
 *    of sequence/structure pairs.
 *
 *
 *  @}
 */

/** \brief set to first pos of second seq for cofolding  */
extern  int cut_point;

/**
 *  \brief verbose info from energy_of_struct
 *  \ingroup eval
 */
extern  int eos_debug;

/**
 *  \addtogroup eval Energy evaluation
 *  \ingroup folding_routines
 *  @{
 *    \brief This module contains all functions and variables related to energy evaluation
 *    of sequence/structure pairs.
 *  @}
 */


/**
 *  \brief Calculate the free energy of an already folded RNA
 *
 *  This function allows for energy evaluation of a given sequence/structure pair.
 *  If the optional parameter 'P' is not NULL, the scoring model as determined by 'P'
 *  will be used for energy evaluation. Otherwise, default parameters are used.
 *
 *  \ingroup eval
 *
 *  \see vrna_eval_structure_pt(), vrna_eval_structure_verbose(), vrna_eval_structure_pt_verbose(),
 *  vrna_get_energy_contributions()
 *
 *  \param string           RNA sequence in uppercase letters
 *  \param structure        Secondary structure in dot-bracket notation
 *  \param P       A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \return                 The free energy of the input structure given the input sequence in kcal/mol
 */
float vrna_eval_structure(const char *string,
                          const char *structure,
                          paramT *P);

/**
 *  \brief Calculate the free energy of an already folded RNA and print contributions per loop.
 *
 *  This function allows for detailed energy evaluation of a given sequence/structure pair.
 *  In contrast to vrna_eval_structure() this function prints detailed energy contributions
 *  based on individual loops to a file handle. If NULL is passed as file handle, this function
 *  defaults to print to stdout.
 *  If the optional parameter 'P' is not NULL, the scoring model as determined by 'P'
 *  will be used for energy evaluation. Otherwise, default parameters are used.
 *
 *  \ingroup eval
 *
 *  \see vrna_eval_structure_pt(), vrna_eval_structure_verbose(), vrna_eval_structure_pt_verbose(),
 *  vrna_get_energy_contributions()
 *
 *  \param string           RNA sequence in uppercase letters
 *  \param structure        Secondary structure in dot-bracket notation
 *  \param P                A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \param file             A file handle where this function should print to (may be NULL).
 *  \return                 The free energy of the input structure given the input sequence in kcal/mol
 */
float vrna_eval_structure_verbose(const char *string,
                                  const char *structure,
                                  paramT *P,
                                  FILE *file);

/**
 *  \brief Calculate the free energy of an already folded RNA
 *
 *  This function allows for energy evaluation of a given sequence/structure pair.
 *  If the optional parameter 'P' is not NULL, the scoring model as determined by 'P'
 *  will be used for energy evaluation. Otherwise, default parameters are used.
 *
 *  \ingroup eval
 *
 *  \see vrna_pt_get(), vrna_eval_structure_verbose(), vrna_eval_structure_pt_verbose(),
 *  vrna_get_energy_contributions()
 *
 *  \param string           RNA sequence in uppercase letters
 *  \param pt               Secondary structure as pair_table
 *  \param P                A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \return                 The free energy of the input structure given the input sequence in 10cal/mol
 */
int vrna_eval_structure_pt( const char *string,
                            const short *pt,
                            paramT *P);

/**
 *  \brief Calculate the free energy of an already folded RNA
 *
 *  This function allows for energy evaluation of a given sequence/structure pair.
 *  In contrast to vrna_eval_structure() this function prints detailed energy contributions
 *  based on individual loops to a file handle. If NULL is passed as file handle, this function
 *  defaults to print to stdout.
 *  If the optional parameter 'P' is not NULL, the scoring model as determined by 'P'
 *  will be used for energy evaluation. Otherwise, default parameters are used.
 *
 *  \ingroup eval
 *
 *  \see vrna_pt_get(), vrna_eval_structure_verbose(), vrna_eval_structure_pt_verbose(),
 *  vrna_get_energy_contributions()
 *
 *  \param string           RNA sequence in uppercase letters
 *  \param pt               Secondary structure as pair_table
 *  \param P                A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \param file             A file handle where this function should print to (may be NULL).
 *  \return                 The free energy of the input structure given the input sequence in 10cal/mol
 */
int vrna_eval_structure_pt_verbose( const char *string,
                                    const short *pt,
                                    paramT *P,
                                    FILE *file);

int vrna_eval_structure_pt_fast(const char *string,
                                const short *pt,
                                const short *s,
                                const short *s1,
                                paramT *parameters);

/**
 * \brief Calculate energy of a loop
 *
 *  \param pt         the pair table of the secondary structure
 *  \param s          encoded RNA sequence
 *  \param s1         encoded RNA sequence
 *  \param i          position of covering base pair
 *  \param P          A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \returns          free energy of the loop in 10cal/mol
 */
int vrna_eval_loop_pt(const short *pt,
                      const short *s,
                      const short *s1,
                      int i,
                      paramT *P);

/** 
 * \brief Calculate energy of a move (closing or opening of a base pair)
 *
 *  If the parameters m1 and m2 are negative, it is deletion (opening)
 *  of a base pair, otherwise it is insertion (opening).
 *  If the optional parameter 'P' is not NULL, the scoring model as determined by 'P'
 *  will be used for energy evaluation. Otherwise, default parameters are used.
 *
 *  \ingroup eval
 *
 *  \see              vrna_eval_move_pt(), vrna_get_energy_contributions()
 *  \param string     RNA sequence
 *  \param structure  secondary structure in dot-bracket notation
 *  \param m1         first coordinate of base pair
 *  \param m2         second coordinate of base pair
 *  \param P          A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \returns          energy change of the move in kcal/mol
 */
float vrna_eval_move( const char *string,
                      const char *structure,
                      int m1,
                      int m2,
                      paramT *P);

/**
 * 
 * \brief Calculate energy of a move (closing or opening of a base pair)
 *
 *  If the parameters m1 and m2 are negative, it is deletion (opening)
 *  of a base pair, otherwise it is insertion (opening).
 *  If the optional parameter 'P' is not NULL, the scoring model as determined by 'P'
 *  will be used for energy evaluation. Otherwise, default parameters are used.
 *
 *  \ingroup eval
 *
 *  \see              vrna_eval_move(), vrna_get_energy_contributions()
 *  \param pt         the pair table of the secondary structure
 *  \param s          encoded RNA sequence
 *  \param s1         encoded RNA sequence
 *  \param m1         first coordinate of base pair
 *  \param m2         second coordinate of base pair
 *  \param P          A data structure containing the prescaled energy contributions and the model details (may be NULL).
 *  \returns          energy change of the move in 10cal/mol
 */
int vrna_eval_move_pt(short *pt,
                      const short *s,
                      const short *s1,
                      int m1,
                      int m2,
                      paramT *P);

/**
 *  \brief Calculate the free energy of an already folded RNA using global model detail settings
 *
 *  If verbosity level is set to a value >0, energies of structure elements are printed to stdout
 *
 *  \note OpenMP: This function relies on several global model settings variables and thus is
 *        not to be considered threadsafe. See energy_of_struct_par() for a completely threadsafe
 *        implementation.
 *
 *  \ingroup eval
 *  \deprecated Use vrna_eval_structure() or vrna_eval_structure_verbose() instead!
 *
 *  \see vrna_eval_structure()
 *
 *  \param string     RNA sequence
 *  \param structure  secondary structure in dot-bracket notation
 *  \param verbosity_level a flag to turn verbose output on/off
 *  \return          the free energy of the input structure given the input sequence in kcal/mol
 */
DEPRECATED(float energy_of_structure(const char *string,
                          const char *structure,
                          int verbosity_level));

/**
 *  \brief Calculate the free energy of an already folded RNA
 *
 *  If verbosity level is set to a value >0, energies of structure elements are printed to stdout
 *
 *  \ingroup eval
 *  \deprecated Use vrna_eval_structure() or vrna_eval_structure_verbose() instead!
 *
 *  \see vrna_eval_structure()
 *
 *  \param string           RNA sequence in uppercase letters
 *  \param structure        Secondary structure in dot-bracket notation
 *  \param parameters       A data structure containing the prescaled energy contributions and the model details.
 *  \param verbosity_level  A flag to turn verbose output on/off
 *  \return                The free energy of the input structure given the input sequence in kcal/mol
 */
DEPRECATED(float energy_of_struct_par( const char *string,
                            const char *structure,
                            paramT *parameters,
                            int verbosity_level));

/**
 *  \brief Calculate the free energy of an already folded  circular RNA
 *
 *  \note OpenMP: This function relies on several global model settings variables and thus is
 *        not to be considered threadsafe. See energy_of_circ_struct_par() for a completely threadsafe
 *        implementation.
 *
 *  If verbosity level is set to a value >0, energies of structure elements are printed to stdout
 *
 *  \ingroup eval
 *
 *  \deprecated Use vrna_eval_structure() or vrna_eval_structure_verbose() instead!
 *
 *  \see vrna_eval_structure()
 *
 *  \param string           RNA sequence
 *  \param structure        Secondary structure in dot-bracket notation
 *  \param verbosity_level  A flag to turn verbose output on/off
 *  \return                The free energy of the input structure given the input sequence in kcal/mol
 */
DEPRECATED(float energy_of_circ_structure( const char *string,
                                const char *structure,
                                int verbosity_level));

/**
 *  \brief Calculate the free energy of an already folded circular RNA
 *
 *  If verbosity level is set to a value >0, energies of structure elements are printed to stdout
 *
 *  \ingroup eval
 *
 *  \deprecated Use vrna_eval_structure() or vrna_eval_structure_verbose() instead!
 *
 *  \see vrna_eval_structure()
 *
 *  \param string           RNA sequence
 *  \param structure        Secondary structure in dot-bracket notation
 *  \param parameters       A data structure containing the prescaled energy contributions and the model details.
 *  \param verbosity_level  A flag to turn verbose output on/off
 *  \return                The free energy of the input structure given the input sequence in kcal/mol
 */
DEPRECATED(float energy_of_circ_struct_par(const char *string,
                                const char *structure,
                                paramT *parameters,
                                int verbosity_level));


DEPRECATED(float energy_of_gquad_structure(const char *string,
                                const char *structure,
                                int verbosity_level));

DEPRECATED(float energy_of_gquad_struct_par( const char *string,
                                  const char *structure,
                                  paramT *parameters,
                                  int verbosity_level));


/**
 *  \brief Calculate the free energy of an already folded RNA
 *
 *  If verbosity level is set to a value >0, energies of structure elements are printed to stdout
 *
 *  \note OpenMP: This function relies on several global model settings variables and thus is
 *        not to be considered threadsafe. See energy_of_struct_pt_par() for a completely threadsafe
 *        implementation.
 *
 *  \ingroup eval
 *
 *  \deprecated Use vrna_eval_structure_pt() or vrna_eval_structure_pt_verbose() instead!
 *
 *  \see vrna_eval_structure_pt()
 *
 *  \param string     RNA sequence
 *  \param ptable     the pair table of the secondary structure
 *  \param s          encoded RNA sequence
 *  \param s1         encoded RNA sequence
 *  \param verbosity_level a flag to turn verbose output on/off
 *  \return          the free energy of the input structure given the input sequence in 10kcal/mol
 */
DEPRECATED(int energy_of_structure_pt( const char *string,
                            short *ptable,
                            short *s,
                            short *s1,
                            int verbosity_level));

/**
 *  \brief Calculate the free energy of an already folded RNA
 *
 *  If verbosity level is set to a value >0, energies of structure elements are printed to stdout
 *
 *  \ingroup eval
 *
 *  \deprecated Use vrna_eval_structure_pt() or vrna_eval_structure_pt_verbose() instead!
 *
 *  \see vrna_eval_structure_pt()
 *
 *  \param string           RNA sequence in uppercase letters
 *  \param ptable           The pair table of the secondary structure
 *  \param s                Encoded RNA sequence
 *  \param s1               Encoded RNA sequence
 *  \param parameters       A data structure containing the prescaled energy contributions and the model details.
 *  \param verbosity_level  A flag to turn verbose output on/off
 *  \return                The free energy of the input structure given the input sequence in 10kcal/mol
 */
DEPRECATED(int energy_of_struct_pt_par(const char *string,
                            short *ptable,
                            short *s,
                            short *s1,
                            paramT *parameters,
                            int verbosity_level));



/** 
 * \brief Calculate energy of a move (closing or opening of a base pair)
 *
 *  If the parameters m1 and m2 are negative, it is deletion (opening)
 *  of a base pair, otherwise it is insertion (opening).
 *
 *  \deprecated Use vrna_eval_move() instead!
 *
 *  \see vrna_eval_move()
 *
 *  \param string     RNA sequence
 *  \param structure  secondary structure in dot-bracket notation
 *  \param m1         first coordinate of base pair
 *  \param m2         second coordinate of base pair
 *  \returns          energy change of the move in kcal/mol
 */
DEPRECATED(float energy_of_move( const char *string,
                      const char *structure,
                      int m1,
                      int m2));


/**
 * 
 * \brief Calculate energy of a move (closing or opening of a base pair)
 *
 *  If the parameters m1 and m2 are negative, it is deletion (opening)
 *  of a base pair, otherwise it is insertion (opening).
 *
 *  \deprecated Use vrna_eval_move_pt() instead!
 *
 *  \see vrna_eval_move_pt()
 *
 *  \param pt         the pair table of the secondary structure
 *  \param s          encoded RNA sequence
 *  \param s1         encoded RNA sequence
 *  \param m1         first coordinate of base pair
 *  \param m2         second coordinate of base pair
 *  \returns          energy change of the move in 10cal/mol
 */
DEPRECATED(int energy_of_move_pt(short *pt,
                   short *s,
                   short *s1,
                   int m1,
                   int m2));

/**
 * \brief Calculate energy of a loop
 *
 *  \deprecated Use vrna_eval_loop_pt() instead!
 *
 *  \see vrna_eval_loop_pt()
 *
 *  \param ptable     the pair table of the secondary structure
 *  \param s          encoded RNA sequence
 *  \param s1         encoded RNA sequence
 *  \param i          position of covering base pair
 *  \returns          free energy of the loop in 10cal/mol
 */
DEPRECATED(int   loop_energy(short *ptable,
                  short *s,
                  short *s1,
                  int i));

/**
 *  Calculate the free energy of an already folded RNA
 * 
 *  \note This function is not entirely threadsafe! Depending on the state of the global
 *  variable \ref eos_debug it prints energy information to stdout or not...\n
 * 
 *  \deprecated This function is deprecated and should not be used in future programs!
 *  Use \ref energy_of_structure() instead!
 * 
 *  \see              energy_of_structure, energy_of_circ_struct(), energy_of_struct_pt()
 *  \param string     RNA sequence
 *  \param structure  secondary structure in dot-bracket notation
 *  \return          the free energy of the input structure given the input sequence in kcal/mol
 */
DEPRECATED(float energy_of_struct(const char *string,
                                  const char *structure));

/**
 *  Calculate the free energy of an already folded RNA
 * 
 *  \note This function is not entirely threadsafe! Depending on the state of the global
 *  variable \ref eos_debug it prints energy information to stdout or not...\n
 * 
 *  \deprecated This function is deprecated and should not be used in future programs!
 *  Use \ref energy_of_structure_pt() instead!
 * 
 *  \see              make_pair_table(), energy_of_structure()
 *  \param string     RNA sequence
 *  \param ptable     the pair table of the secondary structure
 *  \param s          encoded RNA sequence
 *  \param s1         encoded RNA sequence
 *  \return          the free energy of the input structure given the input sequence in 10kcal/mol
 */
DEPRECATED(int energy_of_struct_pt( const char *string,
                                    short *ptable,
                                    short *s,
                                    short *s1));

/**
 *  Calculate the free energy of an already folded  circular RNA
 * 
 *  \note This function is not entirely threadsafe! Depending on the state of the global
 *  variable \ref eos_debug it prints energy information to stdout or not...\n
 * 
 *  \deprecated This function is deprecated and should not be used in future programs
 *  Use \ref energy_of_circ_structure() instead!
 * 
 *  \see              energy_of_circ_structure(), energy_of_struct(), energy_of_struct_pt()
 *  \param string     RNA sequence
 *  \param structure  secondary structure in dot-bracket notation
 *  \return          the free energy of the input structure given the input sequence in kcal/mol
 */
DEPRECATED(float energy_of_circ_struct( const char *string,
                                        const char *structure));

#endif