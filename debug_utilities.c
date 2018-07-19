#include "debug_utilities.h"
#include "pluto.h"
/***************************************************************************************
 * [Ema] Print matrix (useful for calling inside gdb) 
 ****************************************************************************************/
void printmat(double **matrix, int dim2, int dim1)
{   
    char coltop[11];
    int i, j;
    for (i = 0; i < dim1; ++i){
      sprintf(coltop, "--%d--", i );
      printf("%21s", coltop);
    }
    printf("\n");
    for (j = 0; j < dim2; ++j) {
      printf("%2d|", j);
        for (i = 0; i < dim1; ++i)
            printf("%21.15g", matrix[j][i]);
        printf(";\n");
    }
    for (i = 0; i < dim1; ++i){
      sprintf(coltop, "--%d--", i );
      printf("%21s", coltop);
    }
    printf("\n");

    // for (i = 0; i < dim1; ++i) {
    //     for (j = 0; j < dim2; ++j)
    //         fprintf(stdout, "%g ", matrix[i][j]);
    //     fprintf(stdout, "\n");
    // }

    // for (i = 0; i < dim1; ++i) {
    //     for (j = 0; j < dim2; ++j)
    //         fprintf(stderr, "%g ", matrix[i][j]);
    //     fprintf(stderr, "\n");
    // }
}

/***************************************************************************************
 * [Ema] Print matrix from 4D vector choosing dimensions to print (useful for calling inside gdb)
 * whichX (X=0,1,2,3) : Integer which tells whether the dimension X has to be printed (whichX = -1)
 *          or kept fixed (whichX = value to keep)
 ****************************************************************************************/
void printmat4d(double ****matrix, int dim2, int dim1, int which0, int which1, int which2, int which3 ) {
    char coltop[11];
    int i, j;
    
    for (i = 0; i < dim1; ++i){
      sprintf(coltop, "--%d--", i );
      printf("%21s", coltop);
    }
      
    printf("\n");
    for (j = 0; j < dim2; ++j) {
      printf("%2d|", j);
      for (i = 0; i < dim1; ++i) {
        if        (which0==-1 && which1==-1) {
          printf("%21.15g", matrix[j][i][which2][which3]);
        } else if (which0==-1 && which2==-1) {
          printf("%21.15g", matrix[j][which1][i][which3]);
        } else if (which0==-1 && which3==-1) {
          printf("%21.15g", matrix[j][which1][which2][i]);
        } else if (which1==-1 && which2==-1) {
          printf("%21.15g", matrix[which0][j][i][which3]);
        } else if (which1==-1 && which3==-1) {
          printf("%21.15g", matrix[which0][j][which2][i]);
        } else if (which2==-1 && which3==-1) {
          printf("%21.15g", matrix[which0][which1][j][i]);
        }
      }
      printf(";\n");
    }

    for (i = 0; i < dim1; ++i){
      sprintf(coltop, "--%d--", i );
      printf("%21s", coltop);
    }
    printf("\n");
}