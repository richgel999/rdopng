// //////////////////////////////////////////////////////////
// packagemerge.h
// written by Stephan Brumme, 2021
// see https://create.stephan-brumme.com/length-limited-prefix-codes/
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// compute limited prefix code length based on Larmore/Hirschberg's package-merge algorithm
/** - histogram must be in ascending order and no entry must be zero
 *  - the function rejects maxLength > 63 but I don't see any practical reasons you would need a larger limit ...
 *  @param  maxLength  maximum code length, e.g. 15 for DEFLATE or JPEG
 *  @param  numCodes   number of codes, equals the array size of histogram and codeLength
 *  @param  A [in]     how often each code/symbol was found [out] computed code lengths
 *  @result actual maximum code length, 0 if error
 */
unsigned char packageMergeSortedInPlace(unsigned char maxLength, unsigned int numCodes, unsigned int A[]);


// ---------- same algorithm with a more convenient interface ----------

/// same as before but histogram can be in any order and may contain zeros, the output is stored in a dedicated parameter
/** - the function rejects maxLength > 63 but I don't see any practical reasons you would need a larger limit ...
 *  @param  maxLength  maximum code length, e.g. 15 for DEFLATE or JPEG
 *  @param  numCodes   number of codes, equals the array size of histogram and codeLength
 *  @param  histogram  how often each code/symbol was found
 *  @param  codeLength [out] computed code lengths
 *  @result actual maximum code length, 0 if error
 */
unsigned char packageMerge(unsigned char maxLength, unsigned int numCodes, const unsigned int histogram[], unsigned char codeLengths[]);

#ifdef __cplusplus
}
#endif