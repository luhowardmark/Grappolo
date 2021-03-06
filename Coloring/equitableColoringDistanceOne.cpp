// **************************************************************************************************
// GrappoloTK: A C++ library for parallel graph coloring
// Hao Lu, Ananth Kalyanaraman (hao.lu@wsu.edu, ananth@eecs.wsu.edu) Washington State University
// Mahantesh Halappanavar (hala@pnnl.gov) Pacific Northwest National Laboratory
//
// For citation, please cite the following paper:
// Lu, Hao, Mahantesh Halappanavar, and Ananth Kalyanaraman. 
// "Parallel heuristics for scalable community detection." Parallel Computing 47 (2015): 19-37.
//
// **************************************************************************************************
// Copyright (c) 2016. Washington State University ("WSU"). All Rights Reserved.
// Permission to use, copy, modify, and distribute this software and its documentation
// for educational, research, and not-for-profit purposes, without fee, is hereby
// granted, provided that the above copyright notice, this paragraph and the following
// two paragraphs appear in all copies, modifications, and distributions. For
// commercial licensing opportunities, please contact The Office of Commercialization,
// WSU, 280/286 Lighty, PB Box 641060, Pullman, WA 99164, (509) 335-5526,
// commercialization@wsu.edu<mailto:commercialization@wsu.edu>, https://commercialization.wsu.edu/

// IN NO EVENT SHALL WSU BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL,
// OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF
// THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF WSU HAS BEEN ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// WSU SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE AND
// ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED HEREUNDER IS PROVIDED "AS IS". WSU HAS NO
// OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
// **************************************************************************************************

#include "defs.h"
#include "coloring.h"
//Compute the size of each color class
//Return: pointer to a vector that stores the size of each color class
void buildColorSize(long NVer, int *vtxColor, int numColors, long *colorSize) {
  assert(colorSize != 0);
  //Count the frequency of each color
#pragma omp parallel for 
  for(long i =0; i<NVer; i++) {
    __sync_fetch_and_add(&colorSize[vtxColor[i]], 1);
  }
}//end of buildColorSize()

/**********************************************************************************/
//double calVariance(GraphElem nv, ColorElem ncolors, ColorVector freq, std::string output)
void computeVariance(long NVer, int numColors, long *colorSize) {
  double avg = (double)NVer/(double)numColors;
  double variance = 0;
  long max = 0;    //Initialize to zero
  long min = NVer; //Initialize to some large number
  
    printf("ci= ");
//#pragma omp parallel for reduction(+:variance), reduction(max:max), reduction(min:min)
  for(long ci=0; ci<numColors; ci++) {
      printf("%ld, ", ci);
    variance  += (avg - (double)colorSize[ci])*(avg - (double)colorSize[ci]);
    if(colorSize[ci] > max)
      max = colorSize[ci];
    if(colorSize[ci] < min)
      min = colorSize[ci];
  }
    printf("\n");
  variance = variance / (double)numColors;
  printf("==========================================\n");
  printf("Characteristics of color class sizes:     \n");
  printf("==========================================\n");
  printf("MinSize  : %ld \n", min);  
  printf("MaxSize  : %ld \n", max);
  printf("Mean     : %g  \n", (double)NVer / (double)numColors);
  printf("Varaince : %g  \n", variance);
  printf("==========================================\n");
  
}//End of calVariance()

//Perform recoloring based on the CFF & CLU schemes
//type: Specifies the type for First-Fit (1 -- default) or Least-Used (2) 
void equitableDistanceOneColorBased(graph *G, int *vtxColor, int numColors, long *colorSize, 
				    int nThreads, double *totTime, int type) {

  printf("Within equitableColorBasedFirstFit(numColors=%d -- nT = %d)\n", numColors, nThreads);
  /*
  if (nThreads < 1)
    omp_set_num_threads(1); //default to one thread
  else
    omp_set_num_threads(nThreads);
  */
  int nT;
#pragma omp parallel
  {
    nT = omp_get_num_threads();
  }
  printf("Actual number of threads: %d (requested: %d)\n", nT, nThreads);

  double time1=0, time2=0, totalTime=0;
  //Get the iterators for the graph:
  long NVer    = G->numVertices;
  long NEdge   = G->numEdges;  
  long *verPtr = G->edgeListPtrs;   //Vertex Pointer: pointers to endV
  edge *verInd = G->edgeList;       //Vertex Index: destination id of an edge (src -> dest)
#ifdef PRINT_DETAILED_STATS_
  printf("Vertices: %ld  Edges: %ld  Num Colors= %ld\n", NVer, NEdge, numColors);
#endif

  //STEP-1: Create a CSR-like data structure for vertex-colors
  time1 = omp_get_wtime();
 
  long *colorIndex = (long *) malloc (NVer * sizeof(long)); assert(colorIndex != 0);
  long *colorAdded = (long *) malloc (numColors * sizeof(long)); assert(colorAdded != 0);
  printf("Reached here...1\n");  

#pragma omp parallel for
  for(long i = 0; i < numColors; i++) {	
    colorAdded[i] = 0;
  }

  printf("Reached here...2\n"); 

 long *colorPtr = (long *) malloc ((numColors+1) * sizeof(long)); assert(colorPtr != 0);
#pragma omp parallel for
  for(long i = 0; i <= numColors; i++) {	
    colorPtr[i] = 0;
  }
  printf("\nReached here... 0.5\n"); 



  // Count the size of each color
//#pragma omp parallel for
  for(long i = 0; i < NVer; i++) {
    __sync_fetch_and_add(&colorPtr[(long)vtxColor[i]+1],1);
  }
  printf("Reached here...2.5\n"); 
  //Prefix sum:
  for(long i=0; i<numColors; i++) {
    colorPtr[i+1] += colorPtr[i];
  }	
  printf("Reached here...3\n");  

  //Group vertices with the same color in particular order
//#pragma omp parallel for
  for (long i=0; i<NVer; i++) {
    long tc = (long)vtxColor[i];
    long Where = colorPtr[tc] + __sync_fetch_and_add(&(colorAdded[tc]), 1);
    colorIndex[Where] = i;
  }
  time2 = omp_get_wtime();
  totalTime += time2 - time1;
  printf("Time to initialize: %3.3lf\n", time2-time1);
  
  //long avgColorSize = (long)ceil( (double)NVer/(double)numColor );
  long avgColorSize = (NVer + numColors - 1) / numColors;
  
  printf("Reached here...3\n");  

  //STEP-2: Start moving the vertices from one color bin to another
  time1 = omp_get_wtime();
  for (long ci=0; ci<numColors; ci++) {
    if(colorSize[ci] <= avgColorSize)
      continue;  //Dont worry if the size is less than the average
    //Now move the vertices to bring the size to average
    long adjC1 = colorPtr[ci];
    long adjC2 = colorPtr[ci+1];
#pragma omp parallel for
    for (long vi=adjC1; vi<adjC2; vi++) {
      if(colorSize[ci] <= avgColorSize)
	continue; //break the loop when enough vertices have been moved
      //Now recolor the vertex:
      long v = colorIndex[vi];
      
      long adj1 = verPtr[v];
      long adj2 = verPtr[v+1];
      long myDegree = verPtr[v+1] - verPtr[v];
      bool *Mark = (bool *) malloc ( numColors * sizeof(bool) );
      assert(Mark != 0);
      for (int i=0; i<numColors; i++) {
	if ( colorSize[i] >= avgColorSize )
	  Mark[i] = true; //Cannot use this color
	else
	  Mark[i] = false; //Else, It is okay to use
      }
      //Browse the adjacency set of vertex v
      for(long k = adj1; k < adj2; k++ ) {
        if ( v == verInd[k].tail ) //Self-loops
	  continue;
	int adjColor = vtxColor[verInd[k].tail];
	//printf("Color[%ld] = %d\n", verInd[k].tail, vtxColor[verInd[k].tail]);
	assert(adjColor >= 0);
	assert(adjColor < numColors); //Fail-safe check
	Mark[adjColor] = true;
      } //End of for loop(k)
      int myColor;
      //Start from the other end:
      for (myColor=0; myColor<numColors; myColor++) {
	if ( Mark[myColor] == false )
	  break;
      }
      if ((myColor >= 0) && (myColor < numColors) ) { //Found a valid choice
	vtxColor[v] = myColor; //Re-color the vertex
	__sync_fetch_and_add(&colorSize[myColor], 1); //Increment the size of the new color
	__sync_fetch_and_sub(&colorSize[ci], 1); //Decrement the size of the old color
      }      
      free(Mark);
    } //End of outer for loop(vi)    
  }//End of for(ci)
  time2  = omp_get_wtime();
  totalTime += time2 - time1;
#ifdef PRINT_DETAILED_STATS_
  printf("Time taken for re-coloring:  %lf sec.\n", time2-time1);
  printf("Total Time for re-coloring:  %lf sec.\n", totalTime);
#endif
  
  *totTime = totalTime;

  
  ///////////////////////////////////////////////////////////////////////////
  /////////////////// VERIFY THE COLORS /////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////
  //Verify Results and Cleanup 
  int myConflicts = 0;
#pragma omp parallel for
  for (long v=0; v < NVer; v++ ) {
    long adj1 = verPtr[v];
    long adj2 = verPtr[v+1];
    //Browse the adjacency set of vertex v
    for(long k = adj1; k < adj2; k++ ) {
      if ( v == verInd[k].tail ) //Self-loops
        continue;
      if ( vtxColor[v] == vtxColor[verInd[k].tail] ) {
        __sync_fetch_and_add(&myConflicts, 1); //increment the counter
      }
    }//End of inner for loop: w in adj(v)
  }//End of outer for loop: for each vertex
  myConflicts = myConflicts / 2; //Have counted each conflict twice
  if (myConflicts > 0)
    printf("Check - WARNING: Number of conflicts detected after resolution: %d \n\n", myConflicts);
  else
    printf("Check - SUCCESS: No conflicts exist\n\n");
  
}//End of colorBasedEquitable()
	
