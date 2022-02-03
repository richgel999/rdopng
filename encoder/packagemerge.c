// //////////////////////////////////////////////////////////
// packagemerge.c
// written by Stephan Brumme, 2021
// see https://create.stephan-brumme.com/length-limited-prefix-codes/
//

#include "packagemerge.h"
#include <stdlib.h>       // malloc/free/qsort



//#include <stdio.h>



// ----- package-merge algorithm -----

// to me the best explanation is Sebastian Gesemann's Bachelor Thesis (in German only / University of Paderborn, 2004)

// data types (switching to unsigned int is faster but fails if sum(histogram) > 2^31 or maxLength > 31)
typedef unsigned long long BitMask;
typedef unsigned long long HistItem;

/// compute limited prefix code lengths based on Larmore/Hirschberg's package-merge algorithm
/** - histogram must be in ascending order and no entry must be zero
 *  - the function rejects maxLength > 63 but I don't see any practical reasons you would need a larger limit ...
 *  @param  maxLength  maximum code length, e.g. 15 for DEFLATE or JPEG
 *  @param  numCodes   number of codes, equals the array size of histogram and codeLength
 *  @param  A [in]     how often each code/symbol was found [out] computed code lengths
 *  @result actual maximum code length, 0 if error
 */
unsigned char packageMergeSortedInPlace(unsigned char maxLength, unsigned int numCodes, unsigned int A[])
{
  // skip zeros
  while (numCodes > 0 && A[0] == 0)
  {
    numCodes--;
    A++;
  }

  // at least one code needs to be in use
  if (numCodes == 0 || maxLength == 0)
    return 0;

  // one or two codes are always encoded with a single bit
  if (numCodes <= 2)
  {
    A[0] = 1;
    if (numCodes == 2)
      A[1] = 1;
    return 1;
  }

  // A[] is an input  parameter (stores the histogram) as well as
  //        an output parameter (stores the code lengths)
  const unsigned int* histogram   = A;
  unsigned int*       codeLengths = A;

  // my allround variable for various loops
  unsigned int i;

  // check maximum bit length
  if (maxLength > 8*sizeof(BitMask) - 1) // 8*8-1 = 63
    return 0;

  // at least log2(numCodes) bits required for every valid prefix code
  unsigned long long encodingLimit = 1ULL << maxLength;
  if (encodingLimit < numCodes)
    return 0;

  // need two buffers to process iterations and an array of bitmasks
  unsigned int maxBuffer = 2 * numCodes;
  // allocate memory
  HistItem* current  = (HistItem*) malloc(sizeof(HistItem) * maxBuffer);
  HistItem* previous = (HistItem*) malloc(sizeof(HistItem) * maxBuffer);
  BitMask*  isMerged = (BitMask*)  malloc(sizeof(BitMask)  * maxBuffer);

  // initial value of "previous" is a plain copy of the sorted histogram
  for (i = 0; i < numCodes; i++)
    previous[i] = histogram[i];
  unsigned int numPrevious = numCodes;
  // no need to initialize "current", it's completely rebuild every iteration

  // keep track which packages are merged (compact bitmasks):
  // if package p was merged in iteration i then (isMerged[p] & (1 << i)) != 0
  for (i = 0; i < maxBuffer; i++)
    isMerged[i] = 0; // there are no merges before the first iteration

  // the last 2 packages are irrelevant
  unsigned int numRelevant = 2 * numCodes - 2;

  // ... and preparation is finished

  // //////////////////////////////////////////////////////////////////////
  // iterate through potential bit lengths while packaging and merging pairs
  // (step 1 of the algorithm)
  // - the histogram is sorted (prerequisite of the function)
  // - the output must be sorted, too
  // - thus we have to copy the histogram and every and then insert a new package
  // - the code keeps track of the next package and compares it to
  //   the next item to be copied from the history
  // - the smaller value is chosen (if equal, the histogram is chosen)
  // - a bitmask named isMerged is used to keep track which items were packages
  // - repeat until the whole histogram was copied and all packages inserted

  // bitmask for isMerged
  BitMask mask = 1;
  unsigned char bits;
  for (bits = maxLength - 1; bits > 0; bits--)
  {
    // ignore last element if numPrevious is odd (can't be paired)
    numPrevious &= ~1; // bit-twiddling trick to clear the lowest bit, same as numPrevious -= numPrevious % 2

    // first merged package
    current[0] = histogram[0];              // a sum can't be smaller than its parts
    current[1] = histogram[1];              // therefore it's impossible to find a package at index 0 or 1
    HistItem sum = current[0] + current[1]; // same as previous[0] + previous[1]

    // copy histogram and insert merged sums whenever possible
    unsigned int numCurrent = 2;                     // current[0] and current[1] were already set
    unsigned int numHist    = numCurrent;            // we took them from the histogram
    unsigned int numMerged  = 0;                     // but so far no package inserted (however, it's precomputed in "sum")
    for (;;) // stop/break is inside the loop
    {
      // the next package isn't better than the next histogram item ?
      if (numHist < numCodes && histogram[numHist] <= sum)
      {
        // copy histogram item
        current[numCurrent++] = histogram[numHist++];
        continue;
      }

      // okay, we have a package being smaller than next histogram item

      // mark output value as being "merged", i.e. a package
      isMerged[numCurrent] |= mask;

      // store package
      current [numCurrent]  = sum;
      numCurrent++;

      // already finished last package ?
      numMerged++;
      if (numMerged * 2 >= numPrevious)
        break;

      // precompute next sum
      sum = previous[numMerged * 2] + previous[numMerged * 2 + 1];
    }

    // make sure every code from the histogram is included
    // (relevant if histogram is very skewed with a few outliers)
    while (numHist < numCodes)
      current[numCurrent++] = histogram[numHist++];

    // prepare next mask
    mask <<= 1;

    // performance tweak: abort as soon as "previous" and "current" are identical
    if (numPrevious >= numRelevant) // ... at least their relevant elements
    {
      // basically a bool: FALSE == 0, TRUE == 1
      char keepGoing = 0;

      // compare both arrays: if they are identical then stop
      for (i = numRelevant - 1; i > 0; i--) // collisions are most likely at the end
        if (previous[i] != current[i])
        {
          keepGoing++;
          break;
        }

      // early exit ?
      if (keepGoing == 0)
        break;
    }

    // swap pointers "previous" and "current"
    HistItem* tmp = previous;
    previous = current;
    current  = tmp;

    // no need to swap their sizes because only numCurrent needed in next iteration
    numPrevious = numCurrent;
  }

  // shifted one bit too far
  mask >>= 1;

  // keep only isMerged
  free(previous);
  free(current);

  // //////////////////////////////////////////////////////////////////////
  // tracking all merges will produce the code lengths
  // (step 2 of the algorithm)
  // - analyze each bitlength's mask in isMerged:
  //   * a "pure" symbol => increase bitlength of that symbol
  //   * a merged code   => just increase counter
  // - stop if no more merged codes found
  // - if m merged codes were found then only examine
  //   the first 2*m elements in the next iteration
  //   (because only they formed these merged codes)

  // reset code lengths
  for (i = 0; i < numCodes; i++)
    codeLengths[i] = 0;

  // start with analyzing the first 2n-2 values
  unsigned int numAnalyze = numRelevant;
  while (mask != 0) // stops if nothing but symbols are found in an iteration
  {
    // number of merged packages seen so far
    unsigned int numMerged = 0;

    // the first two elements must be symbols, they can't be packages
    codeLengths[0]++;
    codeLengths[1]++;
    unsigned int symbol = 2;

    // look at packages
    for (i = symbol; i < numAnalyze; i++)
    {
      // check bitmask: not merged if bit is 0
      if ((isMerged[i] & mask) == 0)
      {
        // we have a single non-merged symbol, which needs to be one bit longer
        codeLengths[symbol]++;
        symbol++;
      }
      else
      {
        // we have a merged package, so that its parts need to be checked next iteration
        numMerged++;
      }
    }

    // look only at those values responsible for merged packages
    numAnalyze = 2 * numMerged;

    // note that the mask was originally slowly shifted left by the merging loop
    mask >>= 1;
  }

  // last iteration can't have any merges
  for (i = 0; i < numAnalyze; i++)
    codeLengths[i]++;

  // it's a free world ...
  free(isMerged);

  // first symbol has the longest code because it's the least frequent in the sorted histogram
  return (unsigned char)codeLengths[0];
}


// the following code is almost identical to function moffat() in moffat.c


// helper struct for qsort()
struct KeyValue
{
  unsigned int key;
  unsigned int value;
};
// helper function for qsort()
static int compareKeyValue(const void* a, const void* b)
{
  struct KeyValue* aa = (struct KeyValue*) a;
  struct KeyValue* bb = (struct KeyValue*) b;
  // negative if a < b, zero if a == b, positive if a > b
  if (aa->key < bb->key)
    return -1;
  if (aa->key > bb->key)
    return +1;
  return 0;
}


/// same as before but histogram can be in any order and may contain zeros, the output is stored in a dedicated parameter
/** - the function rejects maxLength > 63 but I don't see any practical reasons you would need a larger limit ...
 *  @param  maxLength  maximum code length, e.g. 15 for DEFLATE or JPEG
 *  @param  numCodes   number of codes, equals the array size of histogram and codeLength
 *  @param  histogram  how often each code/symbol was found
 *  @param  codeLength [out] computed code lengths
 *  @result actual maximum code length, 0 if error
 */
unsigned char packageMerge(unsigned char maxLength, unsigned int numCodes, const unsigned int histogram[], unsigned char codeLengths[])
{
  // my allround variable for various loops
  unsigned int i;

  // reset code lengths
  for (i = 0; i < numCodes; i++)
    codeLengths[i] = 0;

  // count non-zero histogram values
  unsigned int numNonZero = 0;
  for (i = 0; i < numCodes; i++)
    if (histogram[i] != 0)
      numNonZero++;

  // reject an empty alphabet because malloc(0) is undefined
  if (numNonZero == 0)
    return 0;

  // allocate a buffer for sorting the histogram
  struct KeyValue* mapping = (struct KeyValue*) malloc(sizeof(struct KeyValue) * numNonZero);
  // copy histogram to that buffer
  unsigned int storeAt = 0;
  for (i = 0; i < numCodes; i++)
  {
    // skip zeros
    if (histogram[i] == 0)
      continue;

    mapping[storeAt].key   = histogram[i];
    mapping[storeAt].value = i;
    storeAt++;
  }
  // now storeAt == numNonZero

  // invoke C standard library's qsort
  qsort(mapping, numNonZero, sizeof(struct KeyValue), compareKeyValue);

  // extract ascendingly ordered histogram
  unsigned int* sorted = (unsigned int*) malloc(sizeof(unsigned int) * numNonZero);
  for (i = 0; i < numNonZero; i++)
    sorted[i] = mapping[i].key;

  // run package-merge algorithm
  unsigned char result = packageMergeSortedInPlace(maxLength, numNonZero, sorted);

  // "unsort" code lengths
  for (i = 0; i < numNonZero; i++)
    codeLengths[mapping[i].value] = (unsigned char)sorted[i];

  // let it go ...
  free(sorted);
  free(mapping);

  return result;
}
