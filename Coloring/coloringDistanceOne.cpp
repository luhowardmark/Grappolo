// **************************************************************************************************
// Grappolo: A C++ library for parallel graph community detection
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


//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////  DISTANCE ONE COLORING      ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//Return the number of colors used (zero is a valid color)
int algoDistanceOneVertexColoringOpt(graph *G, int *vtxColor, int nThreads, double *totTime)
{
#ifdef PRINT_DETAILED_STATS_
  printf("Within algoDistanceOneVertexColoringOpt()\n");
#endif

  if (nThreads < 1)
		omp_set_num_threads(1); //default to one thread
  else
		omp_set_num_threads(nThreads);
  int nT;

	#pragma omp parallel
  {
		nT = omp_get_num_threads();
  }

#ifdef PRINT_DETAILED_STATS_
  printf("Actual number of threads: %d (requested: %d)\n", nT, nThreads);
#endif
	
  double time1=0, time2=0, totalTime=0;
  //Get the iterators for the graph:
  long NVer    = G->numVertices;
  long NEdge   = G->numEdges;  
  long *verPtr = G->edgeListPtrs;   //Vertex Pointer: pointers to endV
  edge *verInd = G->edgeList;       //Vertex Index: destination id of an edge (src -> dest)

#ifdef PRINT_DETAILED_STATS_
  printf("Vertices: %ld  Edges: %ld\n", NVer, NEdge);
#endif

  //Build a vector of random numbers
  double *randValues = (double*) malloc (NVer * sizeof(double));
  assert(randValues != 0);
  generateRandomNumbers(randValues, NVer);

  long *Q    = (long *) malloc (NVer * sizeof(long)); assert(Q != 0);
  long *Qtmp = (long *) malloc (NVer * sizeof(long)); assert(Qtmp != 0);
  long *Qswap;    
  if( (Q == NULL) || (Qtmp == NULL) ) {
    printf("Not enough memory to allocate for the two queues \n");
    exit(1);
  }
  long QTail=0;    //Tail of the queue 
  long QtmpTail=0; //Tail of the queue (implicitly will represent the size)
  long realMaxDegree = 0;
	
	#pragma omp parallel for
  for (long i=0; i<NVer; i++) {
      Q[i]= i;     //Natural order
      Qtmp[i]= -1; //Empty queue
  }
  QTail = NVer;	//Queue all vertices


	// Cal real Maximum degree, 2x for maxDegree to be safe
	#pragma omp parallel for reduction(max: realMaxDegree)
	for (long i = 0; i < NVer; i++) {
		long adj1, adj2, de;
		adj1 = verPtr[i];
		adj2 = verPtr[i+1];
		de = adj2-adj1;
		if ( de > realMaxDegree)
			realMaxDegree = de;
	}
	//realMaxDegree *= 1.5;

	ColorVector freq(MaxDegree,0);
  /////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////// START THE WHILE LOOP ///////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////
  long nConflicts = 0; //Number of conflicts 
  int nLoops = 0;     //Number of rounds of conflict resolution

#ifdef PRINT_DETAILED_STATS_ 
  printf("Results from parallel coloring:\n");
  printf("***********************************************\n");
#endif
  do{
    ///////////////////////////////////////// PART 1 ////////////////////////////////////////
    //Color the vertices in parallel - do not worry about conflicts
#ifdef PRINT_DETAILED_STATS_
    printf("** Iteration : %d \n", nLoops);
#endif

    time1 = omp_get_wtime();
		#pragma omp parallel for
    for (long Qi=0; Qi<QTail; Qi++) {
      long v = Q[Qi]; //Q.pop_front();
			int maxColor = 0;
			BitVector mark(MaxDegree, false);
			maxColor = distanceOneMarkArray(mark,G,v,vtxColor);
				
			int myColor;
			for (myColor=0; myColor<=maxColor; myColor++) {
				if ( mark[myColor] == false )
					break;
			}     
			vtxColor[v] = myColor; //Color the vertex
		} //End of outer for loop: for each vertex
		
		time1  = omp_get_wtime() - time1;
		totalTime += time1;

#ifdef PRINT_DETAILED_STATS_
    printf("Time taken for Coloring:  %lf sec.\n", time1);
#endif
    ///////////////////////////////////////// PART 2 ////////////////////////////////////////
    //Detect Conflicts:
    //printf("Phase 2: Detect Conflicts, add to queue\n");    
    //Add the conflicting vertices into a Q:
    //Conflicts are resolved by changing the color of only one of the 
    //two conflicting vertices, based on their random values 
    time2 = omp_get_wtime();
		
		#pragma omp parallel for
		for (long Qi=0; Qi<QTail; Qi++) {
			long v = Q[Qi]; //Q.pop_front();
			distanceOneConfResolution(G, v, vtxColor, randValues, &QtmpTail, Qtmp, freq, 0);
		} //End of outer for loop: for each vertex
  
		time2  = omp_get_wtime() - time2;
		totalTime += time2;    
		nConflicts += QtmpTail;
		nLoops++;

#ifdef PRINT_DETAILED_STATS_
    printf("Num conflicts      : %ld \n", QtmpTail);
    printf("Time for detection : %lf sec\n", time2);
#endif

    //Swap the two queues:
    Qswap = Q;
    Q = Qtmp; //Q now points to the second vector
    Qtmp = Qswap;
    QTail = QtmpTail; //Number of elements
    QtmpTail = 0; //Symbolic emptying of the second queue    
  } while (QTail > 0);
  //Check the number of colors used
  int nColors = -1;
  for (long v=0; v < NVer; v++ ) 
    if (vtxColor[v] > nColors) nColors = vtxColor[v];
#ifdef PRINT_DETAILED_STATS_
  printf("***********************************************\n");
  printf("Total number of colors used: %d \n", nColors);    
  printf("Number of conflicts overall: %ld \n", nConflicts);  
  printf("Number of rounds           : %d \n", nLoops);      
  printf("Total Time                 : %lf sec\n", totalTime);
  printf("***********************************************\n");
#endif  
  *totTime = totalTime;
  //////////////////////////// /////////////////////////////////////////////////////////////
  ///////////////////////////////// VERIFY THE COLORS /////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////

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
  //Clean Up:
  free(Q);
  free(Qtmp);
  free(randValues);
  
  return nColors; //Return the number of colors used
}


//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////  DISTANCE ONE COLORING      ///////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//Return the number of colors used (zero is a valid color)
int algoDistanceOneVertexColoring(graph *G, int *vtxColor, int nThreads, double *totTime)
{
	printf("Within algoDistanceOneVertexColoring()\n");
	if (nThreads < 1)
		omp_set_num_threads(1); //default to one thread
	else
		omp_set_num_threads(nThreads);
	int nT;
#pragma omp parallel
	{
		nT = omp_get_num_threads();
	}
	printf("Actual number of threads: %d (requested: %d)\n", nT, nThreads);
	
	
	double time1=0, time2=0, totalTime=0;
  //Get the iterators for the graph:
  long NVer    = G->numVertices;
  long NS      = G->sVertices;
  long NT      = NVer - NS;
  long NEdge           = G->numEdges;
  long *verPtr         = G->edgeListPtrs;   //Vertex Pointer: pointers to endV
  edge *verInd         = G->edgeList;       //Vertex Index: destination id of an edge (src -> dest)
  printf("Vertices: %ld  Edges: %ld\n", NVer, NEdge);

  //const int MaxDegree = 4096; //Increase if number of colors is larger    

  //Build a vector of random numbers
  double *randValues = (double*) malloc (NVer * sizeof(double));
  if( randValues == NULL ) {
    printf("Not enough memory to allocate for random numbers \n");
    exit(1);
  }
  generateRandomNumbers(randValues, NVer);

  //The Queue Data Structure for the storing the vertices 
  //   the need to be colored/recolored
  //Have two queues - read from one, write into another
  //   at the end, swap the two.
  long *Q    = (long *) malloc (NVer * sizeof(long));
  long *Qtmp = (long *) malloc (NVer * sizeof(long));
  long *Qswap;    
  if( (Q == NULL) || (Qtmp == NULL) ) {
    printf("Not enough memory to allocate for the two queues \n");
    exit(1);
  }
  long QTail=0;    //Tail of the queue 
  long QtmpTail=0; //Tail of the queue (implicitly will represent the size)
  
#pragma omp parallel for
  for (long i=0; i<NVer; i++) {
      Q[i]= i;     //Natural order
      Qtmp[i]= -1; //Empty queue
  }
  QTail = NVer;	//Queue all vertices
  /////////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////// START THE WHILE LOOP ///////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////
  long nConflicts = 0; //Number of conflicts 
  int nLoops = 0;     //Number of rounds of conflict resolution
  int *Mark = (int *) malloc ( MaxDegree * NVer * sizeof(int) );
  if( Mark == NULL ) {
    printf("Not enough memory to allocate for Mark \n");
    exit(1);
  }
#pragma omp parallel for
  for (long i=0; i<MaxDegree*NVer; i++)
     Mark[i]= -1;

  printf("Results from parallel coloring:\n");
  printf("***********************************************\n");
  do {
    ///////////////////////////////////////// PART 1 ////////////////////////////////////////
    //Color the vertices in parallel - do not worry about conflicts
    printf("** Iteration : %d \n", nLoops);
    time1 = omp_get_wtime();
#pragma omp parallel for
    for (long Qi=0; Qi<QTail; Qi++) {
      long v = Q[Qi]; //Q.pop_front();
      long StartIndex = v*MaxDegree; //Location in Mark
      if (nLoops > 0) //Skip the first time around
	for (long i=StartIndex; i<(StartIndex+MaxDegree); i++)
	  Mark[i]= -1;
      long adj1 = verPtr[v];
      long adj2 = verPtr[v+1];
      int maxColor = -1;
      int adjColor = -1;
      //Browse the adjacency set of vertex v
      for(long k = adj1; k < adj2; k++ ) {
	//if ( v == verInd[k] ) //Skip self-loops
	//continue;
	adjColor =  vtxColor[verInd[k].tail];
	if ( adjColor >= 0 ) {
	  Mark[StartIndex+adjColor] = v;
	  //Find the largest color in the neighborhood
	  if ( adjColor > maxColor )
	    maxColor = adjColor;
	}
      } //End of for loop to traverse adjacency of v
      int myColor;
      for (myColor=0; myColor<=maxColor; myColor++) {
	if ( Mark[StartIndex+myColor] != v )
	  break;
      }
      if (myColor == maxColor)
	myColor++; /* no available color with # less than cmax */
      vtxColor[v] = myColor; //Color the vertex
    } //End of outer for loop: for each vertex
    time1  = omp_get_wtime() - time1;
    totalTime += time1;
    printf("Time taken for Coloring:  %lf sec.\n", time1);

    ///////////////////////////////////////// PART 2 ////////////////////////////////////////
    //Detect Conflicts:
    //printf("Phase 2: Detect Conflicts, add to queue\n");    
    //Add the conflicting vertices into a Q:
    //Conflicts are resolved by changing the color of only one of the 
    //two conflicting vertices, based on their random values 
    time2 = omp_get_wtime();
#pragma omp parallel for
    for (long Qi=0; Qi<QTail; Qi++) {
      long v = Q[Qi]; //Q.pop_front();
      long adj1 = verPtr[v];
      long adj2 = verPtr[v+1];      
      //Browse the adjacency set of vertex v
      for(long k = adj1; k < adj2; k++ ) {
	//if ( v == verInd[k] ) //Self-loops
	//continue;
	if ( vtxColor[v] == vtxColor[verInd[k].tail] ) {
	  //Q.push_back(v or w)
	  if ( (randValues[v] < randValues[verInd[k].tail]) || 
	       ((randValues[v] == randValues[verInd[k].tail])&&(v < verInd[k].tail)) ) {
	    long whereInQ = __sync_fetch_and_add(&QtmpTail, 1);
	    Qtmp[whereInQ] = v;//Add to the queue
	    vtxColor[v] = -1;  //Will prevent v from being in conflict in another pairing
	    break;
	  }
	} //End of if( vtxColor[v] == vtxColor[verInd[k]] )
      } //End of inner for loop: w in adj(v)
    } //End of outer for loop: for each vertex
    time2  = omp_get_wtime() - time2;
    totalTime += time2;    
    nConflicts += QtmpTail;
    nLoops++;
    printf("Conflicts          : %ld \n", QtmpTail);
    printf("Time for detection : %lf sec\n", time2);
    //Swap the two queues:
    Qswap = Q;
    Q = Qtmp; //Q now points to the second vector
    Qtmp = Qswap;
    QTail = QtmpTail; //Number of elements
    QtmpTail = 0; //Symbolic emptying of the second queue    
  } while (QTail > 0);
  //Check the number of colors used
  int nColors = -1;
  for (long v=0; v < NVer; v++ ) 
    if (vtxColor[v] > nColors) nColors = vtxColor[v];
  printf("***********************************************\n");
  printf("Total number of colors used: %d \n", nColors);    
  printf("Number of conflicts overall: %ld \n", nConflicts);  
  printf("Number of rounds           : %d \n", nLoops);      
  printf("Total Time                 : %lf sec\n", totalTime);
  printf("***********************************************\n");
  
  *totTime = totalTime;
  /////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////// VERIFY THE COLORS /////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////
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
	//#pragma omp atomic
	//printf("Conflict: color[%ld]=%d AND color[%ld]=%d\n", v, vtxColor[v], verInd[k].tail, vtxColor[ verInd[k].tail]);
	__sync_fetch_and_add(&myConflicts, 1); //increment the counter
      }
    }//End of inner for loop: w in adj(v)
  }//End of outer for loop: for each vertex
  myConflicts = myConflicts / 2; //Have counted each conflict twice
  if (myConflicts > 0)
    printf("Check - WARNING: Number of conflicts detected after resolution: %d \n\n", myConflicts);
  else
    printf("Check - SUCCESS: No conflicts exist\n\n");
  //Clean Up:
  free(Q);
  free(Qtmp);
  free(Mark); 
  free(randValues);
  
  return nColors; //Return the number of colors used
}
