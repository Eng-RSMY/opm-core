#include <stdlib.h>

#include <grid.h>

#include "cgridinterface.h"

   

static int *compute_cell_facepos(grid_t *g)
{
   int i,j,k;
   int *facepos = malloc((g->number_of_cells + 1) * sizeof *facepos);
   int *fcells  = g->face_cells;
   
   for (i=0; i<g->number_of_cells; ++i) {
      facepos [i] = 0;
   }
   
   for (i=0; i<2*g->number_of_faces; ++i) {
         if (*fcells != -1) {
            (facepos[*fcells])++;
         }
         fcells++;
   }
   
   /* cumsum */
   j=0;
   for (i=0; i<g->number_of_cells; ++i) {
      k = j + facepos[i];
      facepos[i] = j;
      j = k;
   }
   facepos[i] = j;

   return facepos;
}


static int *compute_cell_faces(grid_t *g)
{
   int *cfaces = malloc(g->cell_facepos[g->number_of_cells] * sizeof *cfaces);
   int *work   = malloc(g->number_of_cells * sizeof *work);
   int *fcells = g->face_cells;
   int i,k,cell;
   for(i=0; i<g->number_of_cells; ++i) {
      work[i] = 0;
   }
  
   for (i=0; i<g->number_of_faces; ++i) {
      for (k=0;k<2; ++k) {
         
         if (*fcells != -1) {
            cell = *fcells;
            cfaces[g->cell_facepos[cell] + work[cell]] = i;
            work[cell]++;
         }
         fcells++;
      }
   }
   free(work);   

   return cfaces;
}

void preprocess         (const struct grdecl   *in, 
                         double                tol, 
                         struct CornerpointGrid *G)
{
   struct processed_grid    pg;
   struct UnstructuredGrid *base;

   base = (struct UnstructuredGrid *) G;

   process_grdecl(in, tol, &pg);

   /* 
    *  General grid interface 
    */
   base->dimensions = 3;

   base->number_of_nodes  = pg.number_of_nodes;
   base->number_of_faces  = pg.number_of_faces;
   base->number_of_cells  = pg.number_of_cells;

   base->node_coordinates = pg.node_coordinates;

   base->face_nodes       = pg.face_nodes;
   base->face_nodepos     = pg.face_ptr;
   base->face_cells       = pg.face_neighbors;
   
   base->face_centroids   = NULL;
   base->face_normals     = NULL;
   base->face_areas       = NULL;
   
   /* NB: compute_cell_facepos must be called before compute_cell_faces */
   base->cell_facepos     = compute_cell_facepos(base);
   base->cell_faces       = compute_cell_faces  (base);
   base->cell_centroids   = NULL;
   base->cell_volumes     = NULL;


   /* 
    *  Cornerpoint grid interface 
    */
   G->cartdims[0]      = pg.dimensions[0];
   G->cartdims[1]      = pg.dimensions[1];
   G->cartdims[2]      = pg.dimensions[2];

#if 0
   base->face_tag       = pg.face_tag;
#else
   free(pg.face_tag);
#endif

   G->index_map = pg.local_cell_index;
}
   
void free_cornerpoint_grid(struct CornerpointGrid *G)
{
    free(G->grid.face_nodes);
    free(G->grid.face_nodepos);
    free(G->grid.face_cells);
    free(G->grid.cell_facepos);
    free(G->grid.cell_faces);

    free(G->grid.node_coordinates);
    free(G->grid.face_centroids);
    free(G->grid.face_areas);
    free(G->grid.face_normals);
    free(G->grid.cell_centroids);
    free(G->grid.cell_volumes);

    free(G->index_map);
}
