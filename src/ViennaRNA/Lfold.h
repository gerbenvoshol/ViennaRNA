#ifndef VIENNA_RNA_PACKAGE_LFOLD_H
#define VIENNA_RNA_PACKAGE_LFOLD_H

/**
 */

/* make this interface backward compatible with RNAlib < 2.2.0 */
#define VRNA_BACKWARD_COMPAT

/**
 *  \addtogroup local_fold
 *
 *  Local structures can be predicted by a modified version of the
 *  fold() algorithm that restricts the span of all base pairs.
 *  @{
 *    \file Lfold.h
 *    \brief Predicting local MFE structures of large sequences
 *
 *  @}
 */

/**
 *  \addtogroup local_mfe_fold
 *  @{
 *
 *  @}
 */


/**
 *  \brief
 * 
 *  \ingroup local_mfe_fold
 * 
 *  \param string
 *  \param structure
 *  \param maxdist
 *  \param zsc
 *  \param min_z
 */
float Lfoldz( const char *string,
              char *structure,
              int maxdist,
              int zsc,
              double min_z);


/**
 *  \addtogroup local_consensus_fold
 *  @{
 *
 *  @}
 */

/**
 *  \brief
 *
 *  \ingroup local_consensus_fold
 * 
 *  \param strings
 *  \param structure
 *  \param maxdist
 *  \return
 */
float aliLfold( const char **strings,
                char *structure,
                int maxdist);

#ifdef  VRNA_BACKWARD_COMPAT

/**
 *  \brief The local analog to fold().
 * 
 *  Computes the minimum free energy structure including only base pairs
 *  with a span smaller than 'maxdist'
 *
 *  \ingroup local_mfe_fold
 * 
 *  \param string
 *  \param structure
 *  \param maxdist
 */
float Lfold(const char *string,
            char *structure,
            int maxdist);

#endif

#endif
